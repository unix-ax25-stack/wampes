/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/config.c,v 1.18 1992/05/26 10:08:53 deyke Exp $ */

/* A collection of stuff heavily dependent on the configuration info
 * in config.h. The idea is that configuration-dependent tables should
 * be located here to avoid having to pepper lots of .c files with #ifdefs,
 * requiring them to include config.h and be recompiled each time config.h
 * is modified.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */

#include <stdio.h>
/* #include <dos.h> */
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#ifdef  ARCNET
#include "arcnet.h"
#endif
#include "lapb.h"
#include "ax25.h"
#include "enet.h"
#include "kiss.h"
/* #include "nr4.h" */
#include "nrs.h"
#include "netrom.h"
#include "pktdrvr.h"
/* #include "ppp.h" */
#include "slip.h"
#include "arp.h"
#include "icmp.h"
#include "hardware.h"   /***/
/* #include "usock.h" */
#include "cmdparse.h"
#include "commands.h"
/* #include "mailbox.h" */
/* #include "ax25mail.h" */
/* #include "nr4mail.h" */
/* #include "tipmail.h" */
#include "daemon.h"
#include "bootp.h"
#include "asy.h"
#include "trace.h"
#ifdef  QTSO
#include "qtso.h"
#endif

static int dostart __ARGS((int argc,char *argv[],void *p));
static int dostop __ARGS((int argc,char *argv[],void *p));
static int dostatus __ARGS((int argc,char *argv[],void *p));

#ifdef  AX25
static void axip __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast));
static void axarp __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast));
static void axnr __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast));
#endif  /* AX25 */

struct mbuf *Hopper;            /* Queue of incoming packets */
unsigned Nsessions = NSESSIONS;
int Shortstatus;

/* Free memory threshold, below which things start to happen to conserve
 * memory, like garbage collection, source quenching and refusing connects
 */
int32 Memthresh = MTHRESH;

