/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.c,v 1.10 1992/07/24 20:00:25 deyke Exp $ */

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

/* Set up a SLIP link to use AX.25 */
int
kiss_init(ifp,vj)
struct iface *ifp;
int vj; /* Unused */
{
	int xdev;
	struct slip *sp;
	char *ifn;

	for(xdev = 0;xdev < SLIP_MAX;xdev++){
		sp = &Slip[xdev];
		if(sp->iface == NULLIF)
			break;
	}
	if(xdev >= SLIP_MAX) {
		printf("Too many slip devices\n");
		return -1;
	}
	setencap(ifp,"KISS");
	ifp->ioctl = kiss_ioctl;
	ifp->raw = kiss_raw;
	ifp->show = slip_status;

	if(ifp->hwaddr == NULLCHAR)
		ifp->hwaddr = mallocw(AXALEN);
	memcpy(ifp->hwaddr,Mycall,AXALEN);
	ifp->xdev = xdev;

	sp->iface = ifp;
	sp->send = asy_send;
	sp->get = get_asy;
	sp->type = CL_KISS;
#if 0
	ifp->rxproc = newproc( ifn = if_name( ifp, " rx" ),
		256,slip_rx,xdev,NULL,NULL,0);
	free(ifn);
#else
	ifp->rxproc = slip_rx;
#endif
	return 0;
}
int
kiss_free(ifp)
struct iface *ifp;
{
	if(Slip[ifp->xdev].iface == ifp)
		Slip[ifp->xdev].iface = NULLIF;
	return 0;
}
/* Send raw data packet on KISS TNC */
int
kiss_raw(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	/* Put type field for KISS TNC on front */
	bp = pushdown(bp,1);
	bp->data[0] = PARAM_DATA;
	if(iface->crccontrol != CRC_OFF){
		bp->data[0] |= 0x80;
		append_crc(bp);
		if(iface->crccontrol == CRC_TEST)
			iface->crccontrol = CRC_OFF;
	}
	/* slip_raw also increments sndrawcnt */
	slip_raw(iface,bp);
	return 0;
}

/* Process incoming KISS TNC frame */
void
kiss_recv(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	char kisstype;

	if(bp && (*bp->data & 0x80)){
		if(check_crc(bp)){
			iface->crcerrors++;
			free_p(bp);
			return;
		}
		iface->crccontrol = CRC_ON;
	}
	kisstype = PULLCHAR(&bp);
	switch(kisstype & 0xf){
	case PARAM_DATA:
		ax_recv(iface,bp);
		break;
	default:
		free_p(bp);
		break;
	}
}
/* Perform device control on KISS TNC by sending control messages */
int32
kiss_ioctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	struct mbuf *hbp;
	char *cp;
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
		if((hbp = alloc_mbuf(2)) == NULLBUF){
			free_p(hbp);
			rval = -1;
			break;
		}
		cp = hbp->data;
		*cp++ = cmd;
		*cp = val;
		hbp->cnt = 2;
		slip_raw(iface,hbp);    /* Even more "raw" than kiss_raw */
		rval = val;             /* per Jay Maynard -- mce */
		break;
	case PARAM_SPEED:       /* These go to the local asy driver */
	case PARAM_DTR:
	case PARAM_RTS:
		rval = asy_ioctl(iface,cmd,set,val);
		break;
	default:                /* Not implemented */
		rval = -1;
		break;
	}
	return rval;
}
