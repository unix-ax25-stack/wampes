/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.c,v 1.6 1990/08/23 17:33:32 deyke Exp $ */

/* Main network program - provides both client and server functions */

#define HOSTNAMELEN 64
unsigned restricted_dev=1000;
extern char *Startup;   /* File to read startup commands from */
#include <stdio.h>
#include <time.h>
#ifdef  __TURBOC__
#include <io.h>
#endif
#include <string.h>
#include "config.h"
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "netuser.h"
#include "timer.h"
#include "icmp.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "ax25.h"
#include "netrom.h"
#include "ftp.h"
#include "telnet.h"
#include "remote.h"
#include "session.h"
#include "cmdparse.h"
#include "commands.h"

#ifdef  ASY
#include "asy.h"
#include "slip.h"
#endif

#ifdef  NRS
#include "nrs.h"
#endif

#ifdef  SLFP
/* #include "slfp.h" */
#endif

#ifdef UNIX             /* BSD or SYS5 */
#include "unix.h"
#endif

#ifdef AMIGA
/* #include "amiga.h" */
#endif

#ifdef MAC
/* #include "mac.h" */
#endif

#ifdef MSDOS
#include "asy.h"
#endif

#ifdef  ATARI_ST
/* #include "st.h" */

#ifdef  LATTICE
long _MNEED = 100000L;          /* Reserve RAM for subshell... */
long _32K = 0x8000;             /* For GST Linker (Don't ask me! -- hyc) */
#endif

#ifdef  MWC
long    _stksize = 16384L;      /* Fixed stack size... -- hyc */
#endif
#endif  /* ATARI_ST */

#ifdef  SYS5
/* #include <signal.h> */
/* int     background = 0; */
#endif

#ifdef  TRACE
#include "trace.h"
/* Dummy structure for loopback tracing */
struct iface loopback = { NULLIF, "loopback" };
#endif

extern struct iface *Ifaces;
extern struct mbuf *loopq;
extern FILE *trfp;
extern char trname[];
extern long currtime;

int debug;
int mode;
FILE *logfp;
char Badhost[] = "Unknown host %s\n";
char Hostname[HOSTNAMELEN];
unsigned nsessions = NSESSIONS;
int32 resolve();
int16 lport = 1001;
char prompt[] = "%s> ";
char Nospace[] = "No space!!\n";        /* Generic malloc fail message */
#ifdef  SYS5
int io_active = 0;
#endif

#if ((!defined(MSDOS) && !defined(ATARI_ST)) || defined(PC9801))        /* PC/ST uses F-10 key always */
static char escape = 0x1d;      /* default escape character is ^] */
#endif

static void showtrace __ARGS((struct iface *ifp));
static int dostatus __ARGS((int argc, char *argv [], void *p));
static int dosource __ARGS((int argc, char *argv [], void *p));
static int dortprio __ARGS((int argc, char *argv [], void *p));

/* Command lookup and branch table */

int  dobye();
int  doforward();
int  dostart();
int  dostime();
int  dostop();
int  mail_daemon();
int  memstat();

#ifdef ETHER
int doetherstat();
#endif
#ifdef EAGLE
int doegstat();
#endif
#ifdef HAPN
int dohapnstat();
#endif

