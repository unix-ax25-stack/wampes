/* @(#) $Id: iface.c,v 1.35 2002/09/20 15:18:50 dl9sau Exp $ */

/* IP interface control and configuration routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "dama.h"
#include "ip.h"
#include "icmp.h"
#include "netuser.h"
#include "ax25.h"
#include "cmdparse.h"
#include "commands.h"
#include "trace.h"
#include "pktdrvr.h"
#include "lapb.h"
#include "pidfilter.h"

static void showiface(struct iface *ifp);
static int mask2width(int32 mask);
static int ifipaddr(int argc,char *argv[],void *p);
static int iflinkadr(int argc,char *argv[],void *p);
static int ifbroad(int argc,char *argv[],void *p);
static int ifcrc(int argc,char *argv[],void *p);
int ifdama(int argc,char *argv[],void *p);
int ifdamatimeout(int argc,char *argv[],void *p);
static int ifnetmsk(int argc,char *argv[],void *p);
static int ifrxbuf(int argc,char *argv[],void *p);
static int ifmtu(int argc,char *argv[],void *p);
static int ifforw(int argc,char *argv[],void *p);
static int ifencap(int argc,char *argv[],void *p);
static int iftxqlen(int argc,char *argv[],void *p);
int iftncinit(int argc,char *argv[],void *p);
static int ifautoroute(int argc,char *argv[],void *p);
static int ifdigiarp(int argc,char *argv[],void *p);
static int ifeax25(int argc,char *argv[],void *p);
static int ifpaclen(int argc,char *argv[],void *p);
static int ifmaxframe(int argc,char *argv[],void *p);
static int ifemaxframe(int argc,char *argv[],void *p);
static int if_wants_rest(const char *word);

/* Interface list header */
struct iface *Ifaces = &Loopback;

/* Loopback pseudo-interface */
struct iface Loopback = {
	&Encap,         /* Link to next entry */
	"loopback",     /* name         */
	0x7f000001L,    /* addr         127.0.0.1 */
	-1,             /* broadcast    255.255.255.255 */
	-1,             /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* trace        */
	NULL,   /* trfp         */
	NULL,           /* forw         */
	NULL,   /* rxproc       */
	NULL,   /* txproc       */
	NULL,   /* supv         */
	NULL,   /* outq         */
	0,              /* outlim       */
	0,              /* txbusy       */
	NULL,           /* dstate       */
	NULL,           /* dtickle      */
	NULL,           /* dstatus      */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULL,           /* (*iostatus)  */
	NULL,           /* (*stop)      */
	NULL,   /* hwaddr       */
	NULL,           /* extension    */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	NULL,           /* (*send)      */
	NULL,           /* (*output)    */
	NULL,           /* (*raw)       */
	NULL,           /* (*status)    */
	NULL,           /* (*discard)   */
	NULL,           /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* crccontrol   */
	0,              /* crcfixed     */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
	0,              /* flags        */
};
/* Encapsulation pseudo-interface */
struct iface Encap = {
	NULL,
	"encap",        /* name         */
	INADDR_ANY,     /* addr         0.0.0.0 */
	-1,             /* broadcast    255.255.255.255 */
	-1,             /* netmask      255.255.255.255 */
	MAXINT16,       /* mtu          No limit */
	0,              /* trace        */
	NULL,   /* trfp         */
	NULL,           /* forw         */
	NULL,   /* rxproc       */
	NULL,   /* txproc       */
	NULL,   /* supv         */
	NULL,   /* outq         */
	0,              /* outlim       */
	0,              /* txbusy       */
	NULL,           /* dstate       */
	NULL,           /* dtickle      */
	NULL,           /* dstatus      */
	0,              /* dev          */
	NULL,           /* (*ioctl)     */
	NULL,           /* (*iostatus)  */
	NULL,           /* (*stop)      */
	NULL,   /* hwaddr       */
	NULL,           /* extension    */
	0,              /* xdev         */
	&Iftypes[0],    /* iftype       */
	ip_encap,       /* (*send)      */
	NULL,           /* (*output)    */
	NULL,           /* (*raw)       */
	NULL,           /* (*status)    */
	NULL,           /* (*discard)   */
	NULL,           /* (*echo)      */
	0,              /* ipsndcnt     */
	0,              /* rawsndcnt    */
	0,              /* iprecvcnt    */
	0,              /* rawrcvcnt    */
	0,              /* lastsent     */
	0,              /* lastrecv     */
	0,              /* crccontrol   */
	0,              /* crcfixed     */
	0,              /* crcerrors    */
	0,              /* ax25errors   */
	0,              /* flags        */
};

