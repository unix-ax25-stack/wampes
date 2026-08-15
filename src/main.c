/* @(#) $Id: main.c,v 1.67 2000/01/01 16:12:54 deyke Exp $ */

/* Main-level NOS program:
 *  initialization
 *  keyboard processing
 *  generic user commands
 *
 * Copyright 1986 to 1996 Phil Karn, KA9Q
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include "global.h"
#include <stdarg.h>
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "arp.h"
#include "ip.h"
#include "tcp.h"
#include "udp.h"
#include "ax25.h"
#include "kiss.h"
#include "netrom.h"
#include "ftp.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"
#include "daemon.h"
#include "devparam.h"
#include "files.h"
#include "main.h"
#include "remote.h"
#include "trace.h"
#include "hpux.h"
#include "netuser.h"
#include "remote_net.h"

extern struct cmds Cmds[],Startcmds[],Stopcmds[],Attab[];

char Escape = 0x1d;             /* default escape character is ^] */

char Badhost[] = "Unknown host %s\n";
char *Hostname;
char Nospace[] = "No space!!\n";        /* Generic malloc fail message */
struct proc *Cmdpp;
char *Cmdline;                          /* Copy of most recent command line */
int main_exit = FALSE;                  /* from main program (flag) */

int Debug;
int Mode;
uint Lport = 1024;
int stop_repeat;

char Prompt[] = "%s> ";
static FILE *Logfp;
long StartTime;                         /* time that NOS was started */
static int Verbose;

static void process_char(int c);

/* Everything the node reads while running as root is a way to become root:
 * net.rc takes shell commands with "!", and so does the command channel on
 * .sockets/netcmd.  A file or directory some other account may write is
 * therefore an entrance - take that account, edit, wait.
 *
 * So look, and say what is wrong.  Do NOT repair anything.  A node that
 * quietly fixed its own permissions would teach its operator to stop looking,
 * and the one case it failed to notice would be the one that mattered.
 * doc/PERMISSIONS.md has the rule and the commands.
 *
 * Only as root.  Started as an ordinary user - which is how it is tested -
 * there is no privilege here worth stealing, and the operator is running it
 * out of a directory of their own.
 */

static int Perm_said;

static int
perm_check(const char *path)
{
	struct stat st;

	if(stat(path,&st) == -1)
		return 0;               /* absent is not a hole */
	if(S_ISSOCK(st.st_mode))
		return 0;               /* the node sets those itself, on purpose */
	if(st.st_uid != 0)
		goto complain;
	/* A sticky directory may be writable by all and still be no way in -
	 * nobody can rename another's entry there.  /tmp is the example, and
	 * reporting it would be the kind of noise that teaches people to skip
	 * reading this.
	 */
	if(S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX))
		return 0;
	if(!(st.st_mode & (S_IWGRP | S_IWOTH)))
		return 0;
complain:;

	if(!Perm_said){
		printf("PERMISSIONS - this node runs as root:\n");
		Perm_said = 1;
	}
	if(st.st_uid != 0)
		printf("  %s belongs to uid %lu, not to root\n",
		 path,(unsigned long) st.st_uid);
	else if(st.st_mode & S_IWOTH)
		printf("  %s is writable by everyone\n",path);
	else
		printf("  %s is writable by group %lu\n",
		 path,(unsigned long) st.st_gid);
	return 1;
}

static void
check_permissions(const char *startup)
{
	static const char *const under[] = {
		TCPDIR, TCPDIR "/.sockets",
		TCPDIR "/sbin", TCPDIR "/bin", NULL
	};
	char path[1024];
	size_t n;
	int i;
	int bad = 0;

	if(geteuid() != 0)
		return;

	/* TCPDIR and the directory above it are enough to judge.  Walking the
	 * whole path from / would be the node auditing the system, which is
	 * not its job.  The parent is in because a directory one may write is
	 * one whose contents one may rename: with /usr/local in your hands,
	 * the permissions on /usr/local/wampes are beside the point.  That is
	 * the usual finding on macOS.
	 */
	n = strlen(TCPDIR);
	while(n > 0 && TCPDIR[n-1] != '/')
		n--;
	if(n > 1 && n <= sizeof(path)){
		memcpy(path,TCPDIR,n-1);        /* without the trailing slash */
		path[n-1] = '\0';
		bad += perm_check(path);
	}
	for(i = 0; under[i] != NULL; i++)
		bad += perm_check(under[i]);
	if(startup != NULL)
		bad += perm_check(startup);

	if(bad){
		printf("  Anyone who can write those can run commands as root, through\n");
		printf("  %s or through the command channel.  Nothing was changed\n",
		 startup != NULL ? startup : "the startup file");
		printf("  here on purpose - see doc/PERMISSIONS.md, or run \"make\n");
		printf("  install\" as root, which sets them.\n\n");
	}
}