static struct cmds cmds[] = {
	/* The "go" command must be first */
	"",             go,             0, 0, NULLCHAR,
#if     (MAC && APPLETALK)
	"applestat",    doatstat,       0, 0, NULLCHAR,
#endif
#if     (AX25 || ETHER || APPLETALK)
	"arp",          doarp,          0, 0, NULLCHAR,
#endif
#ifdef  AX25
	"ax25",         doax25,         0, 0, NULLCHAR,
#endif
	"attach",       doattach,       0, 2,
		"attach <hardware> <hw specific options>",
	"bye",          dobye,          0, 0, NULLCHAR,
/* This one is out of alpabetical order to allow abbreviation to "c" */
#ifdef  AX25
	"connect",      doconnect,      0, 2,"connect callsign [digipeaters]",
#endif
#ifndef UNIX    /* BSD or SYS5 */
	"cd",           docd,           0, 0, NULLCHAR,
#endif
	"close",        doclose,        0, 0, NULLCHAR,
	"disconnect",   doclose,        0, 0, NULLCHAR,
#ifdef  EAGLE
	"eaglestat",    doegstat,       0, 0, NULLCHAR,
#endif
	"echo",         doecho,         0, 0, NULLCHAR,
	"eol",          doeol,          0, 0, NULLCHAR,
#if     ((!defined(MSDOS) && !defined(ATARI_ST)) || defined(PC9801))
	"escape",       doescape,       0, 0, NULLCHAR,
#endif
#ifdef  PC_EC
	"etherstat",    doetherstat,    0, 0, NULLCHAR,
#endif
	"exit",         doexit,         0, 0, NULLCHAR,
	"finger",       dofinger,       0, 0, NULLCHAR,
	"forward",      doforward,      0, 0, NULLCHAR,
	"ftp",          doftp,          0, 2, "ftp <address>",
#ifdef HAPN
	"hapnstat",     dohapnstat,     0, 0, NULLCHAR,
#endif
	"hostname",     dohostname,     0, 0, NULLCHAR,
	"kick",         dokick,         0, 0, NULLCHAR,
	"log",          dolog,          0, 0, NULLCHAR,
	"ip",           doip,           0, 0, NULLCHAR,
	"memstat",      memstat,        0, 0, NULLCHAR,
	"mail_daemon",  mail_daemon,    0, 0, NULLCHAR,
#ifdef  AX25
	"mode",         domode,         0, 2, "mode <interface>",
#endif
#ifdef  MULPORT
	"mulport",      mulport,        0, 2, "mulport <on|off>",
#endif
#ifdef  NETROM
	"netrom",       donetrom,       0, 0, NULLCHAR,
#ifdef  NRS
	"nrstat",       donrstat,       0, 0, NULLCHAR,
#endif
#endif
	"param",        doparam,        0, 2, "param <interface>",
	"ping",         doping,         0, 0, NULLCHAR,
#ifndef UNIX /* BSD or SYS5 */
	"pwd",          docd,           0, 0, NULLCHAR,
#endif
	"record",       dorecord,       0, 0, NULLCHAR,
	"remote",       doremote,       0, 3, "remote [-p port] [-k key] [-a kickaddr] <address> exit|reset|kick",
	"reset",        doreset,        0, 0, NULLCHAR,
	"route",        doroute,        0, 0, NULLCHAR,
	"rtprio",       dortprio,       0, 0, NULLCHAR,
	"status",       dostatus,       0, 0, NULLCHAR,
	"session",      dosession,      0, 0, NULLCHAR,
	"source",       dosource,       0, 2, "source <filename>",
#ifdef  SERVERS
	"start",        dostart,        0, 2, "start <servername>",
	"stime",        dostime,        0, 0, NULLCHAR,
	"stop",         dostop,         0, 2, "stop <servername>",
#endif
	"tcp",          dotcp,          0, 0, NULLCHAR,
	"telnet",       dotelnet,       0, 2, "telnet <address>",
#ifdef  TRACE
	"trace",        dotrace,        0, 0, NULLCHAR,
#endif
	"udp",          doudp,          0, 0, NULLCHAR,
	"upload",       doupload,       0, 0, NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0,
		"Unknown command; type \"?\" for list"
};

#ifdef  SERVERS
/* "start" and "stop" subcommands */
int ftp1(),tn1();
int axserv_start(),tcpgate1();
int finger1();

static struct cmds startcmds[] = {
	"ax25",         axserv_start,   0, 0, NULLCHAR,
	"discard",      dis1,           0, 0, NULLCHAR,
	"echo",         echo1,          0, 0, NULLCHAR,
   /*** "finger",       finger1,        0, 0, NULLCHAR, ***/
	"ftp",          ftp1,           0, 0, NULLCHAR,
	"tcpgate",      tcpgate1,       0, 2, "start tcpgate <tcp port> [<host:service>]",
	"telnet",       tn1,            0, 0, NULLCHAR,
	"remote",       rem1,           0, 0, NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0, NULLCHAR
};
int tn0();
int axserv_stop();
int finger0();

