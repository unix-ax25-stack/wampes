/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iface.c,v 1.17 1993/03/04 23:11:51 deyke Exp $ */

/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "icmp.h"
#include "netuser.h"
#include "ax25.h"
#include "enet.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "commands.h"
#include "trace.h"

static void showiface __ARGS((struct iface *ifp));
static int mask2width __ARGS((int32 mask));
static int ifipaddr __ARGS((int argc,char *argv[],void *p));
static int iflinkadr __ARGS((int argc,char *argv[],void *p));
static int ifbroad __ARGS((int argc,char *argv[],void *p));
static int ifnetmsk __ARGS((int argc,char *argv[],void *p));
static int ifrxbuf __ARGS((int argc,char *argv[],void *p));
static int ifmtu __ARGS((int argc,char *argv[],void *p));
static int ifforw __ARGS((int argc,char *argv[],void *p));
static int ifencap __ARGS((int argc,char *argv[],void *p));
static int iftxqlen __ARGS((int argc,char *argv[],void *p));

/* Interface list header */
struct iface *Ifaces = &Loopback;

/* Loopback pseudo-interface */
struct iface Loopback = {
	&Encap,         /* Link to next entry */
	"loopback",     /* name         */
	0x7f000001L,    /* addr         127.0.0.1 */
	0xffffffffL,    /* broadcast    255.255.255.255 */
	0xffffffffL,    /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* trace        */
	NULLFILE,       /* trfp         */
	NULLIF,         /* forw         */
	NULL,           /* rxproc       */
	NULLPROC,       /* txproc       */
	NULLPROC,       /* supv         */
	NULLBUF,        /* outq         */
	0,              /* outlim       */
	0,              /* txbusy       */
	NULL,           /* dstate       */
	NULL,           /* dtickle      */
	NULL,           /* dstatus      */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULLFP,         /* (*iostatus)  */
	NULLFP,         /* (*stop)      */
	NULLCHAR,       /* hwaddr       */
	NULL,           /* extension    */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	NULLFP,         /* (*send)      */
	NULLFP,         /* (*output)    */
	NULLFP,         /* (*raw)       */
	NULLVFP,        /* (*status)    */
	NULLFP,         /* (*discard)   */
	NULLFP,         /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* crccontrol   */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
	0,              /* flags        */
};
/* Encapsulation pseudo-interface */
struct iface Encap = {
	NULLIF,
	"encap",        /* name         */
	INADDR_ANY,     /* addr         0.0.0.0 */
	0xffffffffL,    /* broadcast    255.255.255.255 */
	0xffffffffL,    /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* trace        */
	NULLFILE,       /* trfp         */
	NULLIF,         /* forw         */
	NULL,           /* rxproc       */
	NULLPROC,       /* txproc       */
	NULLPROC,       /* supv         */
	NULLBUF,        /* outq         */
	0,              /* outlim       */
	0,              /* txbusy       */
	NULL,           /* dstate       */
	NULL,           /* dtickle      */
	NULL,           /* dstatus      */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULLFP,         /* (*iostatus)  */
	NULLFP,         /* (*stop)      */
	NULLCHAR,       /* hwaddr       */
	NULL,           /* extension    */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	ip_encap,       /* (*send)      */
	NULLFP,         /* (*output)    */
	NULLFP,         /* (*raw)       */
	NULLVFP,        /* (*status)    */
	NULLFP,         /* (*discard)   */
	NULLFP,         /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* crccontrol   */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
	0,              /* flags        */
};

char Noipaddr[] = "IP address field missing, and ip address not set\n";

struct cmds Ifcmds[] = {
	"broadcast",            ifbroad,        0,      2,      NULLCHAR,
	"encapsulation",        ifencap,        0,      2,      NULLCHAR,
	"forward",              ifforw,         0,      2,      NULLCHAR,
	"ipaddress",            ifipaddr,       0,      2,      NULLCHAR,
	"linkaddress",          iflinkadr,      0,      2,      NULLCHAR,
	"mtu",                  ifmtu,          0,      2,      NULLCHAR,
	"netmask",              ifnetmsk,       0,      2,      NULLCHAR,
	"txqlen",               iftxqlen,       0,      2,      NULLCHAR,
	"rxbuf",                ifrxbuf,        0,      2,      NULLCHAR,
	NULLCHAR,
};
/*
 * General purpose interface transmit task, one for each device that can
 * send IP datagrams. It waits on the interface's IP output queue (outq),
 * extracts IP datagrams placed there in priority order by ip_route(),
 * and sends them to the device's send routine.
 */