char Noipaddr[] = "IP address field missing, and ip address not set\n";

struct cmds Ifcmds[] = {
	{ "autoroute",            ifautoroute,    0,      2,
	  "ifconfig <iface> autoroute on|off" },
	{ "digiarp",              ifdigiarp,      0,      2,
	  "ifconfig <iface> digiarp list | add|del|addvia|delvia <digi>|-" },
	{ "broadcast",            ifbroad,        0,      2,
	  "ifconfig <iface> broadcast <ip address>" },
	{ "crc",                  ifcrc,          0,      2,
	  "ifconfig <iface> crc auto|off|16|rmnc|ccitt" },
	{ "dama",                 ifdama,         0,      2,
	  "ifconfig <iface> dama off|slave" },
	{ "damatimeout",          ifdamatimeout,  0,      2,
	  "ifconfig <iface> damatimeout <seconds>   (0 = built-in default)" },
	{ "eax25",                ifeax25,        0,      2,
	  "ifconfig <iface> eax25 off|accept|caller|always" },
	{ "emaxframe",            ifemaxframe,    0,      2,
	  "ifconfig <iface> emaxframe 0..63   (0 = use the node's)" },
	{ "encapsulation",        ifencap,        0,      2,
	  "ifconfig <iface> encapsulation <name>" },
	{ "maxframe",             ifmaxframe,     0,      2,
	  "ifconfig <iface> maxframe 0..7   (0 = use the node's)" },
	{ "paclen",               ifpaclen,       0,      2,
	  "ifconfig <iface> paclen 0..2048   (0 = use the node's)" },
	{ "forward",              ifforw,         0,      2,
	  "ifconfig <iface> forward <iface>   (send here, receive there)" },
	{ "ipaddress",            ifipaddr,       0,      2,
	  "ifconfig <iface> ipaddress <ip address>" },
	{ "linkaddress",          iflinkadr,      0,      2,
	  "ifconfig <iface> linkaddress <call>" },
	{ "mtu",                  ifmtu,          0,      2,
	  "ifconfig <iface> mtu <bytes>" },
	{ "netmask",              ifnetmsk,       0,      2,
	  "ifconfig <iface> netmask <ip netmask>" },
	{ "pid",                  ifpid,          0,      1,      Pid_usage },
	{ "tncinit",              iftncinit,      0,      1,
	  "ifconfig <iface> tncinit tapr|kenwood|kantronics|\"<sequence>\"|none" },
	{ "txqlen",               iftxqlen,       0,      2,
	  "ifconfig <iface> txqlen <packets>" },
	{ "rxbuf",                ifrxbuf,        0,      2,
	  "ifconfig <iface> rxbuf <bytes>" },
	{ NULL }
};
/*
 * General purpose interface transmit task, one for each device that can
 * send IP datagrams. It waits on the interface's IP output queue (outq),
 * extracts IP datagrams placed there in priority order by ip_route(),
 * and sends them to the device's send routine.
 */
