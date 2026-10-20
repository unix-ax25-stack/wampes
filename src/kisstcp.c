/* kisstcp.c
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

/* KISS-over-TCP - kisstcp, and under the name tcpkiss also from BPQ.
 *
 * THE FRAME: FEND, a KISS type byte, the escaped AX.25 frame, FEND.  The
 * first byte after the FEND is NOT an AX.25 byte but channel and command in
 * the upper and lower nibble.  Anyone who skips it shifts every field in the
 * frame by one and then gets a cryptic CRC message instead of the explanation
 * (Thomas).
 *
 * THE REAL QUESTION IS NOT THE ESCAPING BUT THE STREAM.  KISS is
 * self-synchronising, and that is its whole advantage: 0xc0 proves that a
 * frame has ended, no matter what the stream looked like before.  A read() is
 * not a frame boundary - half a read() is not half a frame, it is only half a
 * frame.  So collect everything that is there, and hand WAMPES only ever a
 * complete frame.
 */

#include "configure.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "cmdparse.h"
#include "iface.h"
#include "devparam.h"
#include "slip.h"			/* FR_END, FR_ESC, T_FR_END, T_FR_ESC */
#include "ax25.h"
#include "axip.h"
#include "crc.h"
#include "tcpsock.h"
#include "kiss.h"

/* THE TYPE BYTE, and it is two fields and not one:
 *
 *      7 6 5 4  3 2 1 0
 *     [ channel ][ command ]
 *
 * The upper nibble is the KISS channel, the lower one the command - zero for
 * a data frame, 1..6 for setting a TNC parameter, 15 for leaving KISS.  Dire
 * Wolf writes it exactly so: stemp[0] = (chan << 4) | kiss_cmd (kissnet.c).
 *
 * AND THE SAME TWO NIBBLES CARRY TWO CHECKSUM FLAGS, which is the collision
 * this file has to live with.  SMACK puts bit 7 - which is channel 8 in plain
 * KISS - over its port number and a CRC-16/ARC behind the frame; FlexNet
 * writes the whole byte as 0x20 - channel 2 in plain KISS - and a FlexNet
 * checksum behind it.  A single frame cannot say which of the two it means.
 *
 * WHAT RESOLVES IT IS THE CHECKSUM ITSELF.  A 0x20 frame whose FlexNet
 * residue comes out as 0x7070 is FlexNet; one that does not is a channel-2
 * frame from somebody who took this port for a multiport one, and is refused
 * as such.  kiss.c does the same on the serial line, and Dire Wolf warns
 * about the ambiguity rather than choosing.  WHETHER WE MAY LOOK AT ALL is the
 * port's "ifconfig <iface> crc" setting (iface.c, ifcrc): "auto" looks, and
 * learns per partner - bit 7 means SMACK, bit 5 means FlexNet, and whichever
 * proves out is what we answer with; "smack"/"flexnet" look for exactly one;
 * "off" does not look, and then 0x20 and 0x80 are simply foreign channels.
 * A freshly attached port is in auto (see kisstcp_attach()).
 *
 * The lower nibble is the command, and none of this touches it: a checksummed
 * frame still has command 0.
 */
#define KISS_CMD_DATA_FRAME	0

/* THE CHANNEL, AND WHY ZERO IS ENOUGH.  Dire Wolf's rule is "one TCP port per
 * channel".  That is not convenience: the channel number stands in the KISS
 * frame and not in the TCP connection, and a programmer holds it in his head.
 * We take channel zero and leave the rest lying instead of assigning it to a
 * channel it was not in (Thomas).
 */
#define KISS_CHAN		0

/* Forward declarations */
static void kisstcp_send_cmd(struct tcpsock *tp, struct mbuf *bp);

/* THE STATE, AND ONE VALUE PER PORT. */

/* What fits into one mbuf over TCP: the AX.25 frame without the KISS frame.
 */
#define MAX_TXFRAME		2048

/*---------------------------------------------------------------------------*/

