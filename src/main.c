/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.c,v 1.29 1992/06/08 12:59:26 deyke Exp $ */

/* Main-level NOS program:
 *  initialization
 *  keyboard processing
 *  generic user commands
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <sys/types.h>
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
#include "daemon.h"
#include "devparam.h"
/* #include "domain.h" */
#include "files.h"
#include "main.h"
#include "remote.h"
#include "trace.h"
#include "hpux.h"
#include "netuser.h"
#include "remote_net.h"

extern int errno;
extern char *sys_errlist[];

extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

#ifndef MSDOS                   /* PC uses F-10 key always */
char Escape = 0x1d;             /* default escape character is ^] */
#endif

int Debug;
int Mode;
char Badhost[] = "Unknown host %s\n";
char *Hostname;
char Prompt[] = "%s> ";
char Nospace[] = "No space!!\n";        /* Generic malloc fail message */
struct proc *Cmdpp;
static FILE *Logfp;
time_t StartTime;                       /* time that NOS was started */
int16 Lport = 1024;
static int Verbose;

static void process_char __ARGS((int c));

int
main(argc,argv)
int argc;
char *argv[];
{
	static char linebuf[BUFSIZ];    /* keep it off the stack */
	FILE *fp;
	struct daemon *tp;
	int c;

	time(&StartTime);

	Debug = (argc >= 2);

	while((c = getopt(argc,argv,"v")) != EOF){
		switch(c){
		case 'v':
			Verbose = 1;
			break;
		}
	}
	kinit();
	ipinit();
	ioinit();
	netrom_initialize();
	remote_net_initialize();
	Cmdpp = mainproc("cmdintrp");

	Sessions = (struct session *)callocw(Nsessions,sizeof(struct session));
	printf("\n================ %s ================\n", Version);
	printf("(c) Copyright 1990-1992 by Dieter Deyke, DK5SG / N0PRA\n");
	printf("(c) Copyright 1992 by Phil Karn, KA9Q\n");
	printf("\n");

	/* Start background Daemons */
	for(tp=Daemons;;tp++){
		if(tp->name == NULLCHAR)
			break;
		newproc(tp->name,tp->stksize,tp->fp,0,NULLCHAR,NULL,0);
	}

	if(optind < argc){
		/* Read startup file named on command line */
		if((fp = fopen(argv[optind],READ_TEXT)) == NULLFILE)
			printf("Can't read config file %s: %s\n",
			 argv[optind],sys_errlist[errno]);
	} else {
		fp = fopen(Startup,READ_TEXT);
	}
	if(fp != NULLFILE){
		while(fgets(linebuf,BUFSIZ,fp) != NULLCHAR){
			char intmp[BUFSIZ];
			strcpy(intmp,linebuf);
			if(Verbose)
				printf("%s",intmp);
			if(cmdparse(Cmds,linebuf,NULL) != 0){
				printf("input line: %s",intmp);
			}
		}
		fclose(fp);
	}
	cmdmode();

	/* Main commutator loop */
	for(;;){
		pwait(NULL);

		/* Wait until interrupt, then do it all over again */
		eihalt();
	}
}
/* Enter command mode */
int
cmdmode()
{
	if(Mode != CMD_MODE){
		Mode = CMD_MODE;
		cooked();
		printf(Prompt,Hostname);
	}
	return 0;
}
/* Process keyboard characters */
static void
process_char(c)
int c;
{

	char *ttybuf;
	int cnt;

	if (c == Escape && Escape != 0) {
		ttydriv('\r', &ttybuf);
		Mode = CONV_MODE;
		cmdmode();
		return;
	}
	if ((cnt = ttydriv(c, &ttybuf)) == 0)
		return;
	switch (Mode) {
	case CMD_MODE:
		cmdparse(Cmds, ttybuf, NULL);
		break;
	case CONV_MODE:
		if (Current->parse != NULLVFP)
			(*Current->parse)(ttybuf, cnt);
		break;
	}
	if (Mode == CMD_MODE)
		printf(Prompt, Hostname);
}
/* Keyboard input process */
void
keyboard(i,v1,v2)
int i;
void *v1;
void *v2;
{

	char *p;
	char buf[1024];
	int n;

	n = read(0, p = buf, sizeof(buf));
	if (n <= 0) dobye(0, (char **) 0, (void * ) 0);
	while (--n >= 0) {
		process_char(uchar(*p++));
		while (Fkey_ptr && *Fkey_ptr)
			process_char(uchar(*Fkey_ptr++));
	}
}
int
dofkey(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int n;

	n = atoi(argv[1]) - 1;
	if (n < 0 || n >= NUM_FKEY) {
		printf("key# must be 1..%d\n", NUM_FKEY);
		return 1;
	}
	if (Fkey_table[n])
		free(Fkey_table[n]);
	Fkey_table[n] = strdup(argv[2]);
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
			printf("Can't delete %s: %s\n",
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
		printf("Can't rename: %s\n",sys_errlist[errno]);
	return 0;
}
int
doexit(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	time_t StopTime;
	char tbuf[32];

	time(&StopTime);
	reset_all();
	shuttrace();
	strcpy(tbuf,ctime(&StopTime));
	rip(tbuf);
	log(NULLTCB,"NOS was stopped at %s", tbuf);
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
		printf("%s\n",Hostname);
	else {
		struct iface *ifp;
		char *name;

		if((ifp = if_lookup(argv[1])) != NULLIF){
			if((name = resolve_a(ifp->addr, FALSE)) == NULLCHAR){
				printf("Interface address not resolved\n");
				return 1;
			} else {
				if(Hostname != NULLCHAR)
					free(Hostname);
				Hostname = strdup(name);

				/* remove trailing dot */
				if ( Hostname[strlen(Hostname)] == '.' ) {
					Hostname[strlen(Hostname)] = '\0';
				}
				printf("Hostname set to %s\n", name );
			}
		} else {
			if(Hostname != NULLCHAR)
				free(Hostname);
			Hostname = strdup(argv[1]);
		}
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
	char tbuf[32];

	if(argc < 2){
		if(Logfp)
			printf("Logging to %s\n",logname);
		else
			printf("Logging off\n");
		return 0;
	}
	if(Logfp){
		log(NULLTCB,"NOS log closed");
		fclose(Logfp);
		Logfp = NULLFILE;
		free(logname);
		logname = NULLCHAR;
	}
	if(strcmp(argv[1],"stop") != 0){
		logname = strdup(argv[1]);
		Logfp = fopen(logname,APPEND_TEXT);
		strcpy(tbuf,ctime(&StartTime));
		rip(tbuf);
		log(NULLTCB,"NOS was started at %s", tbuf);
#ifdef MSDOS
		log(NULLTCB,"NOS load information: CS=0x%04x DS=0x%04x", _CS, _DS);
#endif
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
		printf("Interface \"%s\" unknown\n",argv[1]);
		return 1;
	}
	if(ifp->ioctl == NULL){
		printf("Not supported\n");
		return 1;
	}
	if(argc < 3){
		for(param=1;param<=16;param++){
			val = (*ifp->ioctl)(ifp,param,FALSE,0L);
			if(val != -1)
				printf("%s: %ld\n",parmname(param),val);
		}
		return 0;
	}
	param = devparam(argv[2]);
	if(param == -1){
		printf("Unknown parameter %s\n",argv[2]);
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
		printf("Parameter %s not supported\n",argv[2]);
	} else {
		printf("%s: %ld\n",parmname(param),val);
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

#ifndef MSDOS
int
doescape(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		printf("0x%x\n",Escape);
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
		printf("Insufficient args\n");
		return -1;
	}
	host = argv[optind++];
	cmd = argv[optind];

	if (!(fsock.address = resolve(host))) {
		printf("Host %s unknown\n",host);
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
		printf("Unknown command %s\n",cmd);
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
    printf("cannot open %s\n", argv[1]);
    return 1;
  }
  while (fgets(linebuf, BUFSIZ, fp))
    cmdparse(Cmds, linebuf, NULL);
  fclose(fp);
  Mode = CMD_MODE;
  cooked();
  return 0;
}