void
if_tx(int dev,void *arg1,void *unused)
{
	struct mbuf *bp;        /* Buffer to send */
	struct iface *iface;    /* Pointer to interface control block */
	struct qhdr qhdr;

	iface = (struct iface *) arg1;
	for(;;){
		while(iface->outq == NULL)
			kwait(&iface->outq);

		iface->txbusy = 1;
		bp = dequeue(&iface->outq);
		pullup(&bp,&qhdr,sizeof(qhdr));
		if(iface->dtickle != NULL && (*iface->dtickle)(iface) == -1){
			free_p(&bp);
		} else {
			(*iface->send)(&bp,iface,qhdr.gateway,qhdr.tos);
		}
		iface->txbusy = 0;

		/* Let other tasks run, just in case send didn't block */
		kwait(NULL);
	}
}
/* Process packets in the Hopper */
void
network(int i,void *v1,void *v2)
{
	struct mbuf *bp;
	struct iftype *ift;
	struct iface *ifp;

loop:
	for(;;){
		bp = Hopper;
		if(bp != NULL){
			bp = dequeue(&Hopper);
			break;
		}
#ifndef SINGLE_THREADED
		kwait(&Hopper);
#else
		return;
#endif
	}
	/* Process the input packet */
	pullup(&bp,&ifp,sizeof(ifp));
	if(ifp != NULL){
		ifp->rawrecvcnt++;
		ifp->lastrecv = secclock();
		ift = ifp->iftype;
	} else {
		ift = &Iftypes[0];
	}
	dump(ifp,IF_TRACE_IN,bp);

	if(ift->rcvf != NULL)
		(*ift->rcvf)(ifp,&bp);
	else
		free_p(&bp);    /* Nowhere to send it */

	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
#ifndef SINGLE_THREADED
	kwait(NULL);
#endif
	goto loop;
}

/* put mbuf into Hopper for network task
 * returns 0 if OK
 */
int
net_route(struct iface *ifp,struct mbuf **bpp)
{
	if(bpp == NULL || *bpp == NULL)
		return 0;       /* bogus */
	pushdown(bpp,&ifp,sizeof(ifp));
	enqueue(&Hopper,bpp);
	return 0;
}

/* Null send and output routines for interfaces without link level protocols */
int
nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos)
{
	return (*ifp->raw)(ifp,bpp);
}
int
nu_output(struct iface *ifp,uint8 *dest,uint8 *src,uint type,struct mbuf **bpp)
{
	return (*ifp->raw)(ifp,bpp);
}

/* Set interface parameters */
int
doifconfig(int argc,char *argv[],void *p)
{
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(ifp = Ifaces;ifp != NULL;ifp = ifp->next)
			showiface(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showiface(ifp);
		if(ifp->show != NULL){
			(*ifp->show)(ifp);
		}
		return 0;
	}
	if(argc == 3){
		/* One word and no value.  This used to be answered with
		 * "Argument missing" before the subcommand was ever reached,
		 * which made two things unreachable: the settings that take no
		 * value and show what is in force ("tncinit", "pid"), and the
		 * usage text, which subcmd() prints for the rest.  Both are
		 * better answers than a sentence that names no command.
		 */
		return subcmd(Ifcmds,2,&argv[1],ifp);
	}
	/* The settings are name/value pairs and several may stand on one line:
	 * "ifconfig ax0 mtu 256 paclen 128".  A few take a LIST instead, and
	 * those get the rest of the line - which is why they have to be the
	 * last thing on it.
	 */
	if(if_wants_rest(argv[2]))
		return subcmd(Ifcmds,argc-1,&argv[1],ifp);
	for(i=2;i<argc-1;i+=2)
		subcmd(Ifcmds,3,&argv[i-1],ifp);

	return 0;
}

/* Does this subcommand read a list rather than one value?  The same prefix
 * match subcmd() will make, so that an abbreviation is answered the same way
 * the full word is.
 */
static int
if_wants_rest(const char *word)
{
	struct cmds *cmdp;

	for(cmdp = Ifcmds;cmdp->name != NULL;cmdp++)
		if(strncmp(word,cmdp->name,strlen(word)) == 0)
			return cmdp->func == ifpid;
	return 0;
}

/* Set interface IP address */
static int
ifipaddr(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	ifp->addr = resolve(argv[1]);
	return 0;
}

/* Set link (hardware) address */
static int
iflinkadr(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	if(ifp->iftype == NULL || ifp->iftype->scan == NULL){
		printf("Can't set link address\n");
		return 1;
	}
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	ifp->hwaddr = (uint8 *) mallocw(ifp->iftype->hwalen);
	(*ifp->iftype->scan)(ifp->hwaddr,argv[1]);
	/* The port now answers to this callsign, so it is the node's: any
	 * forwarding entry for a protocol we serve ourselves goes.  Here
	 * rather than only at attach, because this is the command that moves
	 * a callsign onto a port after everything else has been configured.
	 */
	if(ifp->iftype->type == CL_AX25)
		axlisten_drop_local(ifp->hwaddr);
	return 0;
}