static struct cmds stopcmds[] = {
	"ax25",         axserv_stop,    0, 0, NULLCHAR,
	"discard",      dis0,           0, 0, NULLCHAR,
	"echo",         echo0,          0, 0, NULLCHAR,
   /*** "finger",       finger0,        0, 0, NULLCHAR, ***/
	"ftp",          ftp0,           0, 0, NULLCHAR,
	"telnet",       tn0,            0, 0, NULLCHAR,
	"remote",       rem0,           0, 0, NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0, NULLCHAR
};
#endif

main(argc,argv)
int argc;
char *argv[];
{
	static char inbuf[BUFSIZ];      /* keep it off the stack */
	int c;
	char *ttybuf,*fgets();
	int16 cnt;
	int ttydriv();
	int cmdparse();
	void check_time();
	FILE *fp;
	struct iface *ifp;
	struct mbuf *bp;

	debug = (argc >= 2);
	ioinit();
	netrom_initialize();
	remote_net_initialize();
#if     (defined(UNIX) || defined(AMIGA) || defined(MAC))
#else
	chktasker();
#endif
#ifdef  MSDOS
	printf("KA9Q Internet Protocol Package, v%s DS = %x\n",Version,
		getds());
#else
	printf("KA9Q Internet Protocol Package, v%s\n",Version);
#endif

	printf("Copyright 1988 by Phil Karn, KA9Q\n");
	sessions = (struct session *)calloc(nsessions,sizeof(struct session));
	if(argc > 1){
		/* Read startup file named on command line */
		fp = fopen(argv[1],"r");
	} else {
		fp = fopen(Startup,"r");
	}
	if(fp != NULLFILE){
		while(fgets(inbuf,BUFSIZ,fp) != NULLCHAR){
			cmdparse(cmds,inbuf,NULL);
		}
		fclose(fp);
	}
	cmdmode();

	/* Main commutator loop */
	for(;;){
		/* Process any keyboard input */
		while((c = kbread()) != -1){
#if     (defined(MSDOS) || defined(ATARI_ST))
			/* c == -2 means the command escape key (F10) */
			if(c == -2){
				if(mode != CMD_MODE){
					printf("\n");
					cmdmode();
				}
				continue;
			}
#endif
#ifdef SYS5
			if(c == escape && escape != 0){
				ttydriv('\r', &ttybuf);
				mode = CONV_MODE;
				cmdmode();
				continue;
			}
#endif   /* SYS5 */
			if ((cnt = ttydriv(c, &ttybuf)) == 0)
				continue;
			switch(mode){
			case CMD_MODE:
				(void)cmdparse(cmds,ttybuf,NULL);
				fflush(stdout);
				break;
			case CONV_MODE:
#ifdef  false
				if(ttybuf[0] == escape && escape != 0){
					printf("\n");
					cmdmode();
				} else
#endif  /* MSDOS */
					if(current->parse != NULLFP)
						(*current->parse)(ttybuf,cnt);

				break;
			}
			if(mode == CMD_MODE){
				printf(prompt,Hostname);
				fflush(stdout);
			}
		}
		/* Service the loopback queue */
		if((bp = dequeue(&loopq)) != NULLBUF){
			struct ip ip;
#ifdef  TRACE
			dump(&loopback,IF_TRACE_IN,TRACE_IP,bp);
#endif
			/* Extract IP header */
			ntohip(&ip,&bp);
			ip_recv(&ip,bp,0);
		}

		/* Service the interfaces */
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next){
			if(ifp->recv != NULLVFP)
				(*ifp->recv)(ifp);
		}

		/* Service the clock if it has ticked */
		check_time();

#ifdef  MSDOS
		/* Tell DoubleDos to let the other task run for awhile.
		 * If DoubleDos isn't active, this is a no-op
		 */
		giveup();
#else
		/* Wait until interrupt, then do it all over again */
		eihalt();
#endif
	}
}
/* Standard commands called from main */

