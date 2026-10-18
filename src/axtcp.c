/* axtcp.c
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

/* AXTCP - AX.25 in TCP with a length field, the way XRouter does it.  Port
 * 9393.
 *
 * THE FRAME: two bytes of length in big-endian order, then the complete
 * AX.25 frame WITH CRC.  The length counts the AX.25 frame and not itself,
 * and the CRC sits at the end of the length area, not above it - it is the
 * transcript, not the check on the transmission.  Anyone who counts it into
 * the length shifts every field by two bytes and gets a CRC message without
 * any content (Thomas).
 *
 * KISS IS THE MORE DANGEROUS CASE, and precisely because of the MISSING FEND.
 * KISS can re-enter the stream exactly right after a byte was lost, because
 * 0xc0 proves the start.  AXTCP cannot: one byte too many, and every
 * following length field is rubbish.  That is why the CRC here is not
 * decoration but the only way to find the stream again - it is correct exactly
 * when the frame was complete and in the right place, and otherwise it is a
 * matter of luck that one should not ask about.
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
#include "slip.h"			/* only FR_END: 0xc0 */
#include "ax25.h"
#include "axip.h"
#include "crc.h"
#include "tcpsock.h"

/* WHAT A LENGTH FIELD MAY CLAIM AT ALL.  It is 16 bits, and the peer will use
 * them - but an AX.25 frame is at most a few hundred bytes, and anything above
 * that is either an error or someone who wants to fill the buffer.  Without
 * this ceiling the buffer reserves 64 KB on the strength of a claim (Thomas).
 */
#define AXTCP_MAXLEN		2048

/* THE CHANNEL IS ALWAYS ZERO HERE.  AXTCP knows no channels - that is the
 * difference to KISS and the reason why the same axip table can serve both
 * and two tables are not needed.
 */
#define AXTCP_CHAN		0

/* THE STATE, AND ONE VALUE PER PORT.  It is one of three and is compared in
 * the header in exactly one place - a shared value would be right here and
 * wrong in the next place.
 */

/*---------------------------------------------------------------------------*/

/* THE INPUT.  As long as the length field is not complete there is nothing to
 * say; as long as the announced number of bytes is not complete, neither.
 * And when both are complete, the CRC decides whether this was a frame at all.
 */
int
axtcp_rx(
struct tcpsock *tp)
{
  int len;
  int off = 0;