int
main(int argc,char *argv[])
{
	FILE *fp;
	struct daemon *tp;
	int c;
	char cmdbuf[256];

#ifdef macII
	setposix();
#endif

#if defined linux || defined ibm032
	setbuffer(stdout,NULL,8192);
#else
	setvbuf(stdout,NULL,_IOFBF,8192);
#endif
	time((time_t *) &StartTime);
	Hostname = strdup("net");

	while((c = getopt(argc,argv,"gv")) != EOF){
		switch(c){
		case 'g':
			Debug = 1;
			break;
		case 'v':
			Verbose = 1;
			break;
		case '?':
			exit(1);
			break;
		}
	}
	kinit();
	ioinit();
	netrom_initialize();
	remote_net_initialize();
	Cmdpp = mainproc("cmdintrp");

	Sessions = (struct session *)callocw(Nsessions,sizeof(struct session));
	printf("\n================ %s ================\n", Version);
	printf("(c) Copyright 1990-2000 by Dieter Deyke, DK5SG / N0PRA\n");
	printf("(c) Copyright 1986" "-1996 by Phil Karn, KA9Q\n");
	printf("\n");
	/* Start background Daemons */
#ifndef SINGLE_THREADED
	for(tp=Daemons;;tp++){
		if(tp->name == NULL)
			break;
		newproc(tp->name,tp->stksize,tp->fp,0,NULL,NULL,0);
	}
#endif
	if(optind < argc){
		/* Read startup file named on command line */
		Startup = argv[optind];
	}
	check_permissions(Startup);
	if((fp = fopen(Startup,READ_TEXT)) == NULL){
		printf("Can't read config file %s: ",Startup);
		fflush(stdout);
		perror("");
	}else{
		while(fgets(cmdbuf,sizeof(cmdbuf),fp) != NULL){
			rip(cmdbuf);
			if(Cmdline != NULL)
				free(Cmdline);
			Cmdline = strdup(cmdbuf);
			if(Verbose)
				printf("%s\n",Cmdline);
			if(cmdparse(Cmds,cmdbuf,NULL) != 0){
				printf("input line: %s\n",Cmdline);
			}
		}
		fclose(fp);
	}
	Mode = CONV_MODE;
	cmdmode();

	while(!main_exit){
#ifndef SINGLE_THREADED
		kwait(NULL);
#else
		timerproc(0,0,0);
		network(0,0,0);
#endif
		eihalt();
	}

	{
		int i;
		time_t StopTime;
		char tbuf[32];

		time(&StopTime);
		reset_all();
		arp_savefile(NULL);
		axroute_savefile(NULL);
		route_savefile(NULL);
		for(i=0;i<100;i++)
			kwait(NULL);    /* Allow tasks to complete */
		shuttrace();
		strcpy(tbuf,ctime(&StopTime));
		rip(tbuf);
		logmsg(NULL,"NOS was stopped at %s", tbuf);
		if(Logfp){
			fclose(Logfp);
			Logfp = NULL;
		}
		iostop();
	}

	return 0;
}
/* Enter command mode */
int
cmdmode(void)
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
process_char(
int c)
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
		if (Current->parse != NULL)
			(*Current->parse)(ttybuf, cnt);
		break;
	}
	if (Mode == CMD_MODE)
		printf(Prompt, Hostname);
	fflush(stdout);
}
/* Keyboard input process */
void
keyboard(void *arg)
{

	char *p;
	char buf[1024];
	int n;

	stop_repeat = 1;
	n = read(0, p = buf, sizeof(buf));
	if (n <= 0) dobye(0, 0, 0);
	while (--n >= 0) {
		process_char(*p++ & 0xff);
		while (Fkey_ptr && *Fkey_ptr)
			process_char(*Fkey_ptr++ & 0xff);
	}
}
int
dofkey(
int argc,
char *argv[],
void *p)
{
	int n;

	n = atoi(argv[1]) - 1;
	if (n < 0 || n >= NUM_FKEY) {
		printf("key# must be 1..%d\n", NUM_FKEY);
		return 1;
	}
	if (Fkey_table[n]) {
		Fkey_ptr = 0;
		free(Fkey_table[n]);
	}
	Fkey_table[n] = strdup(argv[2]);
	return 0;
}
/* Standard commands called from main */
int
dorepeat(
int argc,
char *argv[],
void *p
){
	int32 interval;
	int ret;

	if(argc >= 3 && isdigit(argv[1][0])){
		interval = atol(argv[1]);
		if(interval <= 0)
			interval = 1;
		argc--;
		argv++;
	} else {
		interval = 1000;
	}
	stop_repeat = 0;
	while(!stop_repeat){
		printf("\033[H\033[J\033H\033J");       /* Clear screen */
		ret = subcmd(Cmds,argc,argv,p);
		fflush(stdout);
		if(ret != 0 || ppause(interval) == -1)
			break;
	}
	return 0;
}
int
dodelete(
int argc,
char *argv[],
void *p
){
	int i;

	for(i=1;i < argc; i++){
		if(remove(argv[i]) == -1){
			printf("Can't delete %s: ",argv[i]);
			fflush(stdout);
			perror("");
		}
	}
	return 0;
}
int
dorename(
int argc,
char *argv[],
void *p
){
	if(rename(argv[1],argv[2]) == -1){
		printf("Can't rename %s: ", argv[1]);
		fflush(stdout);
		perror("");
	}
	return 0;
}
int
doexit(
int argc,
char *argv[],
void *p
){
	main_exit = TRUE;       /* let everyone know we're out of here */
	return 0;       /* To satisfy lint */
}
int
dohostname(
int argc,
char *argv[],
void *p
){
	if(argc < 2)
		printf("%s\n",Hostname);
	else {
		struct iface *ifp;
		char *name;

		if((ifp = if_lookup(argv[1])) != NULL){
			if((name = resolve_a(ifp->addr, FALSE)) == NULL){
				printf("Interface address not resolved\n");
				return 1;
			} else {
				if(Hostname != NULL)
					free(Hostname);
				Hostname = strdup(name);

				/* remove trailing dot */
				if ( Hostname[strlen(Hostname)] == '.' ) {
					Hostname[strlen(Hostname)] = '\0';
				}
				printf("Hostname set to %s\n", name );
			}
		} else {
			if(Hostname != NULL)
				free(Hostname);
			Hostname = strdup(argv[1]);
		}
	}
	return 0;
}
int
dolog(
int argc,
char *argv[],
void *p
){
	static char *log_name;
	char tbuf[32];

	if(argc < 2){
		if(Logfp)
			printf("Logging to %s\n",log_name);
		else
			printf("Logging off\n");
		return 0;
	}
	if(Logfp){
		logmsg(NULL,"NOS log closed","");
		fclose(Logfp);
		Logfp = NULL;
		free(log_name);
		log_name = NULL;
	}
	if(strcmp(argv[1],"stop") != 0){
		log_name = strdup(argv[1]);
		Logfp = fopen(log_name,APPEND_TEXT);
		strcpy(tbuf,ctime((time_t *) &StartTime));
		rip(tbuf);
		logmsg(NULL,"NOS was started at %s", tbuf);
	}
	return 0;
}