/* Enter command mode */
int
cmdmode()
{
	if(mode != CMD_MODE){
		mode = CMD_MODE;
		cooked();
		printf(prompt,Hostname);
		fflush(stdout);
	}
	return 0;
}
doexit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(logfp != NULLFILE)
		fclose(logfp);
	iostop();
#ifdef TRACE
	if (trfp != stdout)
	  fclose(trfp);
#endif
	exit(0);
}
int
dohostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *strncpy();

	if(argc < 2)
		printf("%s\n",Hostname);
	else
		strncpy(Hostname,argv[1],HOSTNAMELEN);
	return 0;
}
int
dolog(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *strncpy();

	static char logname[256];
	if(argc < 2){
		if(logfp)
			printf("Logging to %s\n",logname);
		else
			printf("Logging off\n");
		return 0;
	}
	if(logfp){
		fclose(logfp);
		logfp = NULLFILE;
	}
	if(strcmp(argv[1],"stop") != 0){
		strncpy(logname,argv[1],sizeof(logname));
		logfp = fopen(logname,"a+");
	}
	return 0;
}
int
doecho(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	extern int refuse_echo;

	if(argc < 2){
		if(refuse_echo)
			printf("Refuse\n");
		else
			printf("Accept\n");
	} else {
		if(argv[1][0] == 'r')
			refuse_echo = 1;
		else if(argv[1][0] == 'a')
			refuse_echo = 0;
		else
			return -1;
	}
	return 0;
}
/* set for unix end of line for remote echo mode telnet */
int
doeol(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	extern int unix_line_mode;

	if(argc < 2){
		if(unix_line_mode)
			printf("Unix\n");
		else
			printf("Standard\n");
	} else {
		if(strcmp(argv[1],"unix") == 0)
			unix_line_mode = 1;
		else if(strcmp(argv[1],"standard") == 0)
			unix_line_mode = 0;
		else {
			return -1;
		}
	}
	return 0;
}
/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<speed>]
 */