/* Command lookup and branch tables */
struct cmds Cmds[] = {
	/* The "go" command must be first */
	"",             go,             0, 0, NULLCHAR,
#ifndef AMIGA
	"!",            doshell,        0, 0, NULLCHAR,
#endif
/*      "abort",        doabort,        0, 0, NULLCHAR, */
#ifdef  AMIGA
	"amiga",        doamiga,        0, 0, NULLCHAR,
#endif
#if     (defined(MAC) && defined(APPLETALK))
	"applestat",    doatstat,       0,      0, NULLCHAR,
#endif
#if     (defined(AX25) || defined(ETHER) || defined(APPLETALK))
	"arp",          doarp,          0, 0, NULLCHAR,
#endif
#ifdef  ASY
	"asystat",      doasystat,      0, 0, NULLCHAR,
#endif
	"attach",       doattach,       0, 2,
		"attach <hardware> <hw specific options>",
#ifdef  AX25
	"ax25",         doax25,         0, 0, NULLCHAR,
	"axip",         doaxip,         0, 0, NULLCHAR,
	"axudp",        doaxudp,        0, 0, NULLCHAR,
#endif
#ifdef  BOOTP
	"bootp",        dobootp,        0, 0, NULLCHAR,
	"bootpd",       bootpdcmd,      0, 0, NULLCHAR,
#endif
	"bye",          dobye,          0, 0, NULLCHAR,
/* This one is out of alpabetical order to allow abbreviation to "c" */
#ifdef  AX25
	"connect",      doconnect,      1024, 2,
	"connect callsign [digipeaters]",
#endif
#if     !defined(UNIX) && !defined(AMIGA)
	"cd",           docd,           0, 0, NULLCHAR,
#endif
	"close",        doclose,        0, 0, NULLCHAR,
/* This one is out of alpabetical order to allow abbreviation to "d" */
	"disconnect",   doclose,        0, 0, NULLCHAR,
	"delete",       dodelete,       0, 2, "delete <file>",
/*      "detach",       dodetach,       0, 2, "detach <interface>", */
#ifdef  DIALER
	"dialer",       dodialer,       0, 2,
	"dialer <iface> <timeout> [<raise script> <lower script>]",
#endif
#ifndef AMIGA
/*      "dir",          dodir,          512, 0, NULLCHAR, /* note sequence */
#endif
	"domain",       dodomain,       0, 0, NULLCHAR,
#ifdef  DRSI
	"drsistat",     dodrstat,       0, 0, NULLCHAR,
#endif
#ifdef  EAGLE
	"eaglestat",    doegstat,       0, 0, NULLCHAR,
#endif
	"echo",         doecho,         0, 0, NULLCHAR,
	"eol",          doeol,          0, 0, NULLCHAR,
#if     !defined(MSDOS)
	"escape",       doescape,       0, 0, NULLCHAR,
#endif
	"exit",         doexit,         0, 0, NULLCHAR,
/*      "files",        dofiles,        0, 0, NULLCHAR, */
	"finger",       dofinger,       1024, 2, "finger name@host",
	"fkey",         dofkey,         0, 3, "fkey <key#> <text>",
	"ftp",          doftp,          2048, 2, "ftp <address>",
#ifdef HAPN
	"hapnstat",     dohapnstat,     0, 0, NULLCHAR,
#endif
/*      "help",         dohelp,         0, 0, NULLCHAR, */
#ifdef  HOPCHECK
	"hop",          dohop,          0, 0, NULLCHAR,
#endif
	"hostname",     dohostname,     0, 0, NULLCHAR,
#ifdef  HS
	"hs",           dohs,           0, 0, NULLCHAR,
#endif
	"icmp",         doicmp,         0, 0, NULLCHAR,
	"ifconfig",     doifconfig,     0, 0, NULLCHAR,
	"ip",           doip,           0, 0, NULLCHAR,
#ifdef  MSDOS
	"isat",         doisat,         0, 0, NULLCHAR,
#endif
	"kick",         dokick,         0, 0, NULLCHAR,
	"log",          dolog,          0, 0, NULLCHAR,
#ifdef  MAILBOX
/*      "mbox",         dombox,         0, 0, NULLCHAR, */
#endif
#if     1
	"memory",       domem,          0, 0, NULLCHAR,
#endif
	"mkdir",        domkd,          0, 2, "mkdir <directory>",
#ifdef  AX25
	"mode",         domode,         0, 2, "mode <interface>",
#endif
/*      "more",         domore,         512, 2, "more <filename>", */
#ifdef  NETROM
	"netrom",       donetrom,       0, 0, NULLCHAR,
#endif  /* NETROM */
#ifdef  NNTP
	"nntp",         donntp,         0, 0, NULLCHAR,
#endif  /* NNTP */
#ifdef  NRS
	"nrstat",       donrstat,       0, 0, NULLCHAR,
#endif  /* NRS */
	"param",        doparam,        0, 2, "param <interface>",
	"ping",         doping,         512, 0,
	NULLCHAR,
#ifdef  PI
	"pistatus",     dopistat,       0, 0, NULLCHAR,
#endif
#ifdef POP
	"pop",          dopop,          0, 0, NULLCHAR,
#endif
#ifdef PPP
	"ppp",          doppp_commands, 0, 0, NULLCHAR,
#endif
	"ps",           ps,             0, 0, NULLCHAR,
#if     !defined(UNIX) && !defined(AMIGA)
	"pwd",          docd,           0, 0, NULLCHAR,
#endif
	"record",       dorecord,       0, 0, NULLCHAR,
	"remote",       doremote,       0, 3, "remote [-p port] [-k key] [-a kickaddr] <address> exit|reset|kick",
	"rename",       dorename,       0, 3, "rename <oldfile> <newfile>",
	"reset",        doreset,        0, 0, NULLCHAR,
#ifdef  RIP
	"rip",          dorip,          0, 0, NULLCHAR,
#endif
	"rmdir",        dormd,          0, 2, "rmdir <directory>",
	"route",        doroute,        0, 0, NULLCHAR,
	"status",       dostatus,       0, 0, NULLCHAR,
	"session",      dosession,      0, 0, NULLCHAR,
#ifdef  SCC
	"sccstat",      dosccstat,      0, 0, NULLCHAR,
#endif
#if     !defined(AMIGA)
	"shell",        doshell,        0, 0, NULLCHAR,
#endif
	"smtp",         dosmtp,         0, 0, NULLCHAR,
/*      "socket",       dosock,         0, 0, NULLCHAR, */
	"source",       dosource,       0, 2, "source <filename>",
#ifdef  SERVERS
	"start",        dostart,        0, 2, "start <servername>",
	"stop",         dostop,         0, 2, "stop <servername>",
#endif
	"tcp",          dotcp,          0, 0, NULLCHAR,
	"telnet",       dotelnet,       1024, 2, "telnet <address>",
/*      "test",         dotest,         1024, 0, NULLCHAR, */
/*      "tip",          dotip,          256, 2, "tip <iface", */
#ifdef  TRACE
	"trace",        dotrace,        512, 0, NULLCHAR,
#endif
	"udp",          doudp,          0, 0, NULLCHAR,
	"upload",       doupload,       0, 0, NULLCHAR,
#ifdef  MSDOS
	"watch",        doswatch,       0, 0, NULLCHAR,
#endif
/*      "?",            dohelp,         0, 0, NULLCHAR, */
	NULLCHAR,       NULLFP,         0, 0,
		"Unknown command; type \"?\" for list",
};