/* FORWARD A KISS COMMAND TO BRIDGED INTERFACE.
 * Called when a KISS command frame (cmd != 0) is received on a kisstcp
 * interface that has a bridgegroup.  We find the peer interface(s) in the
 * same bridgegroup and send the KISS command frame to them.
 */
static void
kisstcp_forward_cmd(
struct tcpsock *tp,
unsigned char *frame,
int flen,
int orig_type)
{
  struct iface *ifp;
  struct mbuf *bp;
  unsigned char *cp;

  if (flen < 2)
    return;			/* need at least type byte and value */

  /* Build a KISS frame mbuf: [type byte][value...] */
  if ((bp = alloc_mbuf(flen)) == NULL)
    return;
  cp = bp->data;
  memcpy(cp, frame, flen);
  bp->cnt = flen;

  /* Find peer interface(s) in the same bridgegroup */
  for (ifp = Ifaces; ifp != NULL; ifp = ifp->next) {
    if (ifp == tp->ifp)
      continue;
    if (ifp->bridgegroup && ifp->bridgegroup == tp->ifp->bridgegroup) {
      /* Found bridged interface - send KISS command via appropriate function */
      if (ifp->raw == kiss_raw) {
        /* Serial KISS interface - use kiss_send_cmd which skips extra type byte/CRC */
        struct mbuf *copy = copy_p(bp, bp->cnt);
        if (copy != NULL) {
          kiss_send_cmd(ifp, &copy);
        }
      } else if (ifp->raw != NULL) {
        /* Other interface types (e.g., another kisstcp) - use their raw function */
        struct mbuf *copy = copy_p(bp, bp->cnt);
        if (copy != NULL) {
          (*ifp->raw)(ifp, &copy);
        }
      }
    }
  }
  free_p(&bp);
}

/*---------------------------------------------------------------------------*/

/* THE INPUT.  What tcpsock.c has collected in tp->buf is split into frames
 * here.  At the end the rest that is not yet a complete frame is moved to the
 * front - it stays there until the rest of the stream arrives.
 */
int
kisstcp_rx(
struct tcpsock *tp)
{
  int off = 0;
  int segstart;
  int esclen;
  int flen;
  int i;
  unsigned char raw[TCPSOCK_MAXFRAME];
  unsigned char frame[TCPSOCK_MAXFRAME];
  int type;
  int cmd;