doattach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	extern struct cmds attab[];

	return subcmd(attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
doparam(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;

	for(ifp=Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(strcmp(argv[1],ifp->name) == 0)
			break;
	}
	if(ifp == NULLIF){
		printf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(ifp->ioctl == NULLFP){
		printf("Not supported\n");
		return 1;
	}
	/* Pass rest of args to device-specific code */
	return (*ifp->ioctl)(ifp,argc-2,argv+2);
}
/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
/*VARARGS2*/
void
log(tcb,fmt,arg1,arg2,arg3,arg4)
struct tcb *tcb;
char *fmt;
int32 arg1,arg2,arg3,arg4;
{
	char *cp;

	if(logfp == NULLFILE)
		return;
	cp = ctime(&currtime);
	rip(cp);
    if (tcb)
	fprintf(logfp,"%s %s - ",cp,psocket(&tcb->conn.remote));
    else
	fprintf(logfp,"%s - ",cp);
	fprintf(logfp,fmt,arg1,arg2,arg3,arg4);
	fprintf(logfp,"\n");
	fflush(logfp);
#if     (defined(MSDOS) || defined(ATARI_ST))
	/* MS-DOS doesn't really flush files until they're closed */
	fd = fileno(logfp);
	if((fd = dup(fd)) != -1)
		close(fd);
#endif
}
/* Configuration-dependent code */

/* List of supported hardware devices */
int modem_init(),pc_attach(),at_attach();

#ifdef  EAGLE
int eg_attach();
#endif
#ifdef  HAPN
int hapn_attach();
#endif
#ifdef  PC_EC
int ec_attach();
#endif
#ifdef  PACKET
int pk_attach();
#endif

struct cmds attab[] = {
#ifdef  PC_EC
	/* 3-Com Ethernet interface */
	"3c500", ec_attach, 0, 7,
	"attach 3c500 <address> <vector> arpa <label> <buffers> <mtu>",
#endif
#ifdef  ASY
	/* Ordinary PC asynchronous adaptor */
	"asy", asy_attach, 0, 8,
#ifdef  UNIX
#ifndef SLFP
	"attach asy 0 <ttyname> slip|ax25|nrs <label> <buffers> <mtu> <speed>",
#else
	"attach asy 0 <ttyname> slip|ax25|nrs|slfp <label> <buffers> <mtu> <speed>",
#endif  /* SLFP */
#else
#ifndef SLFP
	"attach asy <address> <vector> slip|ax25|nrs <label> <buffers> <mtu> <speed>",
#else
	"attach asy <address> <vector> slip|ax25|nrs|slfp <label> <buffers> <mtu> <speed>",
#endif  /* SLFP */
#endif
#endif
#ifdef  PC100
	/* PACCOMM PC-100 8530 HDLC adaptor */
	"pc100", pc_attach, 0, 8,
	"attach pc100 <address> <vector> ax25 <label> <buffers> <mtu> <speed>",
#endif
#ifdef  EAGLE
	/* EAGLE RS-232C 8530 HDLC adaptor */
	"eagle", eg_attach, 0, 8,
	"attach eagle <address> <vector> ax25 <label> <buffers> <mtu> <speed>",
#endif
#ifdef  HAPN
	/* Hamilton Area Packet Radio (HAPN) 8273 HDLC adaptor */
	"hapn", hapn_attach, 0, 8,
	"attach hapn <address> <vector> ax25 <label> <rx bufsize> <mtu> csma|full",
#endif
#ifdef  APPLETALK
	/* Macintosh AppleTalk */
	"0", at_attach, 0, 7,
	"attach 0 <protocol type> <device> arpa <label> <rx bufsize> <mtu>",
#endif
#ifdef  PACKET
	/* FTP Software's packet driver spec */
	"packet", pk_attach, 0, 4,
	"attach packet <int#> <label> <buffers> <mtu>",
#endif
	NULLCHAR, NULLFP, 0, 0,
	"Unknown device"
};

/* Protocol tracing function pointers */
#ifdef  TRACE

void (*tracef[])() = {
#ifdef  AX25
	ax25_dump,
#else
	NULLFP,
#endif

#ifdef  ETHER
	ether_dump,
#else
	(void (*)()) NULLFP,
#endif
	ip_dump,

#ifdef  APPLETALK
	at_dump,
#else
	(void (*)()) NULLFP,
#endif

#ifdef  SLFP
	slfp_dump,
#else
	(void (*)()) NULLFP,
#endif
};
#else
int (*tracef[])() = { NULLFP }; /* No tracing at all */
dump(interface,direction,type,bp)
struct iface *interface;
int direction;
unsigned type;
struct mbuf *bp;
{
}
#endif

#ifdef  ASY

/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4"
 * argv[3]: mode, may be:
 *          "slip" (point-to-point SLIP)
 *          "ax25" (AX.25 frame format in SLIP for raw TNC)
 *          "nrs" (NET/ROM format serial protocol)
 *          "slfp" (point-to-point SLFP, as used by the Merit Network and MIT
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional ax.25 callsign (NRS only)
 *          optional command string for modem (SLFP only)
 *          optional modem L.sys style command (SLIP only)
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_asy;
	extern struct iface *Ifaces;
	int16 dev;
	int mode;
	int asy_send();
	int asy_ioctl();
	void doslip();
	int asy_stop();
	int ax_send();
	void kiss_recv();
	int kiss_raw();
	int kiss_ioctl();
	int slip_send();
	void slip_recv();
	int slip_raw();

#ifdef  SLFP
	int doslfp();
	int slfp_raw();
	int slfp_send();
	int slfp_recv();
	int slfp_init();
#endif

#ifdef  AX25
	struct ax25_addr addr ;
#endif
	int ax_send(),nrs_raw(),asy_ioctl();

	if(Nasy >= ASY_MAX){
		printf("Too many asynch controllers\n");
		return -1;
	}
	if(strcmp(argv[3],"slip") == 0)
		mode = SLIP_MODE;
#ifdef  AX25
	else if(strcmp(argv[3],"ax25") == 0)
		mode = AX25_MODE;
#endif
#ifdef  NRS
	else if(strcmp(argv[3],"nrs") == 0)
		mode = NRS_MODE;
#endif
#ifdef  SLFP
	else if(strcmp(argv[3],"slfp") == 0)
		mode = SLFP_MODE;
#endif
	else {
		printf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		return(-1);
	}

	dev = Nasy++;

	/* Create interface structure and fill in details */
	if_asy = (struct iface *)calloc(1,sizeof(struct iface));
	if_asy->name = malloc((unsigned)strlen(argv[4])+1);
	strcpy(if_asy->name,argv[4]);
	if_asy->mtu = atoi(argv[6]);
	if_asy->dev = dev;
	if_asy->recv = doslip;
	if_asy->stop = asy_stop;

	switch(mode){
#ifdef  SLIP
	case SLIP_MODE:
		if_asy->ioctl = asy_ioctl;
		if_asy->send = slip_send;
		if_asy->output = NULLFP;        /* ARP isn't used */
		if_asy->raw = slip_raw;
		if_asy->flags = 0;
		Slip[dev].recv = slip_recv;
		break;
#endif
#ifdef  AX25
	case AX25_MODE:  /* Set up a SLIP link to use AX.25 */
		axarp();
		if(mycall.call[0] == '\0'){
			printf("set mycall first\n");
			free(if_asy->name);
			free((char *)if_asy);
			Nasy--;
			return -1;
		}
		if_asy->ioctl = kiss_ioctl;
		if_asy->send = ax_send;
		if_asy->output = ax_output;
		if_asy->raw = kiss_raw;
		if(if_asy->hwaddr == NULLCHAR)
			if_asy->hwaddr = malloc(sizeof(mycall));
		memcpy(if_asy->hwaddr,(char *)&mycall,sizeof(mycall));
		Slip[dev].recv = kiss_recv;
		break;
#endif
#ifdef  NRS
	case NRS_MODE: /* Set up a net/rom serial interface */
		if(argc < 9){
			/* no call supplied? */
			if(mycall.call[0] == '\0'){
				/* try to use default */
				printf("set mycall first or specify in attach statement\n");
				return -1;
			} else
				addr = mycall;
		} else {
			/* callsign supplied on attach line */
			if(setcall(&addr,argv[8]) == -1){
				printf ("bad callsign on attach line\n");
				free(if_asy->name);
				free((char *)if_asy);
				Nasy--;
				return -1;
			}
		}
		if_asy->recv = nrs_recv;
		if_asy->ioctl = asy_ioctl;
		if_asy->send = ax_send;
		if_asy->output = ax_output;
		if_asy->raw = nrs_raw;
		if(if_asy->hwaddr == NULLCHAR)
			if_asy->hwaddr = malloc(sizeof(addr));
		memcpy(if_asy->hwaddr,(char *)&addr,sizeof(addr));
		Nrs[dev].iface = if_asy;
		break;
#endif
#ifdef  SLFP
	case SLFP_MODE:
		if_asy->ioctl = asy_ioctl;
		if_asy->send = slfp_send;
		if_asy->recv = doslfp;
		if_asy->output = NULLFP;        /* ARP isn't used */
		if_asy->raw = slfp_raw;
		if_asy->flags = 0;
		slfp[dev].recv = slfp_recv;
		break;
#endif
	}
	if_asy->next = Ifaces;
	Ifaces = if_asy;
	asy_init(dev,argv[1],argv[2],(unsigned)atoi(argv[5]));
	asy_speed(dev,atoi(argv[7]));
/*
 * optional SLIP modem command?
 */
#if defined(SLIP) && defined(MODEM_CALL)
	if((mode == SLIP_MODE) && (argc > 8)) {
	    restricted_dev=dev;
	    if((modem_init(dev,argc-8,argv+8)) == -1) {
		printf("\nModem command sequence failed.\n");
		asy_stop(if_asy);
		Ifaces = if_asy->next;
		free(if_asy->name);
		free((char *)if_asy);
		Nasy--;
		restricted_dev=1000;
		return -1;
	    }
	    restricted_dev=1000;
	    return 0;
	}
#endif
#ifdef  SLFP
	if(mode == SLFP_MODE)
	    if(slfp_init(if_asy, argc>7?argv[8]:NULLCHAR) == -1) {
		printf("Request for IP address failed.\n");
		asy_stop(if_asy);
		Ifaces = if_asy->next;
		free(if_asy->name);
		free((char *)if_asy);
		Nasy--;
		return -1;
	    }
#endif
	return 0;
}
#endif

/* Display or set IP interface control flags */
domode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;

	for(ifp=Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(strcmp(argv[1],ifp->name) == 0)
			break;
	}
	if(ifp == NULLIF){
		printf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		printf("%s: %s\n",ifp->name,
		 (ifp->flags & CONNECT_MODE) ? "VC mode" : "Datagram mode");
		return 0;
	}
	switch(argv[2][0]){
	case 'v':
	case 'c':
	case 'V':
	case 'C':
		ifp->flags = CONNECT_MODE;
		break;
	case 'd':
	case 'D':
		ifp->flags = DATAGRAM_MODE;
		break;
	default:
		printf("Usage: %s [vc | datagram]\n",argv[0]);
		return 1;
	}
	return 0;
}

#ifdef SERVERS
dostart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(startcmds,argc,argv,p);
}
dostop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(stopcmds,argc,argv,p);
}
#endif