/* List of supported hardware devices */
struct cmds Attab[] = {
#ifdef  ASY
	/* Ordinary PC asynchronous adaptor */
	"asy", asy_attach, 0, 8,
#ifndef AMIGA
	"attach asy <address> <vector> slip|ax25|nrs <label> <buffers> <mtu> <speed> [ip_addr]",
#else
	"attach asy <driver> <unit> slip|ax25|nrs <label> <buffers> <mtu> <speed> [ip_addr]",
#endif  /* AMIGA */
#endif  /* ASY */
#ifdef  PC100
	/* PACCOMM PC-100 8530 HDLC adaptor */
	"pc100", pc_attach, 0, 8,
	"attach pc100 <address> <vector> ax25 <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef  DRSI
	/* DRSI PCPA card in low speed mode */
	"drsi", dr_attach, 0, 8,
	"attach drsi <address> <vector> ax25 <label> <bufsize> <mtu>\
<chan a speed> <chan b speed> [ip addr a] [ip addr b]",
#endif
#ifdef  EAGLE
	/* EAGLE RS-232C 8530 HDLC adaptor */
	"eagle", eg_attach, 0, 8,
	"attach eagle <address> <vector> ax25 <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef  PI
	/* PI 8530 HDLC adaptor */
	"pi", pi_attach, 0, 8,
	"attach pi <address> <vector> <dmachannel> ax25 <label> <buffers>\
 <mtu> <speed> [ip_addra] [ip_addrb]",
#endif
#ifdef  HAPN
	/* Hamilton Area Packet Radio (HAPN) 8273 HDLC adaptor */
	"hapn", hapn_attach, 0, 8,
	"attach hapn <address> <vector> ax25 <label> <rx bufsize>\
 <mtu> csma|full [ip_addr]",
#endif
#ifdef  APPLETALK
	/* Macintosh AppleTalk */
	"0", at_attach, 0, 7,
	"attach 0 <protocol type> <device> arpa <label> <rx bufsize> <mtu> [ip_addr]",
#endif
#ifdef NETROM
	/* fake netrom interface */
	"netrom", nr_attach, 0, 1,
	"attach netrom [ip_addr]",
#endif
#ifdef  PACKET
	/* FTP Software's packet driver spec */
	"packet", pk_attach, 0, 4,
	"attach packet <int#> <label> <buffers> <mtu> [ip_addr]",
#endif
#ifdef  HS
	/* Special high speed driver for DRSI PCPA or Eagle cards */
	"hs", hs_attach, 0, 7,
	"attach hs <address> <vector> ax25 <label> <buffers> <mtu>\
 <txdelay> <persistence> [ip_addra] [ip_addrb]",
#endif
#ifdef SCC
	"scc", scc_attach, 0, 7,
	"attach scc <devices> init <addr> <spacing> <Aoff> <Boff> <Dataoff>\n"
	"   <intack> <vec> [p]<clock> [hdwe] [param]\n"
	"attach scc <chan> slip|kiss|nrs|ax25 <label> <mtu> <speed> <bufsize> [call] ",
#endif
#ifdef  AX25
	"axip", axip_attach, 0, 1,
	"attach axip",
	"axudp", axudp_attach, 0, 1,
	"attach axudp",
#endif
	"ipip", ipip_attach, 0, 5,
	"attach ipip <label> ip|udp <dest> <port>",
	NULLCHAR,
};