  while (tp->len - off >= 2) {
    len = (tp->buf[off] << 8) | tp->buf[off + 1];
    /* A LENGTH FIELD THAT CANNOT BE.  Unlike KISS there is no way here to
     * find the stream again: every byte that arrives now only changes WHERE
     * the absurdity begins.  So we disconnect, and the port waits for the
     * next attempt.
     */
    if (len > AXTCP_MAXLEN) {
      tp->overruns++;
      /* BOTH COUNTERS, and not only the one that "attach ... stat" shows.  A
       * wrong frame is a wrong frame whether one asks the session or the
       * interface: ifp->crcerrors is what ifconfig prints and what axip.c and
       * kiss.c have always counted, and a port that reports its errors only
       * in one of the two places is half as good as axip (Thomas).
       */
      tp->ifp->crcerrors++;
      tcpsock_forget(tp);
      return -1;
    }
    if (tp->len - off < 2 + len)
      break;			/* not complete yet: wait */

    /* THE CRC IS THE TRANSCRIPT, and it is the only proof that the frame was
     * complete and in the right place.  Without it everything that is not
     * just accidentally two bytes long would pass here.
     */
    if (len < 2 || !check_crc_ccitt((char *) tp->buf + off + 2, len)) {
      tp->crcerr++;
      tp->ifp->crcerrors++;	/* the interface counts too - see above */
      tcpsock_forget(tp);
      return -1;
    }
    /* THE KEEPALIVE, and it is recognisable without guessing: a length of 2 is
     * a frame of no bytes with the two CRC bytes over nothing, and the CRC of
     * nothing is 0x0000.  check_crc_ccitt() above has just confirmed that the
     * two bytes behind the length field are exactly that, so there is nothing
     * left to decide.
     *
     * IT IS NOT AN ERROR AND NOT A FRAME, and both matter.  Passed on to
     * tcpsock_input() it would come back as an overrun - that one counts a
     * length below two addresses (tcpsock.c:181) - and a port that sends one
     * of these every five minutes would then show an overrun that climbs by
     * twelve an hour for ever, which is the one thing a counter must not do.
     * And it must not reach ax25() as a zero byte frame, because ax25 would
     * file it as a heard station.
     */
    if (len == 2) {
      tp->idlesin++;
      off += 2 + len;
      continue;
    }
    /* WITHOUT THE TWO CRC BYTES, because ax25 appends its own again.
     */
    tcpsock_input(tp, tp->buf + off + 2, len - 2, AXTCP_CHAN);
    off += 2 + len;
  }
  /* SEVERAL FRAMES IN ONE read() ARE THE NORMAL CASE, not the exception: a
   * TCP MTU of 1460 and a frame of 60 bytes mean twenty-four frames at once.
   * Anyone who reads only one loses the rest in silence, and the loss only
   * shows when the peer goes quiet (Thomas).
   */
  if (off > 0) {
    memmove(tp->buf, tp->buf + off, (size_t) (tp->len - off));
    tp->len -= off;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE OUTPUT.  Length, then the frame, and the CRC belongs inside the length
 * area - it is part of what the length numbers.
 */
int
axtcp_tx(
struct tcpsock *tp,
unsigned char *out,
int max,
struct mbuf **bpp)
{
  int l;

  if (max < 3)
    return -1;
  /* THE CRC BEFORE THE PULLUP: pullup() hands over exactly the area that goes
   * out, and exactly that is what the length says.
   */
  append_crc_ccitt(*bpp);
  if ((l = pullup(bpp, out + 2, (uint) (max - 2))) <= 0 || *bpp)
    return -1;
  out[0] = (unsigned char) ((l >> 8) & 0xff);
  out[1] = (unsigned char) (l & 0xff);
  return l + 2;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE OF THIS PROTOCOL: 00 02 00 00.
 *
 * A LENGTH OF 2, because the length counts the frame together with its CRC
 * (TODO.txt: "Laengenfeld Big-Endian, umfasst Payload", and the payload is
 * "vollstaendiger AX.25-Frame einschl. FCS").  Two bytes of payload is
 * therefore an AX.25 frame of no bytes and the CRC of nothing, and the CRC of
 * nothing is 0x0000 - which is why the packet is three constants and not a
 * computed one.
 *
 * A LENGTH OF 0 WOULD BE THE OBVIOUS WRONG ANSWER, and it is worth writing
 * down why it is wrong: axtcp_rx() rejects a length below 2 in the same breath
 * in which it checks the CRC, so 00 00 00 00 is read as a frame that cannot
 * be and tears the session down.  A keepalive that killed the connection it
 * was meant to hold would be found out in production and not in a test.
 *
 * ZERO BYTES OF TCP WRITE ARE NOT AN OPTION: a write of length 0 says nothing
 * to the kernel, produces no segment, and never reaches the NAT box that is
 * the whole point of the exercise.
 */
int
axtcp_keepalive(
struct tcpsock *tp,
unsigned char *out,
int max)
{
  (void) tp;
  if (max < 4)
    return -1;
  out[0] = 0x00;
  out[1] = 0x02;		/* two bytes of payload: 0 frame + 2 FCS */
  out[2] = 0x00;		/* the CRC of nothing */
  out[3] = 0x00;
  return 4;
}

/*---------------------------------------------------------------------------*/

/* THE FIRST BYTE DECIDES.  0xc0 is FEND and therefore KISS - and because 0xc0
 * as a length would be 49152, it cannot be anything else here.  Everything
 * else is an AXTCP candidate, and what confirms it is not the first byte but
 * the first CRC that comes out right.
 */
int
axtcp_detect(
struct tcpsock *tp)
{
  if (tp->len < 1)
    return 0;			/* nothing there yet: keep waiting */
  if (tp->buf[0] == FR_END)
    return -1;			/* that is KISS */
  tp->proto = TCPAD_AXTCP;
  return 1;
}

/* attach axtcp [<label> [listen [<port>] | client <host>[:<port>]]]
 *              [type=kiss|axtcp|auto] [bind=<addr>[,<addr>]...]
 *              [keepalive <seconds>] [shared-key <code>]
 *
 * THE SUPERSET.  "attach axtcp" takes both dialects unless "type=" pins one,
 * and the body is shared with "attach kisstcp" (tcpsock_attach(), tcpsock.c) -
 * the framing is all this file keeps.  See there for how "type=" and the two
 * ports are worked out.
 */
int
axtcp_attach(
int argc,
char *argv[],
void *p)
{
  return tcpsock_attach(argc, argv, p, "axtcp", TCPAD_AUTO);
}