#ifdef  TRACE
int
dotrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	extern int notraceall;  /* trace only in command mode? */
	struct iface *ifp;

	if(argc < 2 || strcmp(argv[1], "all") == 0){
		printf("trace mode is %s\n", (notraceall ? "cmdmode" : "allmode"));
		printf("trace to %s\n",trfp == stdout? "console" : trname);
		if(argc >= 3)
			loopback.trace = htoi(argv[2]);
		showtrace(&loopback);
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next) {
			if(argc >= 3)
				ifp->trace = htoi(argv[2]);
			showtrace(ifp);
		}
		return 0;
	}
	if(strcmp("to",argv[1]) == 0){
		if(argc >= 3){
			if(trfp != stdout)
				fclose(trfp);
			if(strncmp(argv[2],"con",3) == 0)
				trfp = stdout;
			else {
				if((trfp = fopen(argv[2],"a")) == NULLFILE){
					printf("%s: cannot open\n",argv[2]);
					trfp = stdout;
					return 1;
				}
			}
			strcpy(trname,argv[2]);
		} else {
			printf("trace to %s\n",trfp == stdout? "console" : trname);
		}
		return 0;
	}
	if(strcmp("loopback",argv[1]) == 0)
		ifp = &loopback;
	else if (strcmp("cmdmode", argv[1]) == 0) {
		notraceall = 1;
		return 0;
	} else if (strcmp("allmode", argv[1]) == 0) {
		notraceall = 0;
		return 0;
	} else
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
			if(strcmp(ifp->name,argv[1]) == 0)
				break;

	if(ifp == NULLIF){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc >= 3)
		ifp->trace = htoi(argv[2]);

	showtrace(ifp);
	return 0;
}
/* Display the trace flags for a particular interface */
static void
showtrace(ifp)
register struct iface *ifp;
{
	if(ifp == NULLIF)
		return;
	printf("%s:",ifp->name);
	if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT)){
		if(ifp->trace & IF_TRACE_IN)
			printf(" input");
		if(ifp->trace & IF_TRACE_OUT)
			printf(" output");

		if(ifp->trace & IF_TRACE_HEX)
			printf(" (Hex/ASCII dump)");
		else if(ifp->trace & IF_TRACE_ASCII)
			printf(" (ASCII dump)");
		else
			printf(" (headers only)");
		printf("\n");
	} else
		printf(" tracing off\n");
	fflush(stdout);
}
#endif