/* Attach an interface
 * Syntax: attach <hw type> <I/O address> <vector> <mode> <label> <bufsize> [<speed>]
 */
int
doattach(
int argc,
char *argv[],
void *p
){
	return subcmd(Attab,argc,argv,p);
}
/* Manipulate I/O device parameters */
int
doparam(
int argc,
char *argv[],
void *p
){
	struct iface *ifp;
	int param;
	int32 val;

	if((ifp = if_lookup(argv[1])) == NULL){
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
				printf("%s: %ld\n",parmname(param),(long)val);
		}
		return 0;
	}
	if((param = devparam(argv[2])) == -1){
		printf("Unknown parameter %s\n",argv[2]);
		return 1;
	}
	if(argc < 4){
		/* Read specific parameter */
		val = (*ifp->ioctl)(ifp,param,FALSE,0L);
		if(val == -1){
			printf("Parameter %s not supported\n",argv[2]);
		} else {
			printf("%s: %ld\n",parmname(param),(long)val);
		}
		return 0;
	}
	/* Set parameter */
	(*ifp->ioctl)(ifp,param,TRUE,atol(argv[3]));
	return 0;
}

/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
void
logmsg(void *tcb, const char *fmt, const char *arg)
{
	char *cp;

	if (Logfp == NULL)
		return;
	cp = ctime((time_t *) &Secclock);
	rip(cp);
	if (tcb)
		fprintf(Logfp, "%s %s - ", cp, pinet_tcp(&((struct tcb *)tcb)->conn.remote));
	else
		fprintf(Logfp, "%s - ", cp);
	fprintf(Logfp, fmt, arg);
	fprintf(Logfp, "\n");
	fflush(Logfp);
}

