/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.c,v 1.6 1992/05/14 13:19:43 deyke Exp $ */

/* Generic serial line interface routines
 * Copyright 1992 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "slhc.h"
#include "8250.h"
#include "asy.h"
#include "ax25.h"
#include "kiss.h"
#include "nrs.h"
#include "pktdrvr.h"
#include "slip.h"
/* #include "ppp.h" */
#include "commands.h"

/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4"
 * argv[3]: mode, may be:
 *              "slip" (point-to-point SLIP)
 *              "ax25" (AX.25 frame format in SLIP for raw TNC)
 *              "nrs" (NET/ROM format serial protocol)
 *              "ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags,
 *              'v' for Van Jacobson TCP header compression (SLIP only,
 *                  use ppp command for VJ compression with PPP);
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	struct asy *asyp;
	char *ifn;
	int dev;
	int xdev;
	int trigchar = -1;
	char monitor = FALSE;
#if     defined(SLIP) || defined(AX25)
	struct slip *sp;
#endif
#ifdef  NRS
	struct nrs *np;
#endif

	if(if_lookup(argv[4]) != NULLIF){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	/* Find unused asy control block */
	for(dev=0;dev < ASY_MAX;dev++){
		asyp = &Asy[dev];
		if(asyp->iface == NULLIF)
			break;
	}
	if(dev >= ASY_MAX){
		printf("Too many asynch controllers\n");
		return -1;
	}

	/* Create interface structure and fill in details */
	ifp = (struct iface *)callocw(1,sizeof(struct iface));
	ifp->addr = Ip_addr;
	ifp->name = strdup(argv[4]);
	ifp->mtu = atoi(argv[6]);
	ifp->dev = dev;
	ifp->stop = asy_stop;

#ifdef  SLIP
	if(stricmp(argv[3],"SLIP") == 0) {
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			sp = &Slip[xdev];
			if(sp->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			printf("Too many slip devices\n");
			return -1;
		}
		setencap(ifp,"SLIP");
		ifp->ioctl = asy_ioctl;
		ifp->raw = slip_raw;
		ifp->show = slip_status;
		ifp->flags = 0;
		ifp->xdev = xdev;

		sp->iface = ifp;
		sp->send = asy_send;
		sp->get = get_asy;
		sp->type = CL_SERIAL_LINE;
		trigchar = FR_END;
#ifdef VJCOMPRESS
		if((argc > 8) && (strchr(argv[8],'v') != NULLCHAR)) {
			sp->escaped |= SLIP_VJCOMPR;
			sp->slcomp = slhc_init(16,16);
		}
#else
		sp->slcomp = NULL;
#endif  /* VJCOMPRESS */
		ifp->rxproc = asy_rx;
	} else
#endif
#ifdef  AX25
	if(stricmp(argv[3],"AX25") == 0) {
		/* Set up a SLIP link to use AX.25 */
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			sp = &Slip[xdev];
			if(sp->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			printf("Too many slip devices\n");
			return -1;
		}
		setencap(ifp,"AX25");
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
		trigchar = FR_END;
		ifp->rxproc = asy_rx;
	} else
#endif
#ifdef  NRS
	if(stricmp(argv[3],"NRS") == 0) {
		/* Set up a net/rom serial iface */
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			np = &Nrs[xdev];
			if(np->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			printf("Too many nrs devices\n");
			return -1;
		}
		/* no call supplied? */
		setencap(ifp,"AX25");
		ifp->ioctl = asy_ioctl;
		ifp->raw = nrs_raw;

		ifp->hwaddr = mallocw(AXALEN);
		memcpy(ifp->hwaddr,Mycall,AXALEN);
		ifp->xdev = xdev;
		np->iface = ifp;
		np->send = asy_send;
		np->get = get_asy;
		trigchar = ETX;
		ifp->rxproc = nrs_recv;
	} else
#endif
#ifdef  PPP
	if(stricmp(argv[3],"PPP") == 0) {
		/* Setup for Point-to-Point Protocol */
		trigchar = HDLC_FLAG;
		monitor = TRUE;
		setencap(ifp,"PPP");
		ifp->ioctl = asy_ioctl;
		ifp->flags = FALSE;

		/* Initialize parameters for various PPP phases/protocols */
		if (ppp_init(ifp) != 0) {
			printf("Cannot allocate PPP control block\n");
			free(ifp->name);
			free((char *)ifp);
			return -1;
		}
	} else
#endif
	{
		printf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		free(ifp->name);
		free(ifp);
		return -1;
	}

	/* Link in the interface */
	ifp->next = Ifaces;
	Ifaces = ifp;

	asy_init(dev,ifp,argv[1],argv[2],(int16)atol(argv[5]),
		trigchar,monitor,(int16)atol(argv[7]));
	return 0;
}