#ifdef  SERVERS
/* "start" and "stop" subcommands */
static struct cmds Startcmds[] = {
#if     defined(AX25) && defined(MAILBOX)
	"ax25",         ax25start,      256, 0, NULLCHAR,
#endif
	"discard",      dis1,           256, 0, NULLCHAR,
	"echo",         echo1,          256, 0, NULLCHAR,
/*      "finger",       finstart,       256, 0, NULLCHAR, */
	"ftp",          ftpstart,       256, 0, NULLCHAR,
	"tcpgate",      tcpgate1,       256, 2, "start tcpgate <tcp port> [<host:service>]",
#if     defined(NETROM) && defined(MAILBOX)
	"netrom",       nr4start,       256, 0, NULLCHAR,
#endif
#ifdef POP
	"pop",          pop1,           256, 0, NULLCHAR,
#endif
#ifdef  RIP
	"rip",          doripinit,      0,   0, NULLCHAR,
#endif
/*      "smtp",         smtp1,          256, 0, NULLCHAR, */
#if     defined(MAILBOX)
	"telnet",       telnet1,        256, 0, NULLCHAR,
/*      "tip",          tipstart,       256, 2, "start tip <interface>", */
#endif
/*      "ttylink",      ttylstart,      256, 0, NULLCHAR, */
	"remote",       rem1,           768, 0, NULLCHAR,
	NULLCHAR,
};

static struct cmds Stopcmds[] = {
#if     defined(AX25) && defined(MAILBOX)
	"ax25",         ax250,          0, 0, NULLCHAR,
#endif
	"discard",      dis0,           0, 0, NULLCHAR,
	"echo",         echo0,          0, 0, NULLCHAR,
/*      "finger",       fin0,           0, 0, NULLCHAR, */
	"ftp",          ftp0,           0, 0, NULLCHAR,
#if     defined(NETROM) && defined(MAILBOX)
	"netrom",       nr40,           0, 0, NULLCHAR,
#endif
#ifdef  POP
	"pop",          pop0,           0, 0, NULLCHAR,
#endif
#ifdef  RIP
	"rip",          doripstop,      0, 0, NULLCHAR,
#endif
/*      "smtp",         smtp0,          0, 0, NULLCHAR, */
#ifdef  MAILBOX
	"telnet",       telnet0,        0, 0, NULLCHAR,
/*      "tip",          tip0,           0, 2, "stop tip <interface>", */
#endif
/*      "ttylink",      ttyl0,          0, 0, NULLCHAR, */
	"remote",       rem0,           0, 0, NULLCHAR,
	NULLCHAR,

};
#endif  /* SERVERS */

/* TCP port numbers to be considered "interactive" by the IP routing
 * code and given priority in queueing
 */
int Tcp_interact[] = {
	IPPORT_FTP,     /* FTP control (not data!) */
	IPPORT_TELNET,  /* Telnet */
	IPPORT_LOGIN,   /* BSD rlogin */
	IPPORT_MTP,     /* Secondary telnet */
	-1
};

/* Transport protocols atop IP */
struct iplink Iplink[] = {
	TCP_PTCL,       tcp_input,
	UDP_PTCL,       udp_input,
	ICMP_PTCL,      icmp_input,
	IP_PTCL,        ipip_recv,
	0,              0
};

/* Transport protocols atop ICMP */
struct icmplink Icmplink[] = {
	TCP_PTCL,       tcp_icmp,
	0,              0
};

#ifdef  AX25
/* Linkage to network protocols atop ax25 */
struct axlink Axlink[] = {
	PID_IP,         axip,
	PID_ARP,        axarp,
#ifdef  NETROM
	PID_NETROM,     axnr,
#endif
	PID_NO_L3,      axnl3,
	0,              NULL,
};
#endif  /* AX25 */

/* ARP protocol linkages, indexed by arp's hardware type */
struct arp_type Arp_type[NHWTYPES] = {
#ifdef  NETROM
	AXALEN, 0, 0, 0, NULLCHAR, pax25, setcall,      /* ARP_NETROM */
#else
	0, 0, 0, 0, NULLCHAR,NULL,NULL,
#endif

#ifdef  ETHER
	EADDR_LEN,IP_TYPE,ARP_TYPE,1,Ether_bdcst,pether,gether, /* ARP_ETHER */
#else
	0, 0, 0, 0, NULLCHAR,NULL,NULL,
#endif

	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_EETHER */

#ifdef  AX25
	AXALEN, PID_IP, PID_ARP, 10, Ax25multi[0], pax25, setcall,
#else
	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_AX25 */
#endif

	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_PRONET */

	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_CHAOS */

	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_IEEE802 */

#ifdef  ARCNET
	AADDR_LEN, ARC_IP, ARC_ARP, 1, ARC_bdcst, parc, garc, /* ARP_ARCNET */
#else
	0, 0, 0, 0, NULLCHAR,NULL,NULL,
#endif