  while (off < tp->len) {
    /* UP TO THE FIRST FEND.  Everything before it is rubbish or the rest of
     * an earlier frame - both is skipped.  A stream that begins with half a
     * frame after a reset is the normal case after an RST and no reason to
     * abort.
     */
    for (segstart = off; segstart < tp->len; segstart++)
      if (tp->buf[segstart] == FR_END)
	break;
    if (segstart >= tp->len)
      break;			/* no FEND yet: wait */

    /* TWO FENDS IN A ROW ARE AN EMPTY FRAME, and that is allowed in KISS.  It
     * is discarded rather than reported as an error, because it is not one:
     * it is what a partner sends when the channel is quiet, and a counter that
     * grows by twelve an hour on a perfectly healthy line has stopped being
     * read (Thomas).
     *
     * BUT IT IS COUNTED, separately and in its own column.  The reason is the
     * NAT box: a mapping that has been dropped is not reported to either end,
     * both believe the connection is up, and nothing is sent for ever.  A
     * partner that can send the empty frame is a partner whose connection can
     * be kept alive - and that is the one thing about a quiet port that cannot
     * be told apart from a port whose peer has vanished.  One count per run of
     * FENDs, however long it is: c0 c0 c0 is one partner being quiet, not three
     * of anything.
     */
    for (off = segstart + 1; off < tp->len && tp->buf[off] == FR_END; off++) ;
    if (off > segstart + 1)
      tp->idlesin++;
    if (off >= tp->len)
      break;

    /* THE RAW SECTION, collected unescaped.  We collect the whole thing first
     * and unpack afterwards.  An escape runs across the boundary of two
     * read()s, and anyone who meets half an escape while unpacking needs a
     * state across the call - that is the reason for the second buffer and
     * against a state machine with five states.
     */
    for (esclen = 0; off < tp->len; off++) {
      if (tp->buf[off] == FR_END)
	break;
      if (esclen >= (int) sizeof(raw)) {
	tp->overruns++;
	tp->ifp->crcerrors++;	/* the interface counts too - see axtcp.c */
	tcpsock_forget(tp);
	return -1;
      }
      raw[esclen++] = tp->buf[off];
    }
    if (off >= tp->len)
      break;			/* no closing FEND: wait */

    /* UNESCAPING.  FESC T_FR_END is a FEND, FESC T_FR_ESC a FESC, everything
     * else stands as it is.  A real FESC without a successor is a broken
     * peer - but the bytes after it are still the bytes they have to be, and
     * are not thrown away with it.
     */
    for (i = 0, flen = 0; i < esclen; i++) {
      if (flen >= (int) sizeof(frame)) {
	tp->overruns++;
	tp->ifp->crcerrors++;	/* the interface counts too - see axtcp.c */
	tcpsock_forget(tp);
	return -1;
      }
      if (raw[i] == FR_ESC && i + 1 < esclen) {
	i++;
	frame[flen++] = (raw[i] == T_FR_END) ? FR_END :
			(raw[i] == T_FR_ESC) ? FR_ESC : raw[i];
      } else {
	frame[flen++] = raw[i];
      }
    }
    if (flen < 1)
      continue;			/* only rubbish between two FEND */
    /* UNREACHABLE, and left that way on purpose rather than decorated with a
     * counter: the two loops above already skipped every FEND that follows
     * another one, so the first byte collected here is never a FEND and esclen
     * is at least 1; unescaping appends one byte per collected byte, so flen
     * is at least 1 too.  An "empty frame" in KISS is the c0 c0 above, and
     * that one is deliberately not an error - it is what a partner sends to
     * say the channel is quiet, and a peer that does it every minute must not
     * fill the counter (Thomas).
     */

    /* THE TYPE BYTE, and it is mandatory, not a matter of form: only from
     * here on is the rest the AX.25 frame.
     */
    type = frame[0];

    /* SAVE THE COMMAND (lower nibble) before CRC handling modifies the type.
     * The original type byte has: bits 7-4 = channel, bits 3-0 = command.
     * CRC flags (0x80, 0x20) are in the upper nibble along with channel.
     */
    cmd = type & 0x0f;

    /* THE CHECKSUM, AND IT COMES FIRST.  It is computed over [type byte][frame]
     * - the serial port does it the same way (kiss.c), and a FlexNet partner
     * who computed his own over that range will not recognise one we computed
     * without it.  Which dialect a flag means, and why the flag is not simply a
     * channel, stands at the top of this file.
     */
    if ((type & 0x80) || (type & 0x20)) {
      /* SMACK announces itself in bit 7, FlexNet in bit 5. */
      int kind = (type & 0x80) ? CRC_16 : CRC_RMNC;
      /* MAY WE LOOK?  "auto" (crcfixed == 0) may, for both dialects; a pinned
       * port may look only for the one it was pinned to.  Anything else -
       * above all "off" - does not look, and the frame then goes the way of
       * every other unexpected channel, below.
       */
      int look = !tp->ifp->crcfixed || tp->ifp->crccontrol == kind;
      int ok = 0;

      if (look) {
        struct mbuf *bp = qdata(frame, (uint) flen);

        if (bp != NULL) {
          ok = (kind == CRC_16) ? (check_crc_16(bp) == 0) :
                                  (check_crc_rmnc(bp) == 0);
          free_p(&bp);
        }
      }
      if (!ok) {
        /* EITHER A BAD CHECKSUM OR A MULTIPORT FRAME.  The two cannot be
         * told apart - see the collision at the top - and both are the same
         * error to us: a partner who speaks a dialect this port was not
         * opened for.  Counted once, in the same place as a foreign channel.
         */
        tp->crcerr++;
        tp->ifp->crcerrors++;
        continue;
      }
      /* THE CHECKSUM WAS RIGHT, so this is a dialect we speak and not a
       * channel.  Learning is per session and only in auto; a pinned port
       * must not be overwritten by the peer (iface.c, ifcrc).
       */
      if (!tp->ifp->crcfixed)
        tp->crcmode = kind;
      /* THE TWO TRAILING BYTES ARE THE CHECKSUM, and the type byte did the
       * checksum and not a channel: it is 0 from here on.
       */
      flen -= 2;
      type = KISS_CMD_DATA_FRAME;
    }

    flen--;
    memmove(frame, frame + 1, (size_t) flen);

    /* THE UPPER NIBBLE IS THE CHANNEL.  We take zero and leave everything else
     * lying.  Channel 3 is a partner that wants to tell us something on
     * channel 3, and that is not our channel; reading it as channel zero would
     * be the error that shows up as "unknown callsigns" and not as "wrong
     * channel".  And it is counted, because a partner on one TCP port that
     * speaks for two channels was told to use two ports (Thomas).
     *
     * 0x20 and 0x80 never reach here: a frame that carried them was taken for
     * a checksum above or refused as a foreign channel there, and a checksummed
     * one had its type byte set to 0 on the way.
     */
    if (((type >> 4) & 0x0f) != KISS_CHAN) {
      tp->crcerr++;
      tp->ifp->crcerrors++;
      continue;
    }

    /* THE LOWER NIBBLE IS THE COMMAND, and only 0 is a data frame.  What
     * stands in the rest is a TNC parameter being set (1..6) or, at 15, a
     * partner leaving KISS - for a TNC we are not.
     *
     * BUT IT IS NOT AN ERROR, and it used to be counted as one.  A partner
     * who sets a TXDELAY is doing what KISS allows him to, and a counter that
     * punishes it shows a healthy port as a broken one.
     *
     * IF THIS INTERFACE IS BRIDGED, FORWARD THE KISS COMMAND to the
     * bridged interface (e.g., a serial KISS port).  The bridgegroup
     * mechanism groups interfaces; we find the peer interface and call
     * its raw output function.
     */
    if (cmd != KISS_CMD_DATA_FRAME) {
      if (tp->ifp->bridgegroup) {
        kisstcp_forward_cmd(tp, frame, flen, type);
      }
      continue;
    }

    /* AND NOTHING MORE TO CUT OFF.  An announced checksum was handled above;
     * what follows is the AX.25 frame and nothing else.  The frame carries its
     * own FCS, which check_crc_16() in ax25 does, and it must NOT be cut off
     * here: ax25 appends one again when it sends.
     */
    if (flen > 0)
      tcpsock_input(tp, frame, flen, KISS_CHAN);
  }

