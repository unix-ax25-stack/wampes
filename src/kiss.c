/* @(#) $Id: kiss.c,v 1.21 1999/02/01 22:24:25 deyke Exp $ */

/* Routines for AX.25 encapsulation in KISS TNC
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "kiss.h"
#include "devparam.h"
#include "slip.h"
#include "asy.h"
#include "ax25.h"
#include "pktdrvr.h"
#include "crc.h"
#include "lapb.h"

/* Set up a SLIP link to use AX.25 */
int
kiss_init(struct iface *ifp)
{
	int xdev;
	struct slip *sp;

	/* The encapsulation word on the attach line picks two things out of
	 * two tables: the framing, which lands here, and the interface type,
	 * which says how what comes back is read.  For ax25ui and ax25i the
	 * two disagree.  Frames go out properly KISS encoded - kiss_raw()
	 * below sees to that - but incoming ones reach ax_recv() with the
	 * KISS type byte still in front, and it throws away every single one.
	 * The port then transmits and never hears an answer.  Nothing is
	 * wired that way on purpose, so refuse instead of running half a port.
	 */
	if(ifp->iftype == NULL || ifp->iftype->rcvf != kiss_recv){
		printf("%s: %s sends KISS frames but cannot receive them - use kissui or kissi\n",
		 ifp->name,
		 ifp->iftype != NULL ? ifp->iftype->name : "this encapsulation");
		return -1;
	}

	for(xdev = 0;xdev < SLIP_MAX;xdev++){
		sp = &Slip[xdev];
		if(sp->iface == NULL)
			break;
	}
	if(xdev >= SLIP_MAX) {
		printf("Too many slip devices\n");
		return -1;
	}
	ifp->ioctl = kiss_ioctl;
	ifp->raw = kiss_raw;
	ifp->show = slip_status;

	if(ifp->hwaddr == NULL)
		ifp->hwaddr = (uint8 *) mallocw(AXALEN);
	memcpy(ifp->hwaddr,Mycall,AXALEN);
	/* The port answers to this callsign now - see axlisten_drop_local(). */
	axlisten_drop_local(ifp->hwaddr);
	ifp->xdev = xdev;
	ifp->crccontrol = CRC_TEST_16;

	sp->iface = ifp;
	sp->send = asy_send;
	sp->get = get_asy;
	sp->type = CL_KISS;
	ifp->rxproc = slip_rx;
	return 0;
}
int
kiss_free(struct iface *ifp)
{
	if(Slip[ifp->xdev].iface == ifp)
		Slip[ifp->xdev].iface = NULL;
	return 0;
}
/* Send raw data packet on KISS TNC */
int
kiss_raw(
struct iface *iface,
struct mbuf **bpp
){
	/* Put type field for KISS TNC on front */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = PARAM_DATA;
	switch (iface->crccontrol){
	case CRC_TEST_16:
		iface->crccontrol = CRC_TEST_RMNC;
	case CRC_16:
		(*bpp)->data[0] |= 0x80;
		append_crc_16(*bpp);
		break;
	case CRC_TEST_RMNC:
		iface->crccontrol = CRC_OFF;
	case CRC_RMNC:
		(*bpp)->data[0] |= 0x20;
		append_crc_rmnc(*bpp);
		break;
	}
	/* slip_raw also increments sndrawcnt */
	slip_raw(iface,bpp);
	return 0;
}

