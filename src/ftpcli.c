/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftpcli.c,v 1.13 1993/12/29 16:32:16 deyke Exp $ */

/* Internet FTP client (interactive user)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "session.h"
#include "cmdparse.h"
#include "timer.h"
#include "socket.h"
#include "icmp.h"
#include "tcp.h"
#include "ftp.h"
#include "commands.h"
#include "netuser.h"
#include "dirutil.h"

static void ftpparse(char *line,int len);
static int doftpcd(int argc,char *argv[],void *p);
static int domkdir(int argc,char *argv[],void *p);
static int dormdir(int argc,char *argv[],void *p);
static int dobinary(int argc,char *argv[],void *p);
static int doascii(int argc,char *argv[],void *p);
static int dotype(int argc,char *argv[],void *p);
static int doget(int argc,char *argv[],void *p);
static int doreget(int argc,char *argv[],void *p);
static int dolist(int argc,char *argv[],void *p);
static int dols(int argc,char *argv[],void *p);
static int doput(int argc,char *argv[],void *p);
static int doappend(int argc,char *argv[],void *p);
static int ftpsetup(struct ftp *ftp,void (*recv)(),void (*send)(),void (*state)());
static void ftpccs(struct tcb *tcb,int old,int new);
static void ftpcds(struct tcb *tcb,int old,int new);
static int sndftpmsg(struct ftp *ftp,char *fmt,char *arg);
static int doftpcdup(int argc,char *argv[],void *p);
static int doftpdelete(int argc,char *argv[],void *p);
static int doftpmodtime(int argc,char *argv[],void *p);
static int doftppassword(int argc,char *argv[],void *p);
static int doftppwd(int argc,char *argv[],void *p);
static int doftpquit(int argc,char *argv[],void *p);
static int doftpquote(int argc,char *argv[],void *p);
static int doftprestart(int argc,char *argv[],void *p);
static int doftprhelp(int argc,char *argv[],void *p);
static int doftpsize(int argc,char *argv[],void *p);
static int doftpsystem(int argc,char *argv[],void *p);
static int doftpuser(int argc,char *argv[],void *p);

static char Notsess[] = "Not an FTP session!\n";
static char cantwrite[] = "Can't write %s\n";
static char cantread[] = "Can't read %s\n";

static struct cmds Ftpabort[] = {
	"",             donothing,      0, 0, NULLCHAR,
	"abort",        doabort,        0, 0, NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0, "Only valid command is \"abort\""
};

static struct cmds Ftpcmds[] = {
	"",             donothing,      0, 0, NULLCHAR,

	"append",       doappend,       0, 2, "append <localfile> <remotefile>",
	"ascii",        doascii,        0, 0, NULLCHAR,
	"binary",       dobinary,       0, 0, NULLCHAR,
	"bye",          doftpquit,      0, 0, NULLCHAR,
	"cd",           doftpcd,        0, 2, "cd <directory>",
	"cdup",         doftpcdup,      0, 0, NULLCHAR,
	"delete",       doftpdelete,    0, 2, "delete <remotefile>",
	"dir",          dolist,         0, 0, NULLCHAR,
	"get",          doget,          0, 2, "get <remotefile> <localfile>",
	"image",        dobinary,       0, 0, NULLCHAR,
	"list",         dolist,         0, 0, NULLCHAR,
	"ls",           dols,           0, 0, NULLCHAR,
	"mkdir",        domkdir,        0, 2, "mkdir <directory>",
	"modtime",      doftpmodtime,   0, 2, "modtime <remotefile>",
	"nlist",        dols,           0, 0, NULLCHAR,
	"password",     doftppassword,  0, 2, "password <password>",
	"put",          doput,          0, 2, "put <localfile> <remotefile>",
	"pwd",          doftppwd,       0, 0, NULLCHAR,
	"quit",         doftpquit,      0, 0, NULLCHAR,
	"quote",        doftpquote,     0, 2, "quote <ftp command>",
	"recv",         doget,          0, 2, "recv <remotefile> <localfile>",
	"reget",        doreget,        0, 2, "reget <remotefile> <localfile>",
	"restart",      doftprestart,   0, 2, "restart <offset>",
	"rhelp",        doftprhelp,     0, 0, NULLCHAR,
	"rmdir",        dormdir,        0, 2, "rmdir <directory>",
	"send",         doput,          0, 2, "send <localfile> <remotefile>",
	"size",         doftpsize,      0, 2, "size <remotefile>",
	"system",       doftpsystem,    0, 0, NULLCHAR,
	"type",         dotype,         0, 0, NULLCHAR,
	"user",         doftpuser,      0, 2, "user <user-name> [password]",

	NULLCHAR,       NULLFP,         0, 0, "Unknown command; type \"?\" for list"
};

/* Handle top-level FTP command */
int
doftp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *s;
	struct ftp *ftp;
	struct tcb *tcb;
	struct socket lsocket,fsocket;

	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	if((fsocket.address = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	if(argc < 3)
		fsocket.port = IPPORT_FTP;
	else
		fsocket.port = tcp_port_number(argv[2]);

	/* Allocate a session control block */
	if((s = newsession()) == NULLSESSION){
		printf("Too many sessions\n");
		return 1;
	}
	Current = s;
	if((s->name = malloc((unsigned)strlen(argv[1])+1)) != NULLCHAR)
		strcpy(s->name,argv[1]);
	s->type = FTP;
	s->parse = ftpparse;

	/* Allocate an FTP control block */
	if((ftp = ftp_create(0)) == NULLFTP){
		s->type = FREE;
		printf(Nospace);
		return 1;
	}
	ftp->state = COMMAND_STATE;
	s->cb.ftp = ftp;        /* Downward link */
	ftp->session = s;       /* Upward link */

	/* Now open the control connection */
	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,
		0,ftpccr,NULLVFP,ftpccs,0,(int)ftp);
	ftp->control = tcb;
	go(argc, argv, p);
	return 0;
}
/* Parse user FTP commands */
static void
ftpparse(line,len)
char *line;
int len;
{
	if(Current->cb.ftp->state != COMMAND_STATE){
		/* The only command allowed in data transfer state is ABORT */
		if(cmdparse(Ftpabort,line,NULL) == -1){
			printf("Transfer in progress; only ABORT is acceptable\n");
		}
		return;
	}
	cmdparse(Ftpcmds,line,NULL);
}
/* Translate 'cd' to 'cwd' for convenience */
static int
doftpcd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"CWD %s\r\n",argv[1]);
}
/* Translate 'mkdir' to 'xmkd' for convenience */
static int
domkdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"XMKD %s\r\n",argv[1]);
}
/* Translate 'rmdir' to 'xrmd' for convenience */
static int
dormdir(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	return sndftpmsg(ftp,"XRMD %s\r\n",argv[1]);
}
static int
dobinary(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *args[2];

	args[1] = "I";
	return dotype(2,args,p);
}
static int
doascii(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *args[2];

	args[1] = "A";
	return dotype(2,args,p);
}
/* Handle "type" command from user */
static int
dotype(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP)
		return -1;
	if(argc < 2){
		switch(ftp->type){
		case IMAGE_TYPE:
			printf("Image\n");
			break;
		case ASCII_TYPE:
			printf("Ascii\n");
			break;
		case LOGICAL_TYPE:
			printf("Logical bytesize %u\n",ftp->logbsize);
			break;
		}
		return 0;
	}
	switch(*argv[1]){
	case 'i':
	case 'I':
	case 'b':
	case 'B':
		ftp->type = IMAGE_TYPE;
		sndftpmsg(ftp,"TYPE I\r\n","");
		break;
	case 'a':
	case 'A':
		ftp->type = ASCII_TYPE;
		sndftpmsg(ftp,"TYPE A\r\n","");
		break;
	case 'L':
	case 'l':
		ftp->type = LOGICAL_TYPE;
		ftp->logbsize = atoi(argv[2]);
		sndftpmsg(ftp,"TYPE L %s\r\n",argv[2]);
		break;
	default:
		printf("Invalid type %s\n",argv[1]);
		return 1;
	}
	return 0;
}
/* Start receive transfer. Syntax: get <remote name> [<local name>] */
static int
doget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(Notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	if(!strcmp(localname, "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(localname,"w")) == NULLFILE){
		printf(cantwrite,localname);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"RETR %s\r\n",remotename);
}
/* Start receive transfer. Syntax: reget <remote name> [<local name>] */
static int
doreget(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	register struct ftp *ftp;
	int rest;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(Notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	if((ftp->fp = fopen(localname,"a")) == NULLFILE){
		printf(cantwrite,localname);
		return 1;
	}

	fseek(ftp->fp,0,SEEK_END);
	rest = ftell(ftp->fp);
	if(rest){
		char buf[16];
		sprintf(buf,"%d",rest);
		sndftpmsg(ftp,"REST %s\r\n",buf);
	}

	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"RETR %s\r\n",remotename);
}
/* List remote directory. Syntax: dir <remote files> [<local name>] */
static int
dolist(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(Notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULLFILE){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);
	/* Generate the command to start the transfer
	 * It's done this way to avoid confusing the 4.2 FTP server
	 * if there's no argument
	 */
	if(argc > 1)
		return sndftpmsg(ftp,"LIST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"LIST\r\n","");
}
/* Abbreviated (name only) list of remote directory.
 * Syntax: ls <remote directory/file> [<local name>]
 */
static int
dols(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp == NULLFTP){
		printf(Notsess);
		return 1;
	}
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULLFILE){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULLVFP,ftpcds);
	/* Generate the command to start the transfer */
	if(argc > 1)
		return sndftpmsg(ftp,"NLST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"NLST\r\n","");
}
/* Start transmit. Syntax: put <local name> [<remote name>] */
static int
doput(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	struct ftp *ftp;

	if((ftp = Current->cb.ftp) == NULLFTP){
		printf(Notsess);
		return 1;
	}
	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);

	if((ftp->fp = fopen(localname,"r")) == NULLFILE){
		printf(cantread,localname);
		return 1;
	}
	ftp->state = SENDING_STATE;
	ftpsetup(ftp,NULLVFP,ftpdt,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"STOR %s\r\n",remotename);
}
/* Start transmit. Syntax: append <local name> [<remote name>] */
static int
doappend(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *remotename,*localname;
	struct ftp *ftp;

	if((ftp = Current->cb.ftp) == NULLFTP){
		printf(Notsess);
		return 1;
	}
	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);

	if((ftp->fp = fopen(localname,"r")) == NULLFILE){
		printf(cantread,localname);
		return 1;
	}
	ftp->state = SENDING_STATE;
	ftpsetup(ftp,NULLVFP,ftpdt,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"APPE %s\r\n",remotename);
}
/* Abort a GET or PUT operation in progress. Note: this will leave
 * the partial file on the local or remote system
 */