	0, 0, 0, 0, NULLCHAR,NULL,NULL,                 /* ARP_APPLETALK */
};
/* Get rid of trace references in Iftypes[] if TRACE is turned off */
#ifndef TRACE
#define ip_dump         NULLVFP
#define ax25_dump       NULLVFP
#define ki_dump         NULLVFP
#define sl_dump         NULLVFP
#define ether_dump      NULLVFP
#define ppp_dump        NULLVFP
#define arc_dump        NULLVFP
#endif  /* TRACE */

/* Table of interface types. Contains most device- and encapsulation-
 * dependent info
 */
struct iftype Iftypes[] = {
	/* This entry must be first, since Loopback refers to it */
	"None",         NULL,           NULL,           NULL,
	NULL,           CL_NONE,        0,

#ifdef  AX25
	"AX25",         ax_send,        ax_output,      pax25,
	setcall,        CL_AX25,        AXALEN,
#endif

#ifdef  SLIP
	"SLIP",         slip_send,      NULL,           NULL,
	NULL,           CL_NONE,        0,
#endif

#ifdef  ETHER
	/* Note: NULL is specified for the scan function even though
	 * gether() exists because the packet drivers don't support
	 * address setting.
	 */
	"Ethernet",     enet_send,      enet_output,    pether,
	NULL,           CL_ETHERNET,    EADDR_LEN,
#endif

#ifdef  NETROM
	"NETROM",       nr_send,        NULL,           pax25,
	setcall,        CL_NETROM,      AXALEN,
#endif

#ifdef  SLFP
	"SLFP",         pk_send,        NULL,           NULL,
	NULL,           CL_NONE,        0,
#endif

#ifdef  PPP
	"PPP",          ppp_send,       ppp_output,     NULL,
	NULL,           CL_PPP, 0,
#endif

	NULLCHAR
};

/* daemons to be run at startup time */
struct daemon Daemons[] = {
	"killer",       1024,   killer,
/*      "gcollect",     256,    gcollect,       */
	"timer",        16000,  timerproc,
	"network",      16000,  network,
/*      "keyboard",     250,    keyboard,       */
	NULLCHAR,       0,      NULLVFP
};

#if     0
void (*Listusers) __ARGS((FILE *network)) = listusers;
#else
void (*Listusers) __ARGS((FILE *network)) = NULL;
#endif  /* MAILBOX */

#ifndef BOOTP
int WantBootp = 0;

int
bootp_validPacket(ip,bpp)
struct ip *ip;
struct mbuf **bpp;
{
	return 0;
}
#endif  /* BOOTP */

/* Packet tracing stuff */
#ifdef  TRACE
#include "trace.h"

/* Protocol tracing function pointers. Matches list of class definitions
 * in pktdrvr.h.
 */
struct trace Tracef[] = {
	NULLFP,         ip_dump,        /* CL_NONE */

#ifdef  ETHER                           /* CL_ETHERNET */
	ether_forus,    ether_dump,
#else
	NULLFP,         NULLVFP,
#endif  /* ETHER */

	NULLFP,         NULLVFP,        /* CL_PRONET_10 */
	NULLFP,         NULLVFP,        /* CL_IEEE8025 */
	NULLFP,         NULLVFP,        /* CL_OMNINET */

#ifdef  APPLETALK
	at_forus,       at_dump,        /* CL_APPLETALK */
#else
	NULLFP,         NULLVFP,
#endif  /* APPLETALK */

#ifdef VJCOMPRESS
	NULLFP,         sl_dump,        /* CL_SERIAL_LINE */
#else
	NULLFP,         ip_dump,        /* CL_SERIAL_LINE */
#endif
	NULLFP,         NULLVFP,        /* CL_STARLAN */

#ifdef  ARCNET
	arc_forus,      arc_dump,       /* CL_ARCNET */
#else
	NULLFP,         NULLVFP,
#endif  /* ARCNET */

#ifdef  AX25
	ax_forus,       ax25_dump,      /* CL_AX25 */
#else
	NULLFP,         NULLVFP,
#endif  /* AX25 */

#ifdef  KISS                            /* CL_KISS */
	ki_forus,       ki_dump,
#else
	NULLFP,         NULLVFP,
#endif  /* KISS */

