/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.c,v 1.8 1992/06/01 10:34:09 deyke Exp $ */

/* Generic serial line interface routines
 * Copyright 1992 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "slhc.h"
#include "n8250.h"
#include "asy.h"
#include "ax25.h"
#include "kiss.h"
#include "nrs.h"
#include "pktdrvr.h"
#include "slip.h"
/* #include "ppp.h" */
#include "commands.h"

static int asy_detach __ARGS((struct iface *ifp));

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
 *              'c' for cts flow control
 *              'r' for rlsd (cd) detection
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	int dev;
	int trigchar = -1;
	int cts,rlsd;
	struct asymode *ap;
	int vj;
	char *cp;

	if(if_lookup(argv[4]) != NULLIF){
		printf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	/* Find unused asy control block */
	for(dev=0;dev < ASY_MAX;dev++){
		if(Asy[dev].iface == NULLIF)
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
	ifp->stop = asy_detach;
	if(argc > 8 && strchr(argv[8],'v') != NULLCHAR)
		vj = 1;
	else
		vj = 0;

	/* Look for the interface mode in the table */
	for(ap = Asymode;ap->name != NULLCHAR;ap++){
		if(stricmp(argv[3],ap->name) == 0){
			trigchar = uchar(ap->trigchar);
			if((*ap->init)(ifp,vj) != 0){
				printf("%s: mode %s Init failed\n",
				 ifp->name,argv[3]);
				if_detach(ifp);
				return -1;
			}
			break;
		}
	}
	if(ap->name == NULLCHAR){
		printf("Mode %s unknown for interface %s\n",
		 argv[3],argv[4]);
		if_detach(ifp);
		return -1;
	}
	/* Link in the interface */
	ifp->next = Ifaces;
	Ifaces = ifp;

	cts = rlsd = 0;
	if(argc > 8){
		if(strchr(argv[8],'c') != NULLCHAR)
			cts = 1;
		if(strchr(argv[8],'r') != NULLCHAR)
			rlsd = 1;
	}
	asy_init(dev,ifp,argv[1],argv[2],(int16)atol(argv[5]),
		trigchar,(int16)atol(argv[7]),cts,rlsd);
#if 0
	cp = if_name(ifp," tx");
	ifp->txproc = newproc(cp,768,if_tx,0,ifp,NULL,0);
	free(cp);
#endif
	return 0;
}

static int
asy_detach(ifp)
struct iface *ifp;
{
	struct asymode *ap;

	if(ifp == NULLIF)
		return -1;
	asy_stop(ifp);

	/* Call mode-dependent routine */
	for(ap = Asymode;ap->name != NULLCHAR;ap++){
		if(ifp->iftype != NULLIFT
		 && stricmp(ifp->iftype->name,ap->name) == 0
		 && ap->free != NULLFP){
			(*ap->free)(ifp);
		}
	}
	return 0;
}