doabort(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ftp *ftp;

	ftp = Current->cb.ftp;

	/* Close the local file */
	if(ftp->fp != NULLFILE && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULLFILE;

	switch(ftp->state){
	case SENDING_STATE:
		/* Send a premature EOF.
		 * Unfortunately we can't just reset the connection
		 * since the remote side might end up waiting forever
		 * for us to send something.
		 */
		close_tcp(ftp->data);
		printf("Put aborted\n");
		break;
	case RECEIVING_STATE:
		/* Just exterminate the data channel TCB; this will
		 * generate a RST on the next data packet which will
		 * abort the sender
		 */
		del_tcp(ftp->data);
		ftp->data = NULLTCB;
		printf("Get aborted\n");
		break;
	}
	ftp->state = COMMAND_STATE;
	return 0;
}
/* create data port, and send PORT message */
static int
ftpsetup(ftp,recv,send,state)
struct ftp *ftp;
void (*send)();
void (*recv)();
void (*state)();
{
	struct socket lsocket;
	struct mbuf *bp;

	lsocket.address = ftp->control->conn.local.address;
	lsocket.port = Lport++;

	/* Compose and send PORT a,a,a,a,p,p message */

	if((bp = alloc_mbuf(35)) == NULLBUF){   /* 5 more than worst case */
		printf(Nospace);
		return 0;
	}
	/* I know, this looks gross, but it works! */
	sprintf(bp->data,"PORT %u,%u,%u,%u,%u,%u\r\n",
		hibyte(hiword(lsocket.address)),
		lobyte(hiword(lsocket.address)),
		hibyte(loword(lsocket.address)),
		lobyte(loword(lsocket.address)),
		hibyte(lsocket.port),
		lobyte(lsocket.port));
	bp->cnt = strlen(bp->data);
	send_tcp(ftp->control,bp);

	/* Post a listen on the data connection */
	ftp->data = open_tcp(&lsocket,NULLSOCK,TCP_PASSIVE,0,
		recv,send,state,0,(int)ftp);
	return 0;
}
/* FTP Client Control channel Receiver upcall routine */
void
ftpccr(tcb,cnt)
register struct tcb *tcb;
uint16 cnt;
{
	struct mbuf *bp;
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection; kill it */
		close_tcp(tcb);
		return;
	}
	/* Hold output if we're not the current session */
	if(Mode != CONV_MODE || Current == NULLSESSION || Current->cb.ftp != ftp)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0){
		while(bp != NULLBUF){
			fwrite(bp->data,1,(unsigned)bp->cnt,stdout);
			bp = free_mbuf(bp);
		}
	}
}

