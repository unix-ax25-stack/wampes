/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.c,v 1.4 1991/02/24 20:17:05 deyke Exp $ */

/* Routines for AX.25 encapsulation in KISS TNC
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "kiss.h"
#include "slip.h"
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
	bp->data[0] = KISS_DATA;
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
	case KISS_DATA:
		ax_recv(iface,bp);
		break;
	default:
		free_p(bp);
		break;
	}
}
/* Perform device control on KISS TNC by sending control messages */
int
kiss_ioctl(iface,argc,argv)
struct iface *iface;
int argc;
char *argv[];
{
	struct mbuf *hbp;
	int i;
	char *cp;

	if(argc < 1){
		tprintf("Data field missing\n");
		return 1;
	}
	/* Allocate space for arg bytes */
	if((hbp = alloc_mbuf((int16)argc)) == NULLBUF){
		free_p(hbp);
		return 0;
	}
	hbp->cnt = argc;
	hbp->next = NULLBUF;
	for(i=0,cp = hbp->data;i < argc;)
		*cp++ = atoi(argv[i++]);

	slip_raw(iface,hbp);    /* Even more "raw" than kiss_raw */
	return 0;
}
