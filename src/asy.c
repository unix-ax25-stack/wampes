/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.c,v 1.3 1991/03/28 19:39:02 deyke Exp $ */

/* Generic serial line interface routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "asy.h"
#include "8250.h"
#include "ax25.h"
#include "kiss.h"
/* #include "ppp.h" */
#include "slip.h"
#include "nrs.h"
#include "config.h"
/* #include "proc.h" */
#include "commands.h"
/* #include "slcomp.h" */

/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4"
 * argv[3]: mode, may be:
 *          "slip" (point-to-point SLIP)
 *          "ax25" (AX.25 frame format in SLIP for raw TNC)
 *          "nrs" (NET/ROM format serial protocol)
 *          "ppp" (Point-to-Point Protocol, RFC1171, RFC1172)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags, e.g., 'c' for cts flow control;
 *          'v' for Van Jacobson TCP header compression (SLIP only,
 *          use 'ppp ipcomp' command for VJ compression with PPP);
 *          'r' for RLSD (RS232 pin 8, CD) physical link up/down
 *          indicator (PPP only); 'a' for autobaud message handling
 *          (PPP only)
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_asy;
	int dev;
	int mode;
	int xdev;
	int trigchar = -1;
	char cts;
	struct asy *asyp;
	char rlsd = 0;
	unsigned long autospeed = 0L;
#if     defined(SLIP) || defined(AX25) || defined(PPP)
	struct slip *sp;
#endif
#ifdef  NRS
	struct nrs *np;