	NULLFP,         NULLVFP,        /* CL_IEEE8023 */
	NULLFP,         NULLVFP,        /* CL_FDDI */
	NULLFP,         NULLVFP,        /* CL_INTERNET_X25 */
	NULLFP,         NULLVFP,        /* CL_LANSTAR */
	NULLFP,         ip_dump,        /* CL_SLFP */

#ifdef  NETROM                          /* CL_NETROM */
	NULLFP,         ip_dump,
#else
	NULLFP,         NULLVFP,
#endif

#ifdef PPP
	NULLFP,         ppp_dump,       /* CL_PPP */
#else
	NULLFP,         NULLVFP,
#endif /* PPP */
};

#else   /* TRACE */

/* Stub for packet dump function */
void
dump(iface,direction,type,bp)
struct iface *iface;
int direction;
unsigned type;
struct mbuf *bp;
{
}
void
raw_dump(iface,direction,bp)
struct iface *iface;
int direction;
struct mbuf *bp;
{
}

#endif  /* TRACE */

#ifndef TRACEBACK
void
stktrace()
{
}
#endif

#ifndef LZW
void
lzwfree(up)
struct usock *up;
{
}
#endif

#ifdef  AX25
/* Hooks for passing incoming AX.25 data frames to network protocols */
static void
axip(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
	int32 ipaddr;
	if (bp && bp->cnt >= 20 && (ipaddr = get32(bp->data + 12)))
		arp_add(ipaddr, ARP_AX25, src, 0);
	(void)ip_route(iface,bp,mcast);
}

static void
axarp(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
	(void)arp_input(iface,bp);
}

#ifdef  NETROM
static void
axnr(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
	nr3_input(bp, src);
}

#endif  /* NETROM */
#endif  /* AX25 */

#ifndef RIP
/* Stub for routing timeout when RIP is not configured -- just remove entry */
void
rt_timeout(s)
void *s;
{
	struct route *stale = (struct route *)s;

	rt_drop(stale->target,stale->bits);
}
#endif

/* Stubs for demand dialer */
#ifndef DIALER
void
dialer_kick(asyp)
struct asy *asyp;
{
}
#endif

/* Stubs for Van Jacobsen header compression */
#if !defined(VJCOMPRESS) && defined(ASY)
struct slcompress *
slhc_init(rslots,tslots)
int rslots;
int tslots;
{
	return NULLSLCOMPR;
}
int
slhc_compress(comp, bpp, compress_cid)
struct slcompress *comp;
struct mbuf **bpp;
int compress_cid;
{
	return SL_TYPE_IP;
}
int
slhc_uncompress(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	return -1;      /* Can't decompress */
}
void
shlc_i_status(comp)
struct slcompress *comp;
{
}
void
shlc_o_status(comp)
struct slcompress *comp;
{
}
int
slhc_remember(comp, bpp)
struct slcompress *comp;
struct mbuf **bpp;
{
	return -1;
}
#endif /* !defined(VJCOMPRESS) && defined(ASY) */

#ifdef  SERVERS
static int
dostart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Startcmds,argc,argv,p);
}
static int
dostop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Stopcmds,argc,argv,p);
}
#endif  /* SERVERS */

static int
dostatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
  char *my_argv[3];

  Shortstatus = 1;
  my_argv[1] = "status";
  my_argv[2] = NULLCHAR;

#ifdef  AX25
  puts("------------------------------------ AX.25 ------------------------------------");
  my_argv[0] = "ax25";
  doax25(2, my_argv, p);
#endif

#ifdef  NETROM
  puts("------------------------------------ NETROM -----------------------------------");
  my_argv[0] = "netrom";
  donetrom(2, my_argv, p);
#endif

  puts("------------------------------------- TCP -------------------------------------");
  my_argv[0] = "tcp";
  dotcp(2, my_argv, p);

  puts("------------------------------------- UDP -------------------------------------");
  my_argv[0] = "udp";
  doudp(2, my_argv, p);

  Shortstatus = 0;
  return 0;
}