/* FTP Client Control channel State change upcall routine */
static void
ftpccs(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	struct ftp *ftp;
	char notify = 0;

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	ftp = (struct ftp *)tcb->user;

	if(Current != NULLSESSION && Current->cb.ftp == ftp)
		notify = 1;

	switch(new){
	case TCP_CLOSE_WAIT:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		close_tcp(tcb);
		break;
	case TCP_CLOSED:    /* heh heh */
		if(notify){
			printf("%s (%s",Tcpstates[new],Tcpreasons[tcb->reason]);
			if(tcb->reason == NETWORK){
				switch(tcb->type){
				case ICMP_DEST_UNREACH:
					printf(": %s unreachable",Unreach[tcb->code]);
					break;
				case ICMP_TIME_EXCEED:
					printf(": %s time exceeded",Exceed[tcb->code]);
					break;
				}
			}
			printf(")\n");
			cmdmode();
		}
		del_tcp(tcb);
		if(ftp != NULLFTP)
			ftp_delete(ftp);
		break;
	default:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		break;
	}
}
/* FTP Client Data channel State change upcall handler */
static void
ftpcds(tcb,old,new)
struct tcb *tcb;
char old,new;
{
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection, kill it */
		close_tcp(tcb);
		return;
	}
	switch(new){
	case TCP_FINWAIT2:
	case TCP_TIME_WAIT:
		if(ftp->state == SENDING_STATE){
			/* We've received an ack of our FIN, so
			 * return to command mode
			 */
			ftp->state = COMMAND_STATE;
			if(Current != NULLSESSION && Current->cb.ftp == ftp){
				printf("Put complete, %lu bytes sent\n",
					tcb->snd.una - tcb->iss - 2);
			}
		}
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		if(ftp->state == RECEIVING_STATE){
			/* End of file received on incoming file */
			if(ftp->fp != stdout)
				fclose(ftp->fp);
			ftp->fp = NULLFILE;
			ftp->state = COMMAND_STATE;
			if(Current != NULLSESSION && Current->cb.ftp == ftp){
				printf("Get complete, %lu bytes received\n",
					tcb->rcv.nxt - tcb->irs - 2);
			}
		}
		break;
	case TCP_CLOSED:
		ftp->data = NULLTCB;
		del_tcp(tcb);
		break;
	}
}
/* Send a message on the control channel */
/*VARARGS*/
static int
sndftpmsg(ftp,fmt,arg)
struct ftp *ftp;
char *fmt;
char *arg;
{
	struct mbuf *bp;
	uint16 len;

	len = strlen(fmt) + strlen(arg) + 10;   /* fudge factor */
	if((bp = alloc_mbuf(len)) == NULLBUF){
		printf(Nospace);
		return 1;
	}
	sprintf(bp->data,fmt,arg);
	bp->cnt = strlen(bp->data);
	send_tcp(ftp->control,bp);
	return 0;
}

