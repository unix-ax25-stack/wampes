/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.c,v 1.6 1991/04/17 19:47:37 deyke Exp $ */

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

/* Send raw data packet on KISS TNC */
int
kiss_raw(iface,data)
struct iface *iface;
struct mbuf *data;
{
	register struct mbuf *bp;

	/* Put type field for KISS TNC on front */
	if((bp = pushdown(data,1)) == NULLBUF){
		free_p(data);
		return -1;
	}
	bp->data[0] = PARAM_DATA;
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
	int rval = val;

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