void
if_tx(dev,arg1,unused)
int dev;
void *arg1;
void *unused;
{
	struct mbuf *bp;        /* Buffer to send */
	struct iface *iface;    /* Pointer to interface control block */
	struct qhdr qhdr;

	iface = arg1;
	for(;;){
		while(iface->outq == NULLBUF)
			pwait(&iface->outq);

		iface->txbusy = 1;
		bp = dequeue(&iface->outq);
		pullup(&bp,(char *)&qhdr,sizeof(qhdr));
		if(iface->dtickle != NULL && (*iface->dtickle)(iface) == -1){
#ifdef  notdef  /* Confuses some non-compliant hosts */
			struct ip ip;

			/* Link redial failed; bounce with unreachable */
			ntohip(&ip,&bp);
			icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,
			 NULLICMP);
#endif
			free_p(bp);
		} else {
			(*iface->send)(bp,iface,qhdr.gateway,qhdr.tos);
		}
		iface->txbusy = 0;

		/* Let other tasks run, just in case send didn't block */
		pwait(NULL);
	}
}
/* Process packets in the Hopper */
void
network(i,v1,v2)
int i;
void *v1;
void *v2;
{
	struct mbuf *bp;
	struct iftype *ift;
	struct iface *ifp;

loop:
	for(;;){
		bp = Hopper;
		if(bp != NULLBUF){
			bp = dequeue(&Hopper);
			break;
		}
#ifndef PURIFY
		pwait(&Hopper);
#else
		return;
#endif
	}
	/* Process the input packet */
	pullup(&bp,(char *)&ifp,sizeof(ifp));
	if(ifp != NULLIF){
		ifp->rawrecvcnt++;
		ifp->lastrecv = secclock();
		ift = ifp->iftype;
	} else {
		ift = &Iftypes[0];
	}
	dump(ifp,IF_TRACE_IN,bp);

	if(ift->rcvf != NULLVFP)
		(*ift->rcvf)(ifp,bp);
	else
		free_p(bp);     /* Nowhere to send it */
	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
#ifndef PURIFY
	pwait(NULL);
	goto loop;
#endif
}

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(ifp,bp)
struct iface *ifp;
struct mbuf *bp;
{
	bp = pushdown(bp,sizeof(ifp));
	memcpy(&bp->data[0],(char *)&ifp,sizeof(ifp));
	enqueue(&Hopper,bp);
	return 0;
}

/* Null send and output routines for interfaces without link level protocols */
int
nu_send(bp,ifp,gateway,tos)
struct mbuf *bp;
struct iface *ifp;
int32 gateway;
int tos;
{
	return (*ifp->raw)(ifp,bp);
}
int
nu_output(ifp,dest,src,type,bp)
struct iface *ifp;
char *dest;
char *src;
int16 type;
struct mbuf *bp;
{
	return (*ifp->raw)(ifp,bp);
}

/* Set interface parameters */
int
doifconfig(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next)
			showiface(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULLIF){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showiface(ifp);
		if(ifp->show != NULLVFP){
			(*ifp->show)(ifp);
		}
		return 0;
	}
	if(argc == 3){
		printf("Argument missing\n");
		return 1;
	}
	for(i=2;i<argc-1;i+=2)
		subcmd(Ifcmds,3,&argv[i-1],ifp);

	return 0;
}

/* Set interface IP address */
static int
ifipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->addr = resolve(argv[1]);
	return 0;
}

/* Set link (hardware) address */
static int
iflinkadr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	if(ifp->iftype == NULLIFT || ifp->iftype->scan == NULL){
		printf("Can't set link address\n");
		return 1;
	}
	if(ifp->hwaddr != NULLCHAR)
		free(ifp->hwaddr);
	ifp->hwaddr = mallocw(ifp->iftype->hwalen);
	(*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
	return 0;
}

/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	struct route *rp;

	rp = rt_blookup(ifp->broadcast,32);
	if(rp != NULLROUTE && rp->iface == ifp)
		rt_drop(ifp->broadcast,32);
	ifp->broadcast = resolve(argv[1]);
	rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
	return 0;
}

/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;
	struct route *rp;

	/* Remove old entry if it exists */
	rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
	if(rp != NULLROUTE)
		rt_drop(rp->target,rp->bits);

	ifp->netmask = htol(argv[1]);
	rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,0L,0L,0);
	return 0;
}

