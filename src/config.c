/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/config.c,v 1.2 1990/09/17 11:51:18 deyke Exp $ */

/* Stuff heavily dependent on the configuration info in config.h */

#include <stdio.h>
/* #include <dos.h> */
#include "global.h"
#include "mbuf.h"
/* #include "proc.h" */
#include "cmdparse.h"
#include "config.h"
/* #include "daemon.h" */
#include "timer.h"
#include "iface.h"
#include "pktdrvr.h"
#include "slip.h"
/* #include "usock.h" */
#include "kiss.h"
#include "enet.h"
#include "ax25.h"
/* #include "lapb.h" */
#include "netrom.h"
/* #include "nr4.h" */
#include "arp.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "udp.h"
#include "commands.h"

static int dostart __ARGS((int argc,char *argv[],void *p));
static int dostop __ARGS((int argc,char *argv[],void *p));
static int dostatus __ARGS((int argc,char *argv[],void *p));

struct mbuf *Hopper;
unsigned Nsessions = NSESSIONS;

/* Free memory threshold, below which things start to happen to conserve
 * memory, like garbage collection, source quenching and refusing connects
 */
int32 Memthresh = MTHRESH;

/* Transport protocols atop IP */
struct iplink Iplink[] = {
	TCP_PTCL,       tcp_input,
	UDP_PTCL,       udp_input,
	ICMP_PTCL,      icmp_input,
	0,              0
};

/* Transport protocols atop ICMP */
struct icmplink Icmplink[] = {
	TCP_PTCL,       tcp_icmp,
	0,              0
};

/* ARP protocol linkages */
struct arp_type Arp_type[NHWTYPES] = {
#ifdef  NETROM
	AXALEN, 0, 0, 0, NULLCHAR, psax25, setpath,     /* ARP_NETROM */
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
	AXALEN, PID_IP, PID_ARP, 10, (char *)&Ax25_bdcst, psax25, setpath,
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

	NULLCHAR
};

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
/*      "asystat",      doasystat,      0, 0, NULLCHAR, */
#endif
#ifdef  AX25
	"ax25",         doax25,         0, 0, NULLCHAR,
#endif
	"attach",       doattach,       0, 2,
		"attach <hardware> <hw specific options>",
	"bye",          dobye,          0, 0, NULLCHAR,
/* This one is out of alpabetical order to allow abbreviation to "c" */
#ifdef  AX25
	"connect",      doconnect,      1024, 2,"connect callsign [digipeaters]",
#endif
#if     !defined(UNIX) && !defined(AMIGA)
	"cd",           docd,           0, 0, NULLCHAR,
#endif
	"close",        doclose,        0, 0, NULLCHAR,
	"disconnect",   doclose,        0, 0, NULLCHAR,
#ifndef AMIGA
/*      "dir",          dodir,          512, 0, NULLCHAR, /* note sequence */
#endif
/*      "delete",       dodelete,       0, 2, "delete <file>", */
/*      "detach",       dodetach,       0, 2, "detach <interface>", */
/*      "domain",       dodomain,       0, 0, NULLCHAR, */
#ifdef  DRSI
	"drsistat",     dodrstat,       0, 0, NULLCHAR,
#endif
#ifdef  HOPCHECK
	"hopcheck",     dohop,          0, 0, NULLCHAR,
#endif
#ifdef  HS
	"hs",           dohs,           0, 0, NULLCHAR,
#endif
#ifdef  EAGLE
	"eaglestat",    doegstat,       0, 0, NULLCHAR,
#endif
	"echo",         doecho,         0, 0, NULLCHAR,
	"eol",          doeol,          0, 0, NULLCHAR,
#if     !defined(MSDOS)
	"escape",       doescape,       0, 0, NULLCHAR,
#endif
#ifdef  PC_EC
	"etherstat",    doetherstat,    0, 0, NULLCHAR,
#endif
	"exit",         doexit,         0, 0, NULLCHAR,
	"finger",       dofinger,       1024, 2, "finger name@host",
	"ftp",          doftp,          2048, 2, "ftp <address>",
#ifdef HAPN
	"hapnstat",     dohapnstat,     0, 0, NULLCHAR,
#endif
/*      "help",         dohelp,         0, 0, NULLCHAR, */
	"hostname",     dohostname,     0, 0, NULLCHAR,
	"icmp",         doicmp,         0, 0, NULLCHAR,
	"ifconfig",     doifconfig,     0, 0, NULLCHAR,
	"ip",           doip,           0, 0, NULLCHAR,
	"kick",         dokick,         0, 0, NULLCHAR,
	"log",          dolog,          0, 0, NULLCHAR,
#ifdef  MAILBOX
/*      "mbox",         dombox,         0, 0, NULLCHAR, */
#endif
	"mem",          domem,          0, 0, NULLCHAR,
	"mail_daemon",  mail_daemon,    0, 0, NULLCHAR,
#ifdef  AX25
	"mode",         domode,         0, 2, "mode <interface>",
#endif
/*      "mkdir",        domkd,          0, 2, "mkdir <directory>", */
/*      "more",         domore,         512, 2, "more <filename>", */
#ifdef  NETROM
	"netrom",       donetrom,       0, 0, NULLCHAR,
#ifdef  NRS
	"nrstat",       donrstat,       0, 0, NULLCHAR,