/* Enable/disable the automatic learning of routes through this interface.
 */
static int
ifautoroute(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;
	int enabled;

	enabled = !(ifp->flags & NO_RT_ADD);
	setbool(&enabled, "IP automatic route learning", argc, argv);
	if (enabled) {
		ifp->flags &= ~NO_RT_ADD;
	} else {
		ifp->flags |= NO_RT_ADD;
	}
	return 0;
}

/* Set interface broadcast address. This is actually done
 * by installing a private entry in the routing table.
 */
static int
ifbroad(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;
	struct route *rp;

	rp = rt_blookup(ifp->broadcast,32);
	if(rp != NULL && rp->iface == ifp)
		rt_drop(ifp->broadcast,32);
	ifp->broadcast = resolve(argv[1]);
	rt_add(ifp->broadcast,32,0L,ifp,1L,0L,1);
	return 0;
}

/* Set interface CRC mode.  Naming a mode also stops the KISS receiver from
 * changing it again: an incoming frame with a valid CRC used to overwrite
 * whatever was configured here, so this command did not stick.  "auto" puts
 * the interface back to probing.
 */
static int
ifcrc(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	ifp->crcfixed = 1;
	switch (argv[1][0]) {
	case 'A':
	case 'a':
		ifp->crccontrol = CRC_TEST_16;
		ifp->crcfixed = 0;
		break;
	case 'O':
	case 'o':
		ifp->crccontrol = CRC_OFF;
		break;
	case '1':
		ifp->crccontrol = CRC_16;
		break;
	case 'R':
	case 'r':
		ifp->crccontrol = CRC_RMNC;
		break;
	case 'C':
	case 'c':
		ifp->crccontrol = CRC_CCITT;
		break;
	default:
		return -1;
	}
	return 0;
}

/* Who decides whether a link on this port runs modulo-128.
 *
 *   off      never.  An incoming SABME is answered with DM, and we never send
 *            one - for the interlink whose partner is known not to speak it,
 *            so not a single probe is wasted, and for a slow channel where
 *            the wider window buys almost nothing anyway.
 *   caller   the default.  A caller who asked for plain AX.25 is carried
 *            onward as plain AX.25 - if the upper leg then misbehaves he is
 *            the one who could do nothing about it.  A connect that starts
 *            here tries modulo-128 once and remembers the answer.
 *   always   also upgrades a caller who asked for AX.25.  For an exclusive
 *            interlink at a higher bit rate, where the wider window is worth
 *            most and the operator knows the partner.
 */

static int
ifeax25(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	if(argc < 2){
		printf("EAX25 (modulo-128): %s\n",
		 ifp->eax25 == EAX25_OFF ? "off" :
		 ifp->eax25 == EAX25_ALWAYS ? "always" :
		 ifp->eax25 == EAX25_CALLER ? "caller" : "accept");
		return 0;
	}
	if(!stricmp(argv[1],"off"))
		ifp->eax25 = EAX25_OFF;
	else if(!stricmp(argv[1],"accept"))
		ifp->eax25 = EAX25_ACCEPT;
	else if(!stricmp(argv[1],"caller"))
		ifp->eax25 = EAX25_CALLER;
	else if(!stricmp(argv[1],"always"))
		ifp->eax25 = EAX25_ALWAYS;
	else {
		printf("Valid options: off accept caller always\n");
		return 1;
	}
	return 0;
}

/* Packet length and window, per port.  Zero gives the node's own setting
 * back, which is what an unconfigured port uses.  These are properties of the
 * CHANNEL - a 1k2 user access and a 19k2 interlink want different answers and
 * until now could not have them.
 *
 * The driver's hard limit still wins over whatever is set here; see
 * ax25_apply_iface_limits().
 */

static int
ifpaclen(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	return setintrc(&ifp->paclen,"Max frame length, this port (0 = node)",
	 argc,argv,0,MAXINT16);
}

static int
ifmaxframe(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	return setintrc(&ifp->maxframe,"Window, this port (0 = node)",
	 argc,argv,0,7);
}