/* Process incoming KISS TNC frame */
void
kiss_recv(
struct iface *iface,
struct mbuf **bpp
){
	char kisstype;
	struct mbuf *bp = *bpp;

	/* The type byte below is read straight from bp->data, so make sure
	 * there is one.  slip_decode() never hands us an empty frame today,
	 * but nothing here relies on that staying true.
	 */
	if(bp == NULL || bp->cnt == 0){
		free_p(bpp);
		return;
	}
	/* THE CRC DECISION, and the type byte below has three incompatible readers
	 * in the wild.  They disagree, and the disagreement is the entire reason
	 * multiport is not supported on this path yet, so it is worth writing down
	 * before anyone tries to add it.
	 *
	 * THE PARAMETERS, each verified against the published check value for the
	 * string "123456789":
	 *
	 *   CRC_16    SMACK   poly 0x8005 reflected, init 0x0000, no final xor,
	 *                    two bytes, LOW byte first, residue 0x0000.
	 *                    Check value 0xbb3d - this is CRC-16/ARC.
	 *   CRC_RMNC  RMNC    poly 0x1021 reflected, init 0xffff, no final xor,
	 *                    two bytes, HIGH byte first, residue 0x7070.
	 *                    Check value 0x9fb5.
	 *   CRC_CCITT         poly 0x1021 reflected, init 0xffff, final xor
	 *                    0xffff, two bytes, LOW byte first, residue 0xf0b8.
	 *                    Check value 0x906e - this is CRC-16/X-25, the AX.25
	 *                    FCS.  It is unreachable from here; see crccontrol in
	 *                    kisstcp.c for why it has no KISS existence at all.
	 *
	 * Note the shape of that: RMNC and the AX.25 FCS share BOTH polynomial and
	 * init and differ only in the final xor and the residue, while SMACK
	 * differs from both in the polynomial.  So "the KISS CRC" is not one
	 * family with three sizes - it is two polynomials, and the 0x1021 one is
	 * reached with two different sets of parameters.  The Crc_*_table in
	 * crc.c are the canonical tables for exactly the parameters above, and
	 * Crc_rmnc_table is byte for byte the same array as the one in tnn's
	 * os/linux/l1linux.c and the one mkiss builds as crctab in ax25-tools.
	 *
	 * WHERE THE FLAG LIVES - the three answers:
	 *
	 *   SMACK (symek.de/g/smack.html) puts the flag in bit 7 and keeps the
	 *   port in bits 6..4, so both fit into the one byte.  tnn spells that out
	 *   literally in its receive state machine: (ch & 0x8F) == 0x80, and then
	 *   rx_port = (ch & 0x70) >> 4.  SMACK and multiport therefore coexist by
	 *   construction - 0x90 is port 1 with a checksum.
	 *
	 *   RMNC consumes the WHOLE type byte.  tnn tests (ch & 0xFF) == 0x20 and
	 *   hardwires rx_port = 0; mkiss sends CRCTYP 0x20 for the same dialect.
	 *   There is no port field to collide with, so RMNC and multiport exclude
	 *   each other by construction, not by accident.
	 *
	 *   The third answer dodges the collision instead of solving it: mkiss
	 *   composes the type byte as (cmd & 0x0F) | (port << 4) and negotiates
	 *   the CRC mode out of band, so there is no flag bit to sit next to a
	 *   port bit.  That is the same layout the KISS TCP dialect uses (Dire
	 *   Wolf: chan = (kiss_msg[0] >> 4) & 0xf), and it is why "crc == off" is
	 *   the right precondition for multiport here: of the KISS CRCs, only the
	 *   ones that push their flag INTO the port field would cost us the
	 *   channel, and RMNC costs it even when it is switched off.
	 *
	 * ONE DEFECT VISIBLE FROM HERE, not fixed because it needs a decision
	 * first: the two tests below run before the command nibble is looked at.
	 * mkiss's G8BPQ mode makes an exception for this - it drops the checksum
	 * for every command except data and ACKREQ, on transmit AND on receive -
	 * so a host may legitimately put bit 7 on a TXDELAY frame.  tnn's SMACK
	 * mode makes no such exception and would reject the same frame.  The two
	 * real implementations thus disagree, which means a checksummed
	 * non-data frame has no defined behaviour on this path today: we would
	 * check it against a checksum that is not there, drop it, and count it in
	 * crcerrors.  The 0x20 test below is loose in the same spirit - it is a
	 * mask, where tnn does an exact comparison - so a plain-KISS multiport
	 * frame for port 2 (type byte 0x20, no CRC at all) would be run through
	 * check_crc_rmnc() here.
	 */
	if(bp && (*bp->data & 0x80)){
		if(check_crc_16(bp)){
			iface->crcerrors++;
			free_p(bpp);
			return;
		}
		if(!iface->crcfixed)
			iface->crccontrol = CRC_16;
	}else if(bp && (*bp->data & 0x20)){
		if(check_crc_rmnc(bp)){
			iface->crcerrors++;
			free_p(bpp);
			return;
		}
		if(!iface->crcfixed)
			iface->crccontrol = CRC_RMNC;
	}
	kisstype = PULLCHAR(bpp);
	switch(kisstype & 0xf){
	case PARAM_DATA:
		ax_recv(iface,bpp);
		break;
	default:
		free_p(bpp);
		break;
	}
}
/* Perform device control on KISS TNC by sending control messages */
int32
kiss_ioctl(
struct iface *iface,
int cmd,
int set,
int32 val
){
	struct mbuf *hbp;
	uint8 *cp;
	int rval = 0;

	/* At present, only certain parameters are supported by
	 * stock KISS TNCs. As additional params are implemented,
	 * this will have to be edited
	 */
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
		*cp++ = cmd;
		*cp = (unsigned char) val;
		hbp->cnt = 2;
		slip_raw(iface,&hbp);   /* Even more "raw" than kiss_raw */
		rval = (int) val;       /* per Jay Maynard -- mce */
		break;
	case PARAM_SPEED:       /* These go to the local asy driver */
	case PARAM_DTR:
	case PARAM_RTS:
	case PARAM_DOWN:
	case PARAM_UP:
		rval = (int) asy_ioctl(iface,cmd,set,val);
		break;
	default:                /* Not implemented */
		rval = -1;
		break;
	}
	return rval;
}
