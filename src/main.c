/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.c,v 1.18 1991/05/24 12:10:00 deyke Exp $ */

/* Main-level NOS program:
 *  initialization
 *  keyboard processing
 *  generic user commands
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <string.h>
#include <time.h>
#if     defined(__TURBOC__) && defined(MSDOS)
#include <io.h>
#include <conio.h>
#endif
#include <unistd.h>
#include "global.h"
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "ax25.h"
#include "kiss.h"
#include "enet.h"
#include "netrom.h"
#include "ftp.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "hardware.h"
/* #include "usock.h" */
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"
/* #include "daemon.h" */
#include "devparam.h"
/* #include "domain.h" */
#include "files.h"
/* #include "main.h" */
#include "remote.h"
#include "trace.h"
#include "hpux.h"
#include "netuser.h"
#include "remote_net.h"

extern int errno;
extern char *sys_errlist[];

extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

#ifndef MSDOS                   /* PC uses F-10 key always */
static char Escape = 0x1d;      /* default escape character is ^] */
#endif

int Debug;
int Mode;
char Badhost[] = "Unknown host %s\n";
char *Hostname;
char Prompt[] = "%s> ";
char Nospace[] = "No space!!\n";        /* Generic malloc fail message */
static FILE *Logfp;
int16 Lport = 1024;
static int Verbose;

int
main(argc,argv)
int argc;
char *argv[];
{
	static char linebuf[BUFSIZ];    /* keep it off the stack */
	char *ttybuf;
	int16 cnt;
	FILE *fp;
	struct iface *ifp;
	int c;

	Debug = (argc >= 2);

	while((c = getopt(argc,argv,"v")) != EOF){
		switch(c){
		case 'v':
			Verbose = 1;
			break;
		}
	}
	ipinit();
	ioinit();
	netrom_initialize();
	remote_net_initialize();

	Sessions = (struct session *)callocw(Nsessions,sizeof(struct session));
	tprintf("\n");
	tprintf("@(#)WAMPES version 910523\n" + 4);
	tprintf("(c) Copyright 1990, 1991 by Dieter Deyke, DK5SG\n");
	tprintf("(c) Copyright 1990 by Phil Karn, KA9Q\n");
	tprintf("\n");

	if(optind < argc){
		/* Read startup file named on command line */
		if((fp = fopen(argv[optind],READ_TEXT)) == NULLFILE)
			tprintf("Can't read config file %s: %s\n",
			 argv[optind],sys_errlist[errno]);
	} else {
		fp = fopen(Startup,READ_TEXT);
	}
	if(fp != NULLFILE){
		while(fgets(linebuf,BUFSIZ,fp) != NULLCHAR){
			char intmp[BUFSIZ];
			strcpy(intmp,linebuf);
			if(Verbose)
				tprintf("%s",intmp);
			if(cmdparse(Cmds,linebuf,NULL) != 0){
				tprintf("input line: %s",intmp);
			}
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
				if(Mode != CMD_MODE){
					tprintf("\n");
					cmdmode();
				}
				continue;
			}
#else
			if(c == Escape && Escape != 0){
				ttydriv('\r', &ttybuf);
				Mode = CONV_MODE;
				cmdmode();
				continue;
			}
#endif
			if ((cnt = ttydriv(c, &ttybuf)) == 0)
				continue;
			switch(Mode){
			case CMD_MODE:
				(void)cmdparse(Cmds,ttybuf,NULL);
				fflush(stdout);
				break;
			case CONV_MODE:
#ifdef  false
				if(ttybuf[0] == Escape && Escape != 0){
					tprintf("\n");
					cmdmode();
				} else
#endif  /* MSDOS */
					if(Current->parse != NULLVFP)
						(*Current->parse)(ttybuf,cnt);

				break;
			}
			if(Mode == CMD_MODE){
				tprintf(Prompt,Hostname);
				fflush(stdout);
			}
		}

		/* Service the interfaces */
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next){
			if(ifp->rxproc != NULLVFP)
				(*ifp->rxproc)(ifp);
		}

		/* Service the clock */
		timerproc();

		if(Hopper)
			network();

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
/* Enter command mode */
int
cmdmode()
{
	if(Mode != CMD_MODE){
		Mode = CMD_MODE;
		cooked();
		tprintf(Prompt,Hostname);
		fflush(stdout);
	}
	return 0;
}
/* Standard commands called from main */
int
dodelete(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int i;

	for(i=1;i < argc; i++){
		if(unlink(argv[i]) == -1){
			tprintf("Can't delete %s: %s\n",
			 argv[i],sys_errlist[errno]);
		}
	}
	return 0;
}
int
dorename(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(rename(argv[1],argv[2]) == -1)
		tprintf("Can't rename: %s\n",sys_errlist[errno]);
	return 0;
}
int
doexit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	reset_all();
	shuttrace();
	if(Logfp){
		fclose(Logfp);
		Logfp = NULLFILE;
	}
	iostop();
	exit(0);
	return 0;       /* To satisfy lint */
}
int
dohostname(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		tprintf("%s\n",Hostname);
	else {
#if 0
		struct iface *ifp;
		char *name;

		if((ifp = if_lookup(argv[1])) != NULLIF){
			if((name = resolve_a(ifp->addr, FALSE)) == NULLCHAR){
				tprintf("Interface address not resolved\n");
				return 1;
			} else {
				/* free(argv[1]); */
				argv[1] = name;
			}
		}
#endif
		if(Hostname != NULLCHAR)
			free(Hostname);
		Hostname = strdup(argv[1]);
	}
	return 0;
}
int
dolog(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	static char *logname;

	if(argc < 2){
		if(Logfp)
			tprintf("Logging to %s\n",logname);
		else
			tprintf("Logging off\n");
		return 0;
	}
	if(Logfp){
		fclose(Logfp);
		Logfp = NULLFILE;
		free(logname);
		logname = NULLCHAR;
	}
	if(strcmp(argv[1],"stop") != 0){
		logname = strdup(argv[1]);
		Logfp = fopen(logname,APPEND_TEXT);
	}
	return 0;
}