int
doescape(
int argc,
char *argv[],
void *p
){
	if(argc < 2)
		printf("0x%x\n",Escape);
	else
		Escape = *argv[1];
	return 0;
}
/* Generate system command packet. Synopsis:
 * remote [-p port#] [-k key] [-a hostname] <hostname> reset|exit|kickme
 */
int
doremote(
int argc,
char *argv[],
void *p)
{
	struct socket fsock,lsock;
	struct mbuf *bp;
	int c;
	uint16 port,len;
	char *key = NULL;
	int klen = 0;
	int32 addr = 0;
	char *cmd,*host;

	port = IPPORT_REMOTE;   /* Set default */
#ifdef linux
	optind = 0;
#else
	optind = 1;             /* reinit getopt() */
#endif
	while((c = getopt(argc,argv,"a:p:k:s:")) != EOF){
		switch(c){
		case 'a':
			addr = resolve(optarg);
			break;
		case 'p':
			port = udp_port_number(optarg);
			break;
		case 'k':
			key = optarg;
			klen = strlen(key);
			break;
		case 's':
			if(Rempass)
				free(Rempass);
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

	if(key != NULL)
		len += klen;

	bp = alloc_mbuf(len);
	bp->cnt = 1;

	switch(cmd[0]){
	case 'r':
		bp->data[0] = SYS__RESET;
		if(key != NULL) {
			strncpy((char *) (&bp->data[1]),key,klen);
			bp->cnt += klen;
		}
		break;
	case 'e':
		bp->data[0] = SYS__EXIT;
		if(key != NULL) {
			strncpy((char *) (&bp->data[1]),key,klen);
			bp->cnt += klen;
		}
		break;
	case 'k':
		bp->data[0] = KICK__ME;
		if(addr != 0) {
			put32(&bp->data[1],addr);
			bp->cnt += sizeof(int32);
		}
		break;
	default:
		printf("Unknown command %s\n",cmd);
		free_p(&bp);
		return 1;
	}
	send_udp(&lsock,&fsock,0,0,&bp,0,0,0);
	return 0;
}
/* No-op command */
int
donothing(
int argc,
char *argv[],
void *p
){
	return 0;
}

/*---------------------------------------------------------------------------*/

int dosource(
int argc,
char *argv[],
void *p)
{

  FILE *fp;
  char cmdbuf[1024];

  if (!(fp = fopen(argv[1], READ_TEXT))) {
    printf("Can't open %s: ", argv[1]);
    fflush(stdout);
    perror("");
    return 1;
  }
  while (fgets(cmdbuf, sizeof(cmdbuf), fp))
    cmdparse(Cmds, cmdbuf, NULL);
  fclose(fp);
  Mode = CMD_MODE;
  cooked();
  return 0;
}