#endif

	/* Find unused asy control block */
	for(dev=0;dev < ASY_MAX;dev++){
		asyp = &Asy[dev];
		if(asyp->iface == NULLIF)
			break;
	}
	if(dev >= ASY_MAX){
		tprintf("Too many asynch controllers\n");
		return -1;
	}
	if(if_lookup(argv[4]) != NULLIF){
		tprintf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(strcmp(argv[3],"slip") == 0)
		mode = SLIP_MODE;
	else if(strcmp(argv[3],"ax25") == 0)
		mode = AX25_MODE;
	else if(strcmp(argv[3],"nrs") == 0)
		mode = NRS_MODE;
	else if(strcmp(argv[3],"ppp") == 0)
		mode = PPP_MODE;
	else
		mode = UNKNOWN_MODE;

	/* Create interface structure and fill in details */
	if_asy = (struct iface *)callocw(1,sizeof(struct iface));
	if_asy->addr = Ip_addr;
	if_asy->name = strdup(argv[4]);
	if_asy->mtu = atoi(argv[6]);
	if_asy->dev = dev;
	if_asy->stop = asy_stop;

	switch(mode){
#ifdef  SLIP
	case SLIP_MODE:
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			sp = &Slip[xdev];
			if(sp->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			tprintf("Too many slip devices\n");
			return -1;
		}
		setencap(if_asy,"SLIP");
		if_asy->ioctl = asy_ioctl;
		if_asy->raw = slip_raw;
		if_asy->flags = 0;
		if_asy->xdev = xdev;

		sp->iface = if_asy;
		sp->send = asy_send;
		sp->get = get_asy;
		sp->type = CL_SERIAL_LINE;
		trigchar = FR_END;
#ifdef VJCOMPRESS
		if((argc > 8) && (strchr(argv[8],'v') != NULLCHAR)) {
			sp->escaped |= SLIP_VJCOMPR;
			sp->slcomp = (struct slcompress *)mallocw(sizeof(struct slcompress));
			sl_compress_init(sp->slcomp);
		}
#else
/*              sp->slcomp = 0L; */
#endif  /* VJCOMPRESS */
		if_asy->rxproc = asy_rx;
		break;
#endif
#ifdef  AX25
	case AX25_MODE:  /* Set up a SLIP link to use AX.25 */
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			sp = &Slip[xdev];
			if(sp->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			tprintf("Too many slip devices\n");
			return -1;
		}
		setencap(if_asy,"AX25");
		if_asy->ioctl = kiss_ioctl;
		if_asy->raw = kiss_raw;
		if(if_asy->hwaddr == NULLCHAR)
			if_asy->hwaddr = mallocw(AXALEN);
		memcpy(if_asy->hwaddr,Mycall,AXALEN);
		if_asy->xdev = xdev;

		sp->iface = if_asy;
		sp->send = asy_send;
		sp->get = get_asy;
		sp->type = CL_KISS;
		trigchar = FR_END;
		if_asy->rxproc = asy_rx;
		break;
#endif
#ifdef  NRS
	case NRS_MODE: /* Set up a net/rom serial iface */
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			np = &Nrs[xdev];
			if(np->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			tprintf("Too many nrs devices\n");
			return -1;
		}
		/* no call supplied? */
		setencap(if_asy,"AX25");
		if_asy->ioctl = asy_ioctl;
		if_asy->raw = nrs_raw;
		if_asy->hwaddr = mallocw(AXALEN);
		memcpy(if_asy->hwaddr,Mycall,AXALEN);
		if_asy->xdev = xdev;
		np->iface = if_asy;
		np->send = asy_send;
		np->get = get_asy;
		trigchar = ETX;
		if_asy->rxproc = nrs_recv;
		break;
#endif
#ifdef  PPP
	case PPP_MODE:  /* Setup for Point-to-Point Protocol */
		for(xdev = 0;xdev < SLIP_MAX;xdev++){
			sp = &Slip[xdev];
			if(sp->iface == NULLIF)
				break;
		}
		if(xdev >= SLIP_MAX) {
			tprintf("Too many slip/ppp devices\n");
			return -1;
		}
		setencap(if_asy,"PPP");
		if_asy->ioctl = asy_ioctl;
		if_asy->raw = ppp_raw;
		if_asy->flags = 0;
		if_asy->xdev = xdev;

		sp->iface = if_asy;
		sp->send = asy_send;
		sp->get = get_asy;
		sp->type = CL_PPP;
		sp->get_rlsd = get_rlsd_asy;
		sp->escaped = 0;
		trigchar = HDLC_FLAG;
		/* Allocate PPP control structure */
		sp->pppio = (struct pppctl *)calloc(1,sizeof(struct pppctl));
		if (sp->pppio == NULLPPPCTL) {
			tprintf("Cannot allocate PPP control block\n");
			free(if_asy->name);
			free((char *)if_asy);
			return -1;
		}
		/* Initialize parameters for various PPP phases/protocols */
		if((argc > 8) && (strchr(argv[8],'r') != NULLCHAR))
			rlsd = 1;
		else
			rlsd = 0;
		if((argc > 8) && (strchr(argv[8],'a') != NULLCHAR)) {
			autospeed = atoi(argv[7]);
		} else {
			autospeed = 0;
		}
		/* Initialize parameters for various PPP phases/protocols */
		if (ppp_init(xdev,rlsd,autospeed) == -1) {
			free(if_asy->name);
			free((char *)if_asy);
			return -1;
		}
		if (autospeed != 0) {
			if_asy->rxproc = newproc("ppp autobaud", 256,
			 ppp_autobaud, xdev, NULL, NULL,0);
		} else {
			if_asy->rxproc = newproc("ppp recv", 256,
			 ppp_recv, xdev, NULL, NULL,0);
		}
		if (rlsd)
			if_asy->txproc = newproc("ppp rlsd", 256, ppp_rlsd,
			 xdev, (void *)autospeed, NULL,0);
		break;
#endif /* PPP */
	default:
		tprintf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		free(if_asy->name);
		free((char *)if_asy);
		return -1;
	}
	if_asy->next = Ifaces;
	Ifaces = if_asy;
	if((argc > 8) && (strchr(argv[8],'c') != NULLCHAR))
		cts = 1;
	else
		cts = 0;
	asy_init(dev,if_asy,argv[1],argv[2],(unsigned)atoi(argv[5]),trigchar,cts,rlsd);
	asy_speed(dev,atol(argv[7]),autospeed);
	return 0;
}