/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<speed>]
 */
int
doattach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
int
doparam(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;
	int param,set;
	int32 val;

	if((ifp = if_lookup(argv[1])) == NULLIF){
		tprintf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(ifp->ioctl == NULL){
		tprintf("Not supported\n");
		return 1;
	}
	if(argc < 3){
		for(param=1;param<=16;param++){
			val = (*ifp->ioctl)(ifp,param,FALSE,0L);
			if(val != -1)
				tprintf("%s: %ld\n",parmname(param),val);
		}
		return 0;
	}
	param = devparam(argv[2]);
	if(param == -1){
		tprintf("Unknown parameter %s\n",argv[2]);
		return 1;
	}
	if(argc < 4){
		set = FALSE;
		val = 0L;
	} else {
		set = TRUE;
		val = atol(argv[3]);
	}
	val = (*ifp->ioctl)(ifp,param,set,val);
	if(val == -1){
		tprintf("Parameter %s not supported\n",argv[2]);
	} else {
		tprintf("%s: %ld\n",parmname(param),val);
	}
	return 0;
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

	if(Logfp == NULLFILE)
		return;
	cp = ctime((long *) &Secclock);
	rip(cp);
    if (tcb)
	fprintf(Logfp,"%s %s - ",cp,pinet_tcp(&tcb->conn.remote));
    else
	fprintf(Logfp,"%s - ",cp);
	fprintf(Logfp,fmt,arg1,arg2,arg3,arg4);
	fprintf(Logfp,"\n");
	fflush(Logfp);
#if     (defined(MSDOS) || defined(ATARI_ST))
	/* MS-DOS doesn't really flush files until they're closed */
	fd = fileno(Logfp);
	if((fd = dup(fd)) != -1)
		close(fd);
#endif
}

/* Display or set IP interface control flags */
int
domode(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *ifp;

	if((ifp = if_lookup(argv[1])) == NULLIF){
		tprintf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		tprintf("%s: %s\n",ifp->name,
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
		tprintf("Usage: %s [vc | datagram]\n",argv[0]);
		return 1;
	}
	return 0;
}

#ifndef MSDOS
int
doescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		tprintf("0x%x\n",Escape);
	else
		Escape = *argv[1];
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
			addr = resolve(optarg);
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
		tprintf("Insufficient args\n");
		return -1;
	}
	host = argv[optind++];
	cmd = argv[optind];

	if (!(fsock.address = resolve(host))) {
		tprintf("Host %s unknown\n",host);
		return 1;
	}
	lsock.address = INADDR_ANY;
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
		tprintf("Unknown command %s\n",cmd);
		free_p(bp);
		return 1;
	}
	send_udp(&lsock,&fsock,0,0,bp,0,0,0);
	return 0;
}
/* No-op command */
int
donothing(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return 0;
}

/*---------------------------------------------------------------------------*/

int  dosource(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  FILE * fp;
  char  linebuf[BUFSIZ];

  if (!(fp = fopen(argv[1], "r"))) {
    tprintf("cannot open %s\n", argv[1]);
    return 1;
  }
  while (fgets(linebuf, BUFSIZ, fp))
    cmdparse(Cmds, linebuf, NULL);
  fclose(fp);
  Mode = CMD_MODE;
  cooked();
  return 0;
}

/*---------------------------------------------------------------------------*/

#include <sys/rtprio.h>

int  dortprio(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  int  tmp;

  if (argc < 2) {
    tmp = rtprio(0, RTPRIO_NOCHG);
    if (tmp == RTPRIO_RTOFF)
      tprintf("Rtprio off\n");
    else
      tprintf("Rtprio %d\n", tmp);
  } else {
    tmp = atoi(argv[1]);
    if (tmp <= 0 || tmp > 127) tmp = RTPRIO_RTOFF;
    rtprio(0, tmp);
  }
  return 0;
}

