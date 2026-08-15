/* @(#) $Id: config.c,v 1.63 2006/02/12 17:49:57 dl9sau Exp $ */

/* Copyright 1991 Phil Karn, KA9Q
 */

#include "configure.h"

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "lapb.h"
#include "ax25.h"
#include "kiss.h"
#include "nrs.h"
#include "sixpack.h"
#include "netrom.h"
#include "pktdrvr.h"
#include "slip.h"
#include "arp.h"
#include "icmp.h"
#include "cmdparse.h"
#include "commands.h"
#include "remote_net.h"
#include "ax25mail.h"
#include "tipmail.h"
#include "daemon.h"
#include "socket.h"
#include "asy.h"
#include "trace.h"
#include "session.h"
#include "flexnet.h"

static int dostart(int argc,char *argv[],void *p);
static int dostop(int argc,char *argv[],void *p);
static int dostatus(int argc,char *argv[],void *p);

static void axip(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);
static void axarp(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);
static void axnr(struct iface *iface,struct ax25_cb *axp,uint8 *src,
	uint8 *dest,struct mbuf **bp,int mcast);

struct mbuf *Hopper;            /* Queue of incoming packets */
unsigned Nsessions = 20;
int Shortstatus;

/* Command lookup and branch tables */
struct cmds Cmds[] = {
	/* The "go" command must be first */
	{ "",             go,             0, 0, NULL },
	{ "!",            doshell,        0, 0, NULL },
	{ "arp",          doarp,          0, 0, NULL },
	{ "asystat",      doasystat,      0, 0, NULL },
	{ "attach",       doattach,       0, 2, "attach <hardware> <hw specific options>" },
	{ "ax25",         doax25,         0, 0, NULL },
	{ "axip",         doaxip,         0, 0, NULL },
	{ "bye",          dobye,          0, 0, NULL },
/* This one is out of alpabetical order to allow abbreviation to "c" */
	{ "connect",      doconnect,      0, 2,
	  "connect [<port>:]<callsign> [via] [digipeaters] [< <mycall>]" },
	{ "close",        doclose,        0, 0, NULL },
/* This one is out of alpabetical order to allow abbreviation to "d" */
	{ "disconnect",   doclose,        0, 0, NULL },
	{ "delete",       dodelete,       0, 2, "delete <file>" },
	{ "domain",       dodomain,       0, 0, NULL },
	{ "echo",         doecho,         0, 0, NULL },
	{ "eol",          doeol,          0, 0, NULL },
	{ "escape",       doescape,       0, 0, NULL },
	{ "exit",         doexit,         0, 0, NULL },
	{ "finger",       dofinger,       0, 2, "finger name@host" },
	{ "fkey",         dofkey,         0, 3, "fkey <key#> <text>" },
	{ "flexnet",      doflexnet,      0, 0, NULL },
	{ "ftp",          doftp,          0, 2, "ftp <address>" },
	{ "hostname",     dohostname,     0, 0, NULL },
	{ "icmp",         doicmp,         0, 0, NULL },
	{ "ifconfig",     doifconfig,     0, 0, NULL },
	{ "axsock",       doaxsock,       0, 0,
	  "axsock [group <name>|mode <octal>]" },
	{ "ip",           doip,           0, 0, NULL },
	{ "kick",         dokick,         0, 0, NULL },
	{ "ipfilter",     doipfilter,     0, 0, NULL },
	{ "listen",       dolisten,       0, 0,
	  "listen [ax25|netrom] [add|drop] ..." },
	{ "log",          dolog,          0, 0, NULL },
	{ "login",        dologin,        0, 0, NULL },
	{ "memory",       domem,          0, 0, NULL },
	{ "mkdir",        domkd,          0, 2, "mkdir <directory>" },
	{ "netrom",       donetrom,       0, 0, NULL },
	{ "nrstat",       donrstat,       0, 0, NULL },
	{ "param",        doparam,        0, 2, "param <interface>" },
	{ "ping",         doping,         0, 0, NULL },
	{ "ps",           ps,             0, 0, NULL },
	{ "record",       dorecord,       0, 0, NULL },
	{ "remote",       doremote,       0, 3, "remote [-p port] [-k key] [-a kickaddr] <address> exit|reset|kick" },
	{ "rename",       dorename,       0, 3, "rename <oldfile> <newfile>" },
	{ "repeat",       dorepeat,   16000, 2, "repeat [<interval>] <command> [args...]" },
	{ "reset",        doreset,        0, 0, NULL },
	{ "rip",          dorip,          0, 0, NULL },
	{ "rmdir",        dormd,          0, 2, "rmdir <directory>" },
	{ "route",        doroute,        0, 0, NULL },
	{ "status",       dostatus,       0, 0, NULL },
	{ "session",      dosession,      0, 0, NULL },
	{ "shell",        doshell,        0, 0, NULL },
	{ "smtp",         dosmtp,         0, 0, NULL },
	{ "sntp",         dosntp,         0, 0, NULL },
	{ "source",       dosource,       0, 2, "source <filename>" },
	{ "start",        dostart,        0, 2, "start <servername>" },
	{ "stop",         dostop,         0, 2, "stop <servername>" },
	{ "tcp",          dotcp,          0, 0, NULL },
	{ "telnet",       dotelnet,       0, 2, "telnet <address>" },
	{ "topt",         dotopt,         0, 0, NULL },
	{ "trace",        dotrace,        0, 0, NULL },
	{ "udp",          doudp,          0, 0, NULL },
	{ "upload",       doupload,       0, 0, NULL },
	{ NULL,           NULL,           0, 0, "Unknown command; type \"?\" for list" }
};