#endif  /* NRS */
#endif  /* NETROM */
	"param",        doparam,        0, 2, "param <interface>",
	"ping",         doping,         512, 0, NULLCHAR,
/*      "ps",           ps,             0, 0, NULLCHAR, */
#if     !defined(UNIX) && !defined(AMIGA)
	"pwd",          docd,           0, 0, NULLCHAR,
#endif
	"record",       dorecord,       0, 0, NULLCHAR,
	"remote",       doremote,       0, 3, "remote [-p port] [-k key] [-a kickaddr] <address> exit|reset|kick",
/*      "rename",       dorename,       0, 3, "rename <oldfile> <newfile>", */
	"reset",        doreset,        0, 0, NULLCHAR,
#ifdef  RIP
	"rip",          dorip,          0, 0, NULLCHAR,
#endif
/*      "rmdir",        dormd,          0, 2, "rmdir <directory>", */
	"route",        doroute,        0, 0, NULLCHAR,
	"rtprio",       dortprio,       0, 0, NULLCHAR,
	"status",       dostatus,       0, 0, NULLCHAR,
	"session",      dosession,      0, 0, NULLCHAR,
#ifdef  SCC
	"sccstat",      dosccstat,      0, 0, NULLCHAR,
#endif
#if     !defined(AMIGA)
	"shell",        doshell,        0, 0, NULLCHAR,
#endif
/*      "smtp",         dosmtp,         0, 0, NULLCHAR, */
/*      "socket",       dosock,         0, 0, NULLCHAR, */
	"source",       dosource,       0, 2, "source <filename>",
#ifdef  SERVERS
	"start",        dostart,        0, 2, "start <servername>",
	"stop",         dostop,         0, 2, "stop <servername>",
#endif
	"stime",        dostime,        0, 0, NULLCHAR,
	"tcp",          dotcp,          0, 0, NULLCHAR,
	"telnet",       dotelnet,       1024, 2, "telnet <address>",
/*      "tip",          dotip,          256, 2, "tip <iface", */
#ifdef  TRACE
	"trace",        dotrace,        0, 0, NULLCHAR,
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
#ifdef  PC_EC
	/* 3-Com Ethernet interface */
	"3c500", ec_attach, 0, 7,
	"attach 3c500 <address> <vector> arpa <label> <buffers> <mtu> [ip_addr]",
#endif
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
	"\nattach scc <devices> init <addr> <spacing> <Aoff> <Boff> <Dataoff>\
\n   <intack> <vec> [p]<clock> [hdwe] [param]\
\nattach scc <chan> slip|kiss|nrs|ax25 <label> <mtu> <speed> <bufsize> [call] ",
#endif
	NULLCHAR,
};

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

	NULLFP,         ip_dump,        /* CL_SERIAL_LINE */
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
#endif  /* TRACE */

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
#ifdef  RIP
	"rip",          doripinit,      0,   0, NULLCHAR,
#endif
/*      "smtp",         smtp1,          256, 0, NULLCHAR, */
#if     defined(MAILBOX)
	"telnet",       telnet1,        256, 0, NULLCHAR,
#endif
/*      "ttylink",      ttylstart,      256, 0, NULLCHAR, */
	"remote",       rem1,           256, 0, NULLCHAR,
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
#ifdef  RIP
	"rip",          doripstop,      0, 0, NULLCHAR,
#endif
/*      "smtp",         smtp0,          0, 0, NULLCHAR, */
#ifdef  MAILBOX
	"telnet",       telnet0,        0, 0, NULLCHAR,
#endif
/*      "ttylink",      ttyl0,          0, 0, NULLCHAR, */
	"remote",       rem0,           0, 0, NULLCHAR,
	NULLCHAR,

};
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
static int
dostatus(argc,argv,p)
int argc;
char *argv[];
void *p;
{
  char *my_argv[3];

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

  return 0;
}
#endif  /* SERVERS */

/* Various configuration-dependent functions */

/* Process packets in the Hopper */
void
network()
{
	struct mbuf *bp;
	struct phdr phdr;

	/* Process the input packet */
	bp = dequeue(&Hopper);
	pullup(&bp,(char *)&phdr,sizeof(phdr));
	if(phdr.iface != NULLIF)
		phdr.iface->rawrecvcnt++;
	dump(phdr.iface,IF_TRACE_IN,phdr.type,bp);
	switch(phdr.type){
#ifdef  KISS
	case CL_KISS:
		kiss_recv(phdr.iface,bp);
		break;
#endif
#ifdef  AX25
	case CL_AX25:
		ax_recv(phdr.iface,bp);
		break;
#endif
#ifdef  ETHER
	case CL_ETHERNET:
		eproc(phdr.iface,bp);
		break;
#endif
#ifdef ARCNET
	case CL_ARCNET:
		aproc(phdr.iface,bp);
		break;
#endif
	/* These types have no link layer protocol at the point when they're
	 * put in the hopper, so they can be handed directly to IP. The
	 * separate types are just for user convenience when running the
	 * "iface" command.
	 */
	case CL_NONE:
	case CL_SERIAL_LINE:
	case CL_SLFP:
		ip_route(phdr.iface,bp,0);
		break;
	default:
		free_p(bp);
		break;
	}
	/* Let everything else run - this keeps the system from wedging
	 * when we're hit by a big burst of packets
	 */
}
