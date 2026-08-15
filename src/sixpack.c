/* 6PACK: an AX.25 host-to-TNC protocol on a serial line.
 *
 * Copyright 2026 Thomas Osterried, DL9SAU.  Distributed under the GNU General
 * Public License; see http://www.fsf.org/.
 *
 * Not KISS with something added - a protocol of its own, by the people who
 * later gave KISS its FlexNet checksum, and it shows: what KISS had to bolt
 * on, 6pack has by construction.  A checksum over every frame, a TNC address
 * in every command byte so that eight of them can share a line, and the
 * carrier state reported rather than guessed.
 *
 * Written from the protocol as described, not ported: the reference
 * implementations are a Linux line discipline and a multiplexer over
 * pseudo-terminals, and neither shape fits a node with mbufs and a
 * cooperative scheduler.  What follows the description exactly is the wire.
 *
 * The wire.  A data byte carries six bits and is known by its top two being
 * clear; three plaintext bytes therefore take four bytes to send:
 *
 *      SEOF|addr   <4/3 of the data, then the checksum>   SEOF
 *
 * The leading SEOF carries the address of the TNC being addressed, the
 * trailing one does not - the frame is already spoken for.  The checksum is
 * chosen so that the data bytes plus the address sum to 0xFF.
 *
 * The first byte of the data field is the transmit delay, not payload; the
 * far end strips it again.  And every outgoing frame is announced to the TNC
 * with a TX_1 before it, which is how the two keep count of what is in
 * flight.
 *
 * Anything that is not a data byte is a one-byte command, and a command may
 * arrive IN THE MIDDLE OF A DATA FRAME.  That is the one thing here that
 * catches people out: the decoder must answer it without touching the frame
 * it is halfway through assembling.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "asy.h"
#include "trace.h"
#include "sixpack.h"

/* Shortest data frame that can mean anything: delay, one byte, checksum. */
#define SIXP_MIN_FRAME          3

#define SIXP_TXDELAY_DEFAULT    25      /* in units of 10 ms, as KISS counts */

struct sixpack Sixpack[ASY_MAX];

static struct mbuf *sixpack_decode(struct sixpack *sp, uint8 c);
static void sixpack_command(struct sixpack *sp, uint8 c);
static struct mbuf *sixpack_encode(struct sixpack *sp, struct mbuf **bpp);

/*---------------------------------------------------------------------------*/

int sixpack_init(struct iface *ifp)
{
	int xdev;
	struct sixpack *sp;

	for (xdev = 0; xdev < ASY_MAX; xdev++) {
		sp = &Sixpack[xdev];
		if (sp->iface == NULL)
			break;
	}
	if (xdev >= ASY_MAX) {
		printf("Too many 6pack devices\n");
		return -1;
	}

	memset(sp, 0, sizeof(*sp));
	ifp->ioctl = asy_ioctl;
	ifp->raw = sixpack_raw;
	ifp->hwaddr = (uint8 *) mallocw(AXALEN);
	memcpy(ifp->hwaddr, Mycall, AXALEN);
	ifp->xdev = xdev;
	ifp->rxproc = sixpack_recv;
	sp->iface = ifp;
	sp->send = asy_send;
	sp->get = get_asy;
	sp->txdelay = SIXP_TXDELAY_DEFAULT;
	return 0;
}