static int
ifemaxframe(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	return setintrc(&ifp->emaxframe,
	 "Window modulo-128, this port (0 = node)",argc,argv,0,63);
}

/* Set the network mask. This is actually done by installing
 * a routing entry.
 */
static int
ifnetmsk(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;
	struct route *rp;

	/* Remove old entry if it exists */
	rp = rt_blookup(ifp->addr & ifp->netmask,mask2width(ifp->netmask));
	if(rp != NULL)
		rt_drop(rp->target,rp->bits);

	ifp->netmask = htol(argv[1]);
	rt_add(ifp->addr,mask2width(ifp->netmask),0L,ifp,0L,0L,0);
	return 0;
}

/* Command to set interface encapsulation mode */
static int
ifencap(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	if(setencap(ifp,argv[1]) != 0){
		printf("Encapsulation mode '%s' unknown\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Function to set encapsulation mode */
int
setencap(struct iface *ifp,char *mode)
{
	struct iftype *ift;

	for(ift = &Iftypes[0];ift->name != NULL;ift++)
		if(strnicmp(ift->name,mode,strlen(mode)) == 0)
			break;
	if(ift->name == NULL)
		return -1;

	if(ifp != NULL){
		ifp->iftype = ift;
		ifp->send = ift->send;
		ifp->output = ift->output;
	}
	return 0;
}
/* Set interface receive buffer size */
static int
ifrxbuf(int argc,char *argv[],void *p)
{
	return 0;       /* To be written */
}

/* Is this a size IP can work with?  RFC 791 requires every link to carry a
 * datagram of 68 octets without fragmenting it, so that is the floor.  It is
 * also comfortably above the point where the fragmentation arithmetic in
 * ip_route() gives out: fragsize = (mtu - ip_len) & 0xfff8 rounds down to
 * zero from 27 downwards, and below 20 the unsigned subtraction underflows
 * and puts one fragment with a nonsensical length field on the air before
 * giving up.  See doc/AX25-MTU-SEGMENTATION.md.
 *
 * IPv6 would want 1280 here (RFC 8200 section 5), and on a link that cannot
 * carry that in one piece it also demands fragmentation and reassembly below
 * IPv6 - which is exactly what the AX.25 segmenter is.  WAMPES has no IPv6,
 * so 68 it is; the number is worth knowing before that changes.
 */
int
mtu_ok(const char *who,long mtu)
{
	if(mtu < MTU_MIN){
		printf("%s: mtu %ld is below the %d octets IP needs (RFC 791)\n",
		 who,mtu,MTU_MIN);
		return 0;
	}
	return 1;
}

/* Set interface Maximum Transmission Unit */
static int
ifmtu(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;
	long mtu = atol(argv[1]);

	if(!mtu_ok(ifp->name,mtu))
		return 1;
	if(ifp->framemax && mtu > ifp->framemax){
		printf("%s: mtu %ld is above the %d octets this port can carry\n",
		 ifp->name,mtu,ifp->framemax);
		return 1;
	}
	ifp->mtu = (uint) mtu;
	return 0;
}

/* Set interface forwarding */
static int
ifforw(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	ifp->forw = if_lookup(argv[1]);
	if(ifp->forw == ifp)
		ifp->forw = NULL;
	return 0;
}

/* Display the parameters for a specified interface */
static void
showiface(struct iface *ifp)
{
	char tmp[25];

	printf("%-10s IP addr %s MTU %u Link encap %s\n",ifp->name,
	 inet_ntoa(ifp->addr),(int)ifp->mtu,
	 ifp->iftype != NULL ? ifp->iftype->name : "not set");
	if(ifp->iftype != NULL && ifp->iftype->format != NULL && ifp->hwaddr != NULL){
		printf("           Link addr %s\n",
		 (*ifp->iftype->format)(tmp,ifp->hwaddr));
	}
	printf("           trace 0x%x netmask 0x%08lx broadcast %s\n",
		ifp->trace,(unsigned long)ifp->netmask,inet_ntoa(ifp->broadcast));
	if(ifp->forw != NULL)
		printf("           output forward to %s\n",ifp->forw->name);
	dama_show(ifp);
	printf("           sent: ip %lu tot %lu idle %s qlen %u",
	 (unsigned long)ifp->ipsndcnt,(unsigned long)ifp->rawsndcnt,
	 tformat(secclock() - ifp->lastsent),
		len_q(ifp->outq));
	if(ifp->outlim != 0)
		printf("/%u",ifp->outlim);
	if(ifp->txbusy)
		printf(" BUSY");
	printf("\n");
	printf("           recv: ip %lu tot %lu idle %s\n",
	 (unsigned long)ifp->iprecvcnt,(unsigned long)ifp->rawrecvcnt,
	 tformat(secclock() - ifp->lastrecv));
	if(ifp->paclen || ifp->maxframe || ifp->emaxframe || ifp->framemax){
		printf("           paclen %d maxframe %d emaxframe %d",
		 ifp->paclen ? ifp->paclen : Paclen,
		 ifp->maxframe ? ifp->maxframe : Maxframe,
		 ifp->emaxframe ? ifp->emaxframe : EMaxframe);
		if(ifp->framemax)
			printf("  (this port carries at most %d octets)",
			 ifp->framemax);
		printf("\n");
	}
	printf("           eax25: %s\n",
	 ifp->eax25 == EAX25_OFF ? "off" :
	 ifp->eax25 == EAX25_ALWAYS ? "always" :
	 ifp->eax25 == EAX25_CALLER ? "caller" : "accept");
	switch (ifp->crccontrol){
	default:            printf("           crc off");           break;
	case CRC_TEST_16:   printf("           crc-16 test");       break;
	case CRC_TEST_RMNC: printf("           crc-rmnc test");     break;
	case CRC_16:        printf("           crc-16 enabled");    break;
	case CRC_RMNC:      printf("           crc-rmnc enabled");  break;
	case CRC_CCITT:     printf("           crc-ccitt enabled"); break;
	}
	printf(" crc errors %lu bad ax25 headers %lu\n",
	 (unsigned long)ifp->crcerrors,(unsigned long)ifp->ax25errors);
}
/* Detach a specified interface */
int
if_detach(struct iface *ifp)
{
	struct iface *iftmp;

	if(ifp == &Loopback || ifp == &Encap)
		return -1;

	/* Free allocated memory associated with this interface */
	if(ifp->name != NULL)
		free(ifp->name);
	if(ifp->hwaddr != NULL)
		free(ifp->hwaddr);
	/* Remove from interface list */
	if(ifp == Ifaces){
		Ifaces = ifp->next;
	} else {
		/* Search for entry just before this one
		 * (necessary because list is only singly-linked.)
		 */
		for(iftmp = Ifaces;iftmp != NULL ;iftmp = iftmp->next)
			if(iftmp->next == ifp)
				break;
		if(iftmp != NULL && iftmp->next == ifp)
			iftmp->next = ifp->next;
	}
	/* Finally free the structure itself */
	free(ifp);
	return 0;
}
static int
iftxqlen(int argc,char *argv[],void *p)
{
	struct iface *ifp = (struct iface *) p;

	setint(&ifp->outlim,"TX queue limit",argc,argv);
	return 0;
}

// dl9sau: patch for ARP requests (to QST-0 or directly to a digipeater)
// for an extended "collision domain"
static int
ifdigiarp(int argc, char *argv[], void *p)
{
	struct iface *ifp = (struct iface *) p;
	char tmp[AXBUF];
	uint8 **ax_via;
	int len;
	char *cmd;
	int arp_to_digi = 1;
	char *cp;

	if (!ifp || !ifp->iftype || !(ax_via = ifp->iftype->ax_mcast_digis)) {
	  printf("Not supported by this interface\n");
	  return -1;
	}

	cmd = *(++argv);
	len = strlen(cmd);
	++argv;

	// an ARP request could go to
	// QST-0 (normal, direct), to a DIGI or to QST-0 via DIGI
	if (len > 3 && (cp = strstr(cmd, "v")) && !strncmp("via", cp, strlen(cp))) {
	  arp_to_digi = 0;
	  *cp = 0;
	  len = cp-cmd;
	}
	if (!strncmp("add", cmd, len)) {
	  uint8 call[AXALEN];
	  // "-" means no direct ARPs
	  if (!*argv) {
	    printf("need an argument\n");
	    return -1;
	  }
	  if (!strcmp(*argv, "-"))
	    memset(call, 0, AXALEN);
          else if (!setcall(call, *argv)) {
	    if (!arp_to_digi)
	      call[AXALEN-1] |= 1;	// this is safe through addreq (ssid field)
	  } else {
	    printf("Not a valid call: %s\n", *argv);
	    return -1;
	  }
	  for (len = 0; len < AX_MCAST_DIGIS_MAX; len++) {
	    if (ax_via[len]) {
	      if (addreq(ax_via[len], call) && ax_via[len][AXALEN-1] == call[AXALEN-1]) {
		printf("Already stored: %s\n", *argv);
		return 0;
	      }
	      continue;
	    }
	    if (!(ax_via[len] = (uint8 *) mallocw(AXALEN))) {
	      printf("Out of memory\n");
	      return -1;
	    }
	    memcpy(ax_via[len], call, AXALEN);
	    return 0;
	  }
	  printf("Too many entries to store %s (max %d)\n", *argv, AX_MCAST_DIGIS_MAX);
	  return -1;
	} else if (!strncmp("delete", cmd, len)) {
	  uint8 call[AXALEN];
	  if (!*argv) {
	    printf("need an argument\n");
	    return -1;
	  }
	  // "-" means no direct ARPs
	  if (!strcmp(*argv, "-"))
	    memset(call, 0, AXALEN);
          else if (!setcall(call, *argv)) {
	    if (!arp_to_digi)
	      call[AXALEN-1] |= 0x01;	// this is safe through addreq (ssid field)
	  } else {
	    printf("Not a valid call: %s\n", *argv);
	    return -1;
	  }
	  for (len = 0; len < AX_MCAST_DIGIS_MAX && ax_via[len]; len++) {
	    if (!addreq(ax_via[len], call) || ax_via[len][AXALEN-1] != call[AXALEN-1])
	      continue;
	    free(ax_via[len]);
	    while (len < AX_MCAST_DIGIS_MAX-1) {
	      ax_via[len] = ax_via[len+1];
	      len++;
	    }
	    ax_via[AX_MCAST_DIGIS_MAX-1] = 0;
	    return 0;
	  }
	  printf("No such entry %s\n", *argv);
	  return -1;
	} else if (!strncmp("list", cmd, len)) {
	  if (ax_via[0]) {
	    for (len = 0; len < AX_MCAST_DIGIS_MAX && ax_via[len]; len++) {
	      if ((ax_via[len][AXALEN-1] & 0x01) == 1)
		printf("QST-0 via ");
	      printf("%s\n", (ax_via[len][0] ? pax25(tmp, ax_via[len]) : "[no direct ARPs]"));
	    }
	  }
	  else
	    printf("No entries\n");
	} else {
  	  printf("Subcommands: list | <add|del|addvia|delvia> <digi>.\n");
	  printf("               digi:   \"-\" means no direct ARPs\n");
	  printf("               add:    ARP directly to digi\n");
	  printf("               addvia: ARP to QST-0 via digi\n");
	  return -1;
	}
	return 0;
}

/* Given the ascii name of an interface, return a pointer to the structure,
 * or NULL if it doesn't exist
 */
struct iface *
if_lookup(char *name)
{
	struct iface *ifp;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(strcmp(ifp->name,name) == 0)
			break;
	return ifp;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULL otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(int32 addr)
{
	struct iface *ifp;

	if(addr == INADDR_ANY)
		return &Loopback;
	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if(addr == ifp->addr)
			break;
	return ifp;
}

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(int32 mask)
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
if_name(struct iface *ifp,char *comment)
{
	char *result;

	result = (char *) mallocw(strlen(ifp->name) + strlen(comment) + 1);
	strcpy(result,ifp->name);
	strcat(result,comment);
	return result;
}

/* Raw output routine that tosses all packets. Used by dialer, tip, etc */
int
bitbucket(struct iface *ifp,struct mbuf **bpp)
{
	free_p(bpp);
	return 0;
}