/* List of supported hardware devices */
struct cmds Attab[] = {

	/* Ordinary asynchronous adaptor */
	{ "asy", asy_attach, 0, 8,
	"attach asy <address> <vector> slip|vjslip|ax25ui|ax25i|nrs <label> <buffers> <mtu> <speed> [ip_addr]" },

	/* fake netrom interface */
	{ "netrom", nr_attach, 0, 1,
	"attach netrom [ip_addr]" },

	{ "axip", axip_attach, 0, 1,
	"attach axip [<label> [ip|udp|ip6|udp6 [protocol|port]]]" },

	{ "ipip", ipip_attach, 0, 1,
	"attach ipip [<label> [ip|udp [protocol|port]]]" },

#if HAS_NI
	{ "ni", ni_attach, 0, 3,
	"attach ni <label> <dest> [mask]" },
#endif

#if defined __FreeBSD__ || defined __MACOSX__
	{ "tun", tun_attach, 0, 3,
	"attach tun <label> <mtu>" },
#endif

#ifdef	linux
	{ "kernel", krnlif_attach, 0, 2,
	"attach kernel <iface> [label] [nopromisc]" },
#endif

	{ "ethertap", ethertap_attach, 0, 2,
	"attach ethertap <label> [mtu]" },

	{ NULL }
};

/* "start" and "stop" subcommands */
static struct cmds Startcmds[] = {
	{ "ax25",         ax25start,      0, 0, NULL },
	{ "axtcp",        axtcpstart,     0, 0, "start axtcp [<port>]" },
	{ "discard",      dis1,           0, 0, NULL },
	{ "domain",       domain1,        0, 0, NULL },
	{ "echo",         echo1,          0, 0, NULL },
	{ "ftp",          ftpstart,       0, 0, NULL },
	{ "tcpgate",      tcpgate1,       0, 2, "start tcpgate <tcp port> [<host:service>]" },
	{ "netrom",       nr4start,       0, 0, NULL },
	{ "rip",          doripinit,      0, 0, NULL },
	{ "sntp",         sntp1,          0, 0, NULL },
	{ "telnet",       telnet1,        0, 0, NULL },
	{ "time",         time1,          0, 0, NULL },
	{ "remote",       rem1,           0, 0, NULL },
	{ NULL }
};

static struct cmds Stopcmds[] = {
	{ "ax25",         ax250,          0, 0, NULL },
	{ "axtcp",        axtcp0,         0, 0, NULL },
	{ "discard",      dis0,           0, 0, NULL },
	{ "domain",       domain0,        0, 0, NULL },
	{ "echo",         echo0,          0, 0, NULL },
	{ "ftp",          ftp0,           0, 0, NULL },
	{ "netrom",       nr40,           0, 0, NULL },
	{ "rip",          doripstop,      0, 0, NULL },
	{ "sntp",         sntp0,          0, 0, NULL },
	{ "telnet",       telnet0,        0, 0, NULL },
	{ "time",         time0,          0, 0, NULL },
	{ "remote",       rem0,           0, 0, NULL },
	{ NULL }
};