static int doftpcdup(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "CDUP\r\n", "");
}

static int doftpdelete(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "DELE %s\r\n", argv[1]);
}

static int doftpmodtime(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "MDTM %s\r\n", argv[1]);
}

static int doftppassword(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "PASS %s\r\n", argv[1]);
}

static int doftppwd(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "PWD\r\n", "");
}

static int doftpquit(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "QUIT\r\n", "");
}

static int doftpquote(int argc, char *argv[], void *p)
{

	char buf[2048];
	int i;

	*buf = 0;
	for (i = 1; i < argc; i++) {
		if (*buf) strcat(buf, " ");
		strcat(buf, argv[i]);
	}
	return sndftpmsg(Current->cb.ftp, "%s\r\n", buf);
}

static int doftprestart(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "REST %s\r\n", argv[1]);
}

static int doftprhelp(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "HELP\r\n", "");
}

static int doftpsize(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "SIZE %s\r\n", argv[1]);
}

static int doftpsystem(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "SYST\r\n", "");
}

static int doftpuser(int argc, char *argv[], void *p)
{
	sndftpmsg(Current->cb.ftp, "USER %s\r\n", argv[1]);
	if (argc >= 3)
		sndftpmsg(Current->cb.ftp, "PASS %s\r\n", argv[2]);
	return 0;
}