#if     ((!defined(MSDOS) && !defined(ATARI_ST)) || defined(PC9801))
int
doescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		printf("0x%x\n",escape);
	else
		escape = *argv[1];
	return 0;
}
#endif  /* MSDOS */
/* Generate system command packet. Synopsis:
 * remote [-p port#] [-k key] [-a hostname] <hostname> reset|exit|kickme
 */
int
doremote(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket fsock,lsock;
	struct mbuf *bp;
	int c;
	int16 port,len;
	char *key = NULLCHAR;
	int klen;
	int32 addr = 0;
	char *cmd,*host;

	port = IPPORT_REMOTE;   /* Set default */
	optind = 1;             /* reinit getopt() */
	while((c = getopt(argc,argv,"a:p:k:s:")) != EOF){
		switch(c){
		case 'a':
			if (!(addr = resolve(optarg))) {
				printf("Host %s unknown\n",optarg);
				return 1;
			}
			break;
		case 'p':
			port = atoi(optarg);
			break;
		case 'k':
			key = optarg;
			klen = strlen(key);
			break;
		case 's':
			Rempass = strdup(optarg);
			return 0;       /* Only set local password */
		}
	}
	if(optind > argc - 2){
		printf("Insufficient args\n");
		return 1;
	}
	host = argv[optind++];
	cmd = argv[optind];

	if (!(fsock.address = resolve(host))) {
		printf("Host %s unknown\n",host);
		return 1;
	}
	lsock.address = Ip_addr;
	lsock.port = fsock.port = port;

	len = 1;
	/* Did the user include a password or kickme target? */
	if(addr != 0)
		len += sizeof(int32);

	if(key != NULLCHAR)
		len += klen;

	bp = alloc_mbuf(len);
	bp->cnt = 1;

	switch(cmd[0]){
	case 'r':
		bp->data[0] = SYS_RESET;
		if(key != NULLCHAR) {
			strncpy(&bp->data[1],key,klen);
			bp->cnt += klen;
		}
		break;
	case 'e':
		bp->data[0] = SYS_EXIT;
		if(key != NULLCHAR) {
			strncpy(&bp->data[1],key,klen);
			bp->cnt += klen;
		}
		break;
	case 'k':
		bp->data[0] = KICK_ME;
		if(addr != 0) {
			put32(&bp->data[1],addr);
			bp->cnt += sizeof(int32);
		}
		break;
	default:
		printf("Unknown command %s\n",cmd);
		free_p(bp);
		return 1;
	}
	send_udp(&lsock,&fsock,0,0,bp,0,0,0);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int  dostatus(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  char  *my_argv[3];

  my_argv[1] = "status";
  my_argv[2] = NULLCHAR;

#ifdef  AX25
  puts("------------------------------------ AX.25 ------------------------------------");
  my_argv[0] = "ax25";
  doax25(2, my_argv, p);
#endif

  puts("------------------------------------ NETROM -----------------------------------");
  my_argv[0] = "netrom";
  donetrom(2, my_argv, p);

  puts("------------------------------------- TCP -------------------------------------");
  my_argv[0] = "tcp";
  dotcp(2, my_argv, p);

  puts("------------------------------------- UDP -------------------------------------");
  my_argv[0] = "udp";
  doudp(2, my_argv, p);

  puts("----------------------------------- IP/ICMP -----------------------------------");
  my_argv[0] = "ip";
  doip(2, my_argv, p);

  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dosource(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  FILE * fp;
  char  inbuf[BUFSIZ];

  if (!(fp = fopen(argv[1], "r"))) {
    printf("cannot open %s\n", argv[1]);
    return 1;
  }
  while (fgets(inbuf, BUFSIZ, fp))
    cmdparse(cmds, inbuf, NULL);
  fclose(fp);
  mode = CMD_MODE;
  cooked();
  return 0;
}

/*---------------------------------------------------------------------------*/

#include <sys/rtprio.h>

static int  dortprio(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  int  tmp;

  if (argc < 2) {
    tmp = rtprio(0, RTPRIO_NOCHG);
    if (tmp == RTPRIO_RTOFF)
      printf("Rtprio off\n");
    else
      printf("Rtprio %d\n", tmp);
  } else {
    tmp = atoi(argv[1]);
    if (tmp <= 0 || tmp > 127) tmp = RTPRIO_RTOFF;
    rtprio(0, tmp);
  }
  return 0;
}