/* TCP port numbers to be considered "interactive" by the IP routing
 * code and given priority in queueing
 */
int Tcp_interact[] = {
	IPPORT_FTP,     /* FTP control (not data!) */
	IPPORT_TELNET,  /* Telnet */
	6000,           /* X server 0 */
	IPPORT_LOGIN,   /* BSD rlogin */
	IPPORT_MTP,     /* Secondary telnet */
	-1
};

/* Transport protocols atop IP */
struct iplink Iplink[] = {
	{ TCP_PTCL,       "TCP",  tcp_input,      tcp_dump },
	{ UDP_PTCL,       "UDP",  udp_input,      udp_dump },
	{ ICMP_PTCL,      "ICMP", icmp_input,     icmp_dump },
	{ IP_PTCL,        "IP",   ipip_recv,      ipip_dump },
	{ IP4_PTCL,       "IP",   ipip_recv,      ipip_dump },
	{ 0,              NULL,   NULL,           NULL }
};

/* Transport protocols atop ICMP */
struct icmplink Icmplink[] = {
	{ TCP_PTCL,       tcp_icmp },
	{ 0,              0 }
};

/* Linkage to network protocols atop ax25 */
struct axlink Axlink[] = {
	{ PID_IP,         axip },
	{ PID_ARP,        axarp },
	{ PID_FLEXNET,    flexnet_input },
	{ PID_NETROM,     axnr },
#ifdef	AX25_VJCOMP
        { PID_VJCOMP,     ax_rx_vjcomp },
	{ PID_VJUNCOMP,   ax_rx_vjuncomp },
#endif
	{ 0,              NULL }
};

/* ARP protocol linkages, indexed by arp's hardware type */
struct arp_type Arp_type[NHWTYPES] = {
	{ AXALEN, 0, 0, 0, NULL, pax25, setcall },          /* ARP_NETROM */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_ETHER */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_EETHER */

	{ AXALEN, PID_IP, PID_ARP, 10, Ax25multi[0], pax25, setcall }, /* ARP_AX25 */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_PRONET */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_CHAOS */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_IEEE802 */

	{ 0, 0, 0, 0, NULL,NULL,NULL },                     /* ARP_ARCNET */

	{ 0, 0, 0, 0, NULL,NULL,NULL }                      /* ARP_APPLETALK */
};

/* Table of interface types. Contains most device- and encapsulation-
 * dependent info
 */
struct iftype Iftypes[] = {

	/* This entry must be first, since Loopback refers to it */
	{
		"None",         nu_send,        nu_output,      NULL,
		NULL,           CL_NONE,        0,              ip_proc,
		NULL,           ip_dump,        NULL,           NULL
	},

	{
		"AX25UI",       axui_send,      ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         ax_recv,
		ax_forus,       ax25_dump,      NULL,           NULL
	},

	{
		"AX25I",        axi_send,       ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         ax_recv,
		ax_forus,       ax25_dump,      NULL,           NULL
	},

	/* The AX.25 side of these is the same as above - what differs is the
	 * framing on the wire, which asy_attach() picks from Asymode by the
	 * very same name.  One name, looked up in two tables.
	 */
	{
		"6PACKUI",      axui_send,      ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         ax_recv,
		ax_forus,       ax25_dump,      NULL,           NULL
	},

	{
		"6PACKI",       axi_send,       ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         ax_recv,
		ax_forus,       ax25_dump,      NULL,           NULL
	},

	{
		"KISSUI",       axui_send,      ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         kiss_recv,
		ki_forus,       ki_dump,        NULL,           NULL
	},

	{
		"KISSI",        axi_send,       ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         kiss_recv,
		ki_forus,       ki_dump,        NULL,           NULL
	},

	{
		"SLIP",         slip_send,      NULL,           NULL,
		NULL,           CL_NONE,        0,              ip_proc,
		NULL,           ip_dump,        NULL,           NULL
	},

	{
		"VJSLIP",       vjslip_send,    NULL,           NULL,
		NULL,           CL_NONE,        0,              ip_proc,
		NULL,           sl_dump,        NULL,           NULL
	},

	{
		"NETROM",       nr_send,        NULL,           pax25,
		setcall,        CL_NETROM,      AXALEN,         NULL,
		NULL,           ip_dump,        NULL,           NULL
	},