int sixpack_free(struct iface *ifp)
{
	struct sixpack *sp = &Sixpack[ifp->xdev];

	if (sp->iface == ifp) {
		free_p(&sp->rbp);
		sp->iface = NULL;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Send one frame.  What arrives here is the bare AX.25 frame; the transmit
 * delay in front of it belongs to this layer, as the command byte does in
 * KISS.
 */

int sixpack_raw(struct iface *iface, struct mbuf **bpp)
{
	struct mbuf *bp;
	struct sixpack *sp = &Sixpack[iface->xdev];

	dump(iface, IF_TRACE_OUT, *bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();

	/* The encoder consumes the chain, as nrs_encode() does - pullup()
	 * frees the buffers while it reads them, so there is nothing left
	 * here to free and freeing it anyway hands the same buffers back
	 * twice.
	 */
	if ((bp = sixpack_encode(sp, bpp)) == NULL)
		return -1;
	return (*sp->send)(iface->dev, &bp);
}

/* Three bytes in, four out, six bits each.  The checksum rides along in the
 * same encoding and is counted so that data plus address make 0xFF.
 */

static struct mbuf *sixpack_encode(struct sixpack *sp, struct mbuf **bpp)
{
	int i;
	int len;
	struct mbuf *out;
	uint8 *cp;
	uint8 checksum;
	uint8 in[SIXP_MAX_FRAME];
	uint8 part;

	/* The delay byte first, then the frame - and no more than we can hold,
	 * since a truncated frame is worse than a refused one.
	 */
	if ((len = (int) len_p(*bpp)) > (int) sizeof(in) - 2) {
		free_p(bpp);
		return NULL;
	}
	in[0] = sp->txdelay;
	pullup(bpp, in + 1, (uint) len);
	free_p(bpp);                    /* pullup emptied it; make it plain */
	len++;

	/* Four bytes for every three, one more group for the checksum, plus
	 * the announcement and the two SEOFs.
	 */
	if ((out = alloc_mbuf((uint) (4 * (len + 3) / 3 + 8))) == NULL)
		return NULL;
	cp = out->data;

	/* Tell the TNC a frame is coming before sending it. */
	*cp++ = SIXP_MAKE_CMD(SIXP_CMD_TX_1, sp->addr);
	*cp++ = SIXP_MAKE_CMD(SIXP_CMD_SEOF, sp->addr);

	checksum = 0;
	part = 0;
	for (i = 0; i < len; i++) {
		switch (i % 3) {
		case 0:
			*cp++ = in[i] & 0x3F;
			part = (in[i] & 0xC0) >> 2;
			break;
		case 1:
			*cp++ = part | (in[i] & 0x0F);
			part = (in[i] & 0xF0) >> 2;
			break;
		default:
			*cp++ = part | (in[i] & 0x03);
			*cp++ = (in[i] & 0xFC) >> 2;
			part = 0;
			break;
		}
		checksum += in[i];
	}
	checksum += sp->addr;
	checksum = SIXP_CHKSUM - checksum;

	switch (i % 3) {
	case 0:
		*cp++ = checksum & 0x3F;
		*cp++ = (checksum & 0xC0) >> 2;
		break;
	case 1:
		*cp++ = part | (checksum & 0x0F);
		*cp++ = (checksum & 0xF0) >> 2;
		break;
	default:
		*cp++ = part | (checksum & 0x03);
		*cp++ = (checksum & 0xFC) >> 2;
		break;
	}

	/* The closing SEOF names no address: the frame is already spoken for
	 * by the one that opened it.
	 */
	*cp++ = SIXP_MAKE_CMD(SIXP_CMD_SEOF, 0);

	out->cnt = (uint) (cp - out->data);
	return out;
}

/*---------------------------------------------------------------------------*/

/* A one-byte command.  It may turn up in the middle of a data frame, so
 * nothing here may touch the frame being assembled.
 */

static void sixpack_command(struct sixpack *sp, uint8 c)
{
	switch (SIXP_CMD(c)) {
	case SIXP_CMD_DCD:
		/* The carrier state, told rather than guessed - which is one
		 * of the reasons this protocol exists.
		 */
		sp->dcd = (c & 0x08) != 0;
		break;
	case SIXP_CMD_TX_ORUN:
	case SIXP_CMD_RX_ORUN:
	case SIXP_CMD_RX_BUF_OVL:
		/* The TNC lost something.  Counted, not reported: on a busy
		 * channel this is noise, and the count is what an operator
		 * needs to see a trend.
		 */
		sp->overruns++;
		break;
	case SIXP_CMD_RX_1:
	case SIXP_CMD_TX_1:
	case SIXP_CMD_LED:
	case SIXP_CMD_CAL:
	case SIXP_CMD_ADDR:
		break;
	default:
		/* Not an error - an extension we do not speak.  The PR430
		 * carries transceiver control over this same line with
		 * command bytes of its own, and counting those as faults
		 * would make a perfectly good TNC look broken.  Ignoring
		 * them is also what lets such a TNC be used for plain
		 * packet by a node that knows nothing of its extras.
		 */
		sp->unknown++;
		break;
	}
}

/* One byte in; a finished frame out, or nothing yet. */

static struct mbuf *sixpack_decode(struct sixpack *sp, uint8 c)
{
	struct mbuf *bp;
	uint8 checksum;
	int i;
	int len;

	if (SIXP_IS_DATA(c)) {
		/* Four encoded bytes make three plain ones.  Anything beyond
		 * the buffer is dropped here rather than at the far end: a
		 * frame that long is not one of ours.
		 */
		if (sp->rcnt >= sizeof(sp->rbuf)) {
			sp->inframe = 0;
			return NULL;
		}
		switch (sp->cnt++ % 4) {
		case 0:
			sp->part = c & 0x3F;
			break;
		case 1:
			sp->rbuf[sp->rcnt++] = sp->part | ((c & 0x30) << 2);
			sp->part = c & 0x0F;
			break;
		case 2:
			sp->rbuf[sp->rcnt++] = sp->part | ((c & 0x3C) << 2);
			sp->part = c & 0x03;
			break;
		default:
			sp->rbuf[sp->rcnt++] = sp->part | ((c & 0x3F) << 2);
			break;
		}
		return NULL;
	}

	if (SIXP_CMD(c) != SIXP_CMD_SEOF) {
		sixpack_command(sp, c);
		return NULL;
	}

	/* A SEOF: it closes whatever was open and opens what comes next.  The
	 * address of the frame just closed came from the SEOF that began it.
	 */
	bp = NULL;
	len = (int) sp->rcnt;
	if (sp->inframe && len > 0) {
		if (len < SIXP_MIN_FRAME) {
			sp->errors++;
		} else {
			checksum = 0;
			for (i = 0; i < len; i++)
				checksum += sp->rbuf[i];
			checksum += SIXP_ADDR(sp->seof);
			if (checksum != SIXP_CHKSUM) {
				sp->errors++;
			} else {
				/* Drop the checksum, and the transmit delay
				 * in front, which is ours and not the
				 * caller's.
				 */
				len -= 2;
				if ((bp = qdata((char *) sp->rbuf + 1,
						(uint) len)) != NULL)
					sp->packets++;
			}
		}
	}
	sp->seof = c;
	sp->inframe = 1;
	sp->cnt = 0;
	sp->rcnt = 0;
	sp->part = 0;
	return bp;
}

/*---------------------------------------------------------------------------*/

void sixpack_recv(void *arg)
{
	int cnt;
	struct iface *iface = (struct iface *) arg;
	struct mbuf *bp;
	struct sixpack *sp = &Sixpack[iface->xdev];
	uint8 buf[4096];
	uint8 *cp;

	cnt = (*sp->get)(iface->dev, cp = buf, sizeof(buf));
	while (--cnt >= 0) {
		if ((bp = sixpack_decode(sp, *cp++)) == NULL)
			continue;
		if (iface->trace & IF_TRACE_RAW)
			raw_dump(iface, IF_TRACE_IN, bp);
		ax_recv(iface, &bp);
	}
}

/*---------------------------------------------------------------------------*/

int do6pstat(int argc, char *argv[], void *p)
{
	int i;
	struct sixpack *sp;

	(void) argc;
	(void) argv;
	(void) p;
	printf("Iface      Addr  DCD  Frames    Errors  Overruns   Unknown\n");
	for (i = 0; i < ASY_MAX; i++) {
		sp = &Sixpack[i];
		if (sp->iface == NULL)
			continue;
		printf("%-10s %4d  %-3s  %6ld  %8ld  %8ld  %8ld\n",
		       sp->iface->name, sp->addr, sp->dcd ? "on" : "off",
		       sp->packets, sp->errors, sp->overruns, sp->unknown);
	}
	return 0;
}