/* Command to set interface encapsulation mode */
static int
ifencap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	if(setencap(ifp,argv[1]) != 0){
		printf("Encapsulation mode '%s' unknown\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Function to set encapsulation mode */
int
setencap(ifp,mode)
struct iface *ifp;
char *mode;
{
	struct iftype *ift;

	for(ift = &Iftypes[0];ift->name != NULLCHAR;ift++)
		if(strnicmp(ift->name,mode,strlen(mode)) == 0)
			break;
	if(ift->name == NULLCHAR)
		return -1;

	if(ifp != NULLIF){
		ifp->iftype = ift;
		ifp->send = ift->send;
		ifp->output = ift->output;
	}
	return 0;
}
/* Set interface receive buffer size */
static int
ifrxbuf(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return 0;       /* To be written */
}

/* Set interface Maximum Transmission Unit */
static int
ifmtu(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->mtu = atoi(argv[1]);
	return 0;
}

/* Set interface forwarding */
static int
ifforw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	ifp->forw = if_lookup(argv[1]);
	if(ifp->forw == ifp)
		ifp->forw = NULLIF;
	return 0;
}

/* Display the parameters for a specified interface */
static void
showiface(ifp)
register struct iface *ifp;
{
	char tmp[25];

	printf("%-10s IP addr %s MTU %u Link encap %s\n",ifp->name,
	 inet_ntoa(ifp->addr),(int)ifp->mtu,
	 ifp->iftype != NULL ? ifp->iftype->name : "not set");
	if(ifp->iftype != NULL && ifp->iftype->format != NULL && ifp->hwaddr != NULLCHAR){
		printf("           Link addr %s\n",
		 (*ifp->iftype->format)(tmp,ifp->hwaddr));
	}
	printf("           trace 0x%x netmask 0x%08lx broadcast %s\n",
		ifp->trace,ifp->netmask,inet_ntoa(ifp->broadcast));
	if(ifp->forw != NULLIF)
		printf("           output forward to %s\n",ifp->forw->name);
	printf("           sent: ip %lu tot %lu idle %s qlen %u",
	 ifp->ipsndcnt,ifp->rawsndcnt,tformat(secclock() - ifp->lastsent),
		len_q(ifp->outq));
	if(ifp->outlim != 0)
		printf("/%u",ifp->outlim);
	if(ifp->txbusy)
		printf(" BUSY");
	printf("\n");
	printf("           recv: ip %lu tot %lu idle %s\n",
	 ifp->iprecvcnt,ifp->rawrecvcnt,tformat(secclock() - ifp->lastrecv));
	switch (ifp->crccontrol){
	default:            printf("           crc disabled");      break;
	case CRC_TEST_16:   printf("           crc-16 test");       break;
	case CRC_TEST_RMNC: printf("           crc-rmnc test");     break;
	case CRC_16:        printf("           crc-16 enabled");    break;
	case CRC_RMNC:      printf("           crc-rmnc enabled");  break;
	case CRC_CCITT:     printf("           crc-ccitt enabled"); break;
	}
	printf(" crc errors %lu bad ax25 headers %lu\n",
	 ifp->crcerrors,ifp->ax25errors);
}
/* Detach a specified interface */
int
if_detach(ifp)
register struct iface *ifp;
{
	struct iface *iftmp;
	struct route *rp,*rptmp;
	int i,j;

	if(ifp == &Loopback || ifp == &Encap)
		return -1;

#if 0
	/* Drop all routes that point to this interface */
	if(R_default.iface == ifp)
		rt_drop(0L,0);  /* Drop default route */

	for(i=0;i<HASHMOD;i++){
		for(j=0;j<32;j++){
			for(rp = Routes[j][i];rp != NULLROUTE;rp = rptmp){
				/* Save next pointer in case we delete this entry */
				rptmp = rp->next;
				if(rp->iface == ifp)
					rt_drop(rp->target,rp->bits);
			}
		}
	}
	/* Unforward any other interfaces forwarding to this one */
	for(iftmp = Ifaces;iftmp != NULLIF;iftmp = iftmp->next){
		if(iftmp->forw == ifp)
			iftmp->forw = NULLIF;
	}
	/* Call device shutdown routine, if any */
	if(ifp->stop != NULLFP)
		(*ifp->stop)(ifp);

	killproc(ifp->rxproc);
	killproc(ifp->txproc);
	killproc(ifp->supv);
#endif

	/* Free allocated memory associated with this interface */
	if(ifp->name != NULLCHAR)
		free(ifp->name);
	if(ifp->hwaddr != NULLCHAR)
		free(ifp->hwaddr);
	/* Remove from interface list */
	if(ifp == Ifaces){
		Ifaces = ifp->next;
	} else {
		/* Search for entry just before this one
		 * (necessary because list is only singly-linked.)
		 */
		for(iftmp = Ifaces;iftmp != NULLIF ;iftmp = iftmp->next)
			if(iftmp->next == ifp)
				break;
		if(iftmp != NULLIF && iftmp->next == ifp)
			iftmp->next = ifp->next;
	}
	/* Finally free the structure itself */
	free((char *)ifp);
	return 0;
}
static int
iftxqlen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp = p;

	setint(&ifp->outlim,"TX queue limit",argc,argv);
	return 0;
}

/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULLIF if it doesn't exist
 */
struct iface *
if_lookup(name)
char *name;
{
	register struct iface *ifp;

	for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
		if(strcmp(ifp->name,name) == 0)
			break;
	return ifp;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULLIF otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(addr)
int32 addr;
{
	register struct iface *ifp;

	for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
		if(addr == ifp->addr)
			break;
	return ifp;
}

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(mask)
int32 mask;
{
	int width,i;

	width = 0;
	for(i = 31;i >= 0;i--){
		if(!(mask & (1L << i)))
			break;
		width++;
	}
	return width;
}

/* return buffer with name + comment */
char *
if_name(ifp,comment)
struct iface *ifp;
char *comment;
{
	char *result;

	result = mallocw(strlen(ifp->name) + strlen(comment) + 1);
	strcpy(result,ifp->name);
	strcat(result,comment);
	return result;
}

/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(ifp,bp)
struct iface *ifp;
struct mbuf *bp;
{
	free_p(bp);
	return 0;
}