	{
		"NRS",          axui_send,      ax_output,      pax25,
		setcall,        CL_AX25,        AXALEN,         ax_recv,
		ax_forus,       ax25_dump,      NULL,           NULL
	},

	{
		NULL,           NULL,           NULL,           NULL,
		NULL,           -1,             0,              NULL,
		NULL,           NULL,           NULL,           NULL
	}
};

/* Asynchronous interface mode table */
struct asymode Asymode[] = {
	{ "SLIP",         FR_END,         slip_init,      slip_free },
	{ "VJSLIP",       FR_END,         slip_init,      slip_free },
	{ "AX25UI",       FR_END,         kiss_init,      kiss_free },
	{ "AX25I",        FR_END,         kiss_init,      kiss_free },
	{ "KISSUI",       FR_END,         kiss_init,      kiss_free },
	{ "KISSI",        FR_END,         kiss_init,      kiss_free },
	{ "6PACKUI",      SIXP_CMD_SEOF,  sixpack_init,   sixpack_free },
	{ "6PACKI",       SIXP_CMD_SEOF,  sixpack_init,   sixpack_free },
	{ "NRS",          ETX,            nrs_init,       nrs_free },
	{ NULL }
};

/* daemons to be run at startup time */
struct daemon Daemons[] = {
	{ "killer",        1024,  killer },
	{ "timer",        16000,  timerproc },
	{ "network",      16000,  network },
	{ NULL,               0,  NULL }
};

/* Packet tracing stuff */
#include "trace.h"

/* Hooks for passing incoming AX.25 data frames to network protocols */
static void
axip(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){

	int32 ipaddr = 0;
	struct arp_tab *ap;
	uint16 ip_len = 0;
	uint8 hwaddr[AXALEN];

	if(!mcast &&
	   bpp &&
	   *bpp &&
	   (*bpp)->cnt >= IPLEN &&
	   (ipaddr = get32((*bpp)->data + 12)) &&
	   (ip_len = ((*bpp)->data[0] & 0xf) << 2) >= IPLEN &&
	   !cksum(NULL, *bpp, ip_len)){
		iface->flags |= NO_RT_ADD;
		addrcp(hwaddr, src);
		if((ap = revarp_lookup(ARP_AX25,hwaddr)) != NULL &&
		   ap->state == ARP_VALID &&
		   !run_timer(&ap->timer) &&
		   ap->ip_addr != ipaddr){
			rt_add(ipaddr,32,ap->ip_addr,iface,1L,0x7fffffff/1000,0);
		}else if((ap = arp_lookup(ARP_AX25,ipaddr)) == NULL ||
		   ap->state != ARP_VALID ||
		   run_timer(&ap->timer)){
			rt_add(ipaddr,32,0L,iface,1L,0x7fffffff/1000,0);
			arp_add(ipaddr, ARP_AX25, hwaddr, 0);
		}
	}
	ip_route(iface,bpp,mcast);
}

static void
axarp(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){
	arp_input(iface,bpp);
}

static void
axnr(
struct iface *iface,
struct ax25_cb *axp,
uint8 *src,
uint8 *dest,
struct mbuf **bpp,
int mcast
){
	nr3_input(src,bpp);
}

static int
dostart(
int argc,
char *argv[],
void *p)
{
	return subcmd(Startcmds,argc,argv,p);
}
static int
dostop(
int argc,
char *argv[],
void *p)
{
	return subcmd(Stopcmds,argc,argv,p);
}

static int
dostatus(
int argc,
char *argv[],
void *p)
{
  char *my_argv[3];

  Shortstatus = 1;
  my_argv[1] = "status";
  my_argv[2] = NULL;

  puts("------------------------------------ AX.25 ------------------------------------");
  my_argv[0] = "ax25";
  doax25(2, my_argv, p);

  puts("------------------------------------ NETROM -----------------------------------");
  my_argv[0] = "netrom";
  donetrom(2, my_argv, p);

  puts("------------------------------------- TCP -------------------------------------");
  my_argv[0] = "tcp";
  dotcp(2, my_argv, p);

  puts("------------------------------------- UDP -------------------------------------");
  my_argv[0] = "udp";
  doudp(2, my_argv, p);

  Shortstatus = 0;
  return 0;
}