  /* THE REST STAYS.  Everything that was not a complete frame is moved to the
   * front and waits for the rest of the stream - that is the whole reason for
   * this buffer.
   */
  if (off > 0) {
    memmove(tp->buf, tp->buf + off, (size_t) (tp->len - off));
    tp->len -= off;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE CHECKSUM THIS SESSION SPEAKS ON THE WAY OUT.
 *
 * THE PORT SAYS WHAT IS ALLOWED, THE SESSION SAYS WHAT WAS LEARNED.  A port
 * pinned with "ifconfig <iface> crc off|smack|flexnet" answers with exactly
 * that; a port left in "auto" answers with what its partner's frames have
 * taught it, and - until the first checksummed frame arrives - with none at
 * all.  See the top of this file for why starting with none is right and not
 * an omission.
 */
static int
kisstcp_crcmode(
struct tcpsock *tp)
{
  if (tp->ifp->crcfixed)
    return tp->ifp->crccontrol;
  return tp->crcmode;
}

/* THE OUTPUT.  FEND, the type byte (with a checksum flag when the partner
 * speaks one), the escaped frame, FEND.
 *
 * THE TYPE BYTE GOES ON BEFORE THE CHECKSUM IS COMPUTED, and that is not
 * cosmetic: the checksum runs over [type byte][frame], exactly as on the
 * receiving side, and a FlexNet partner will reject one computed over the
 * frame alone.  kiss.c's kiss_raw() puts it on first for the same reason.
 *
 * WHICH CHECKSUM is asked of kisstcp_crcmode() above.  A client that greeted a
 * stranger with a SMACK would be told "we do not do that here" and
 * disconnected, while a client that starts with plain KISS and follows the
 * peer is welcome everywhere (Thomas).
 */
int
kisstcp_tx(
struct tcpsock *tp,
unsigned char *out,
int max,
struct mbuf **bpp)
{
  int l;
  int i;
  int n = 0;
  /* THREE BYTES MORE THAN THE FRAME tcpsock_raw() hands in: the type byte and,
   * when a checksum is added, its two.  pullup() never writes more than
   * sizeof(buf) and leaves the rest in *bpp, so an over-long frame falls
   * through the test below instead of running over.
   */
  unsigned char buf[MAX_TXFRAME + 3];

  if (max < 3)
    return -1;
  pushdown(bpp, NULL, 1);
  (*bpp)->data[0] = PARAM_DATA;	/* channel 0, data */
  switch (kisstcp_crcmode(tp)) {
  case CRC_16:
    (*bpp)->data[0] |= 0x80;	/* SMACK: bit 7 over the port number */
    append_crc_16(*bpp);
    break;
  case CRC_RMNC:
    (*bpp)->data[0] |= 0x20;	/* FlexNet: the whole byte, no port */
    append_crc_rmnc(*bpp);
    break;
  default:
    break;			/* off: type byte 0x00 and no checksum */
  }
  if ((l = pullup(bpp, buf, sizeof(buf))) <= 0 || *bpp)
    return -1;

  out[n++] = FR_END;
  for (i = 0; i < l; i++) {
    if (n + 3 >= max)
      return -1;
    if (buf[i] == FR_END) {
      out[n++] = FR_ESC;
      out[n++] = T_FR_END;
    } else if (buf[i] == FR_ESC) {
      out[n++] = FR_ESC;
      out[n++] = T_FR_ESC;
    } else {
      out[n++] = buf[i];
    }
  }
  out[n++] = FR_END;
  return n;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE OF THIS PROTOCOL: FEND FEND, two bytes and nothing else.
 *
 * THAT IS ONE FRAME OF NO CONTENT between two FENDs, and it is the KISS way of
 * saying "the channel is quiet" - which is exactly what we mean, and it is why
 * no byte has to be invented for the purpose.  TODO.txt asks here for "ein
 * Keepalive/Sync-Byte 0xC0", and one lone FEND would not do: it leaves the
 * receiver's decoder standing in the middle of a frame with no end in sight,
 * and a decoder waiting for an end is a decoder that cannot synchronise itself
 * out of it.  A FEND pair closes what it opens, which is the whole property
 * that makes KISS findable again in the stream.
 */
int
kisstcp_keepalive(
struct tcpsock *tp,
unsigned char *out,
int max)
{
  (void) tp;
  if (max < 2)
    return -1;
  out[0] = FR_END;
  out[1] = FR_END;
  return 2;
}

/*---------------------------------------------------------------------------*/

/* IOCTL FOR KISS-TCP - sends KISS command frames to the TNC.
 * This allows remote configuration of a KISS TNC over TCP.
 */
int32
kisstcp_ioctl(
struct iface *iface,
int cmd,
int set,
int32 val
){
	struct mbuf *hbp;
	uint8 *cp;
	int rval = 0;
	struct tcpsock *tp;

	/* Find the tcpsock for this interface */
	for (tp = Tcp_socks; tp != NULL; tp = tp->next) {
		if (tp->ifp == iface)
			break;
	}
	if (tp == NULL)
		return -1;

	switch(cmd){
	case PARAM_RETURN:
		set = 1;        /* Note fall-thru */
	case PARAM_TXDELAY:
	case PARAM_PERSIST:
	case PARAM_SLOTTIME:
	case PARAM_TXTAIL:
	case PARAM_FULLDUP:
	case PARAM_HW:
	case 12:                /* echo */
	case 13:                /* rxdelay */
		if(!set){
			rval = -1;      /* Can't read back */
			break;
		}
		/* Allocate space for cmd and arg */
		if((hbp = alloc_mbuf(2)) == NULL){
			free_p(&hbp);
			rval = -1;
			break;
		}
		cp = hbp->data;
		*cp++ = cmd & 0x0f;
		*cp = (unsigned char) val;
		hbp->cnt = 2;
		/* Send via kisstcp_tx directly (bypass normal framing) */
		kisstcp_send_cmd(tp, hbp);
		rval = (int) val;
		break;
	default:
		rval = -1;
		break;
	}
	return rval;
}

/*---------------------------------------------------------------------------*/

/* SEND A KISS COMMAND FRAME.
 * Frame format: FEND [type byte] [command] [value] FEND
 * Value byte is escaped if needed (FEND/FESC).
 */
static void
kisstcp_send_cmd(
struct tcpsock *tp,
struct mbuf *bp)
{
	unsigned char out[MAX_TXFRAME + 3];
	int n = 0;
	int i;
	unsigned char *frame = bp->data;
	int flen = bp->cnt;

	if (flen < 2)
		return;

	/* Build KISS frame: type byte (channel 0 | cmd), then value */
	out[n++] = FR_END;
	out[n++] = (KISS_CHAN << 4) | (frame[0] & 0x0f);  /* type byte */

	/* Escape the value byte if needed */
	if (frame[1] == FR_END) {
		out[n++] = FR_ESC;
		out[n++] = T_FR_END;
	} else if (frame[1] == FR_ESC) {
		out[n++] = FR_ESC;
		out[n++] = T_FR_ESC;
	} else {
		out[n++] = frame[1];
	}

	/* Any additional data bytes (shouldn't happen for KISS cmd, but be safe) */
	for (i = 2; i < flen; i++) {
		if (n + 2 >= (int) sizeof(out))
			break;
		if (frame[i] == FR_END) {
			out[n++] = FR_ESC;
			out[n++] = T_FR_END;
		} else if (frame[i] == FR_ESC) {
			out[n++] = FR_ESC;
			out[n++] = T_FR_ESC;
		} else {
			out[n++] = frame[i];
		}
	}
	out[n++] = FR_END;

	/* Write directly to socket */
	write(tp->fd, out, n);

	free_p(&bp);
}

/*---------------------------------------------------------------------------*/

/* THE FIRST BYTE DECIDES.  0xc0 is FEND and therefore KISS; anything else can
 * only be the upper half of an AXTCP length field, because 0xc0 as a length
 * would be 49152 bytes (XRouter against BPQ).
 *
 * ONLY ONCE, and that is the reason for the state TCPAD_DETECT: a protocol
 * change in mid-operation would be something one negotiates with a protocol,
 * and not something one guesses at from one byte.
 */
int
kisstcp_detect(
struct tcpsock *tp)
{
  if (tp->len < 1)
    return 0;			/* nothing there yet: keep waiting */
  if (tp->buf[0] != FR_END)
    return -1;			/* not KISS */
  tp->proto = TCPAD_KISSTCP;
  return 1;
}

/*---------------------------------------------------------------------------*/

/* attach kisstcp [<label> [listen [<port>] | client <host>[:<port>]]]
 *                [type=kiss|axtcp|auto] [bind=<addr>[,<addr>]...]
 *                [keepalive <seconds>] [shared-key <code>]
 *
 * THE ALIAS, and it is exactly "attach axtcp type=kiss": KISS pinned, default
 * port 8001.  The body is shared (tcpsock_attach(), tcpsock.c); the framing is
 * what this file keeps.
 */
int
kisstcp_attach(
int argc,
char *argv[],
void *p)
{
  return tcpsock_attach(argc, argv, p, "kisstcp", TCPAD_KISSTCP);
}