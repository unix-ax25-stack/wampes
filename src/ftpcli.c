/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftpcli.c,v 1.22 1996/08/11 18:16:09 deyke Exp $ */

/* Internet FTP client (interactive user)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
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

#include "seek.h"

static void ftpparse(char *line, int len);
static int doftpcd(int argc, char *argv [], void *p);
static int domkdir(int argc, char *argv [], void *p);
static int dormdir(int argc, char *argv [], void *p);
static int dobinary(int argc, char *argv [], void *p);
static int doascii(int argc, char *argv [], void *p);
static int dotype(int argc, char *argv [], void *p);
static int doget(int argc, char *argv [], void *p);
static int doreget(int argc, char *argv [], void *p);
static int dolist(int argc, char *argv [], void *p);
static int donlst(int argc, char *argv [], void *p);
static int doput(int argc, char *argv [], void *p);
static int doappend(int argc, char *argv [], void *p);
static int doabort(int argc, char *argv [], void *p);
static int ftpsetup(struct ftp *ftp, void (*recv)(struct tcb *,int32), void (*send)(struct tcb *,int32), void (*state)(struct tcb *,enum tcp_state,enum tcp_state));
static void ftpccs(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static void ftpcds(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static int sndftpmsg(struct ftp *ftp, char *fmt, char *arg);
static int doftpcdup(int argc, char *argv [], void *p);
static int doftpdelete(int argc, char *argv [], void *p);
static int doftpmodtime(int argc, char *argv [], void *p);
static int doftppassword(int argc, char *argv [], void *p);
static int doftppwd(int argc, char *argv [], void *p);
static int doftpquit(int argc, char *argv [], void *p);
static int doftpquote(int argc, char *argv [], void *p);
static int doftprestart(int argc, char *argv [], void *p);
static int doftprhelp(int argc, char *argv [], void *p);
static int doftpsize(int argc, char *argv [], void *p);
static int doftpsystem(int argc, char *argv [], void *p);
static int doftpuser(int argc, char *argv [], void *p);

static char cantwrite[] = "Can't write %s\n";
static char cantread[] = "Can't read %s\n";

static struct cmds Ftpabort[] = {
	"",             donothing,      0, 0, NULL,
	"abort",        doabort,        0, 0, NULL,
	NULL,       NULL,         0, 0, "Only valid command is \"abort\""
};

static struct cmds Ftpcmds[] = {
	"",             donothing,      0, 0, NULL,

	"append",       doappend,       0, 2, "append <localfile> [<remotefile>]",
	"ascii",        doascii,        0, 0, NULL,
	"binary",       dobinary,       0, 0, NULL,
	"bye",          doftpquit,      0, 0, NULL,
	"cd",           doftpcd,        0, 2, "cd <directory>",
	"cdup",         doftpcdup,      0, 0, NULL,
	"delete",       doftpdelete,    0, 2, "delete <remotefile>",
	"dir",          dolist,         0, 0, NULL,
	"get",          doget,          0, 2, "get <remotefile> [<localfile>]",
	"image",        dobinary,       0, 0, NULL,
	"ls",           dolist,         0, 0, NULL,
	"mkdir",        domkdir,        0, 2, "mkdir <directory>",
	"modtime",      doftpmodtime,   0, 2, "modtime <remotefile>",
	"nlist",        donlst,         0, 0, NULL,
	"password",     doftppassword,  0, 2, "password <password>",
	"put",          doput,          0, 2, "put <localfile> [<remotefile>]",
	"pwd",          doftppwd,       0, 0, NULL,
	"quit",         doftpquit,      0, 0, NULL,
	"quote",        doftpquote,     0, 2, "quote <ftp command>",
	"recv",         doget,          0, 2, "recv <remotefile> [<localfile>]",
	"reget",        doreget,        0, 2, "reget <remotefile> [<localfile>]",
	"restart",      doftprestart,   0, 2, "restart <offset>",
	"rhelp",        doftprhelp,     0, 0, NULL,
	"rmdir",        dormdir,        0, 2, "rmdir <directory>",
	"send",         doput,          0, 2, "send <localfile> [<remotefile>]",
	"size",         doftpsize,      0, 2, "size <remotefile>",
	"system",       doftpsystem,    0, 0, NULL,
	"type",         dotype,         0, 0, NULL,
	"user",         doftpuser,      0, 2, "user <user-name> [<password>]",

	NULL,       NULL,         0, 0, "Unknown command; type \"?\" for list"
};

/* Handle top-level FTP command */
int doftp(int argc, char *argv[], void *p)
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
	if((s = newsession()) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	Current = s;
	if((s->name = (char *) malloc((unsigned)strlen(argv[1])+1)) != NULL)
		strcpy(s->name,argv[1]);
	s->type = FTP;
	s->parse = ftpparse;

	/* Allocate an FTP control block */
	if((ftp = ftp_create(0)) == NULL){
		s->type = NO_SESSION;
		printf(Nospace);
		return 1;
	}
	ftp->state = COMMAND_STATE;
	s->cb.ftp = ftp;        /* Downward link */
	ftp->session = s;       /* Upward link */

	/* Now open the control connection */
	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,
		0,ftpccr,NULL,ftpccs,0,(int)ftp);
	ftp->control = tcb;
	go(argc, argv, p);
	return 0;
}
/* Parse user FTP commands */
static void ftpparse(char *line, int len)
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

static int doftpcd(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "CWD %s\r\n", argv[1]);
}

static int domkdir(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "XMKD %s\r\n", argv[1]);
}

static int dormdir(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "XRMD %s\r\n", argv[1]);
}

static int
dobinary(
int argc,
char *argv[],
void *p)
{
	char *args[2];

	args[1] = "I";
	return dotype(2,args,p);
}

static int
doascii(
int argc,
char *argv[],
void *p)
{
	char *args[2];

	args[1] = "A";
	return dotype(2,args,p);
}

static int
dotype(
int argc,
char *argv[],
void *p)
{
	char *bytesize;
	struct ftp *ftp;

	ftp = Current->cb.ftp;
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
		bytesize = (argc >= 3) ? argv[2] : "8";
		ftp->logbsize = atoi(bytesize);
		sndftpmsg(ftp,"TYPE L %s\r\n",bytesize);
		break;
	default:
		printf("Invalid type %s\n",argv[1]);
		return 1;
	}
	return 0;
}

static int
doget(
int argc,
char *argv[],
void *p)
{

	char *remotename, *localname;
	struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULL;

	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	if(!strcmp(localname, "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(localname,"w")) == NULL){
		printf(cantwrite,localname);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULL,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"RETR %s\r\n",remotename);
}

static int doreget(int argc, char *argv[], void *p)
{

	char *remotename, *localname;
	long rest;
	struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULL;

	remotename = argv[1];
	if(argc < 3)
		localname = remotename;
	else
		localname = argv[2];

	if((ftp->fp = fopen(localname,"a")) == NULL){
		printf(cantwrite,localname);
		return 1;
	}

	fseek(ftp->fp,0,SEEK_END);
	rest = ftell(ftp->fp);
	if(rest){
		char buf[16];
		sprintf(buf,"%ld",rest);
		sndftpmsg(ftp,"REST %s\r\n",buf);
	}

	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULL,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"RETR %s\r\n",remotename);
}

static int dolist(int argc, char *argv[], void *p)
{
	struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULL;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULL){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULL,ftpcds);
	/* Generate the command to start the transfer
	 * It's done this way to avoid confusing the 4.2 FTP server
	 * if there's no argument
	 */
	if(argc > 1)
		return sndftpmsg(ftp,"LIST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"LIST\r\n","");
}

static int donlst(int argc, char *argv[], void *p)
{
	struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULL;

	if(argc < 3 || !strcmp(argv[2], "-")){
		ftp->fp = stdout;
	} else if((ftp->fp = fopen(argv[2],"w")) == NULL){
		printf(cantwrite,argv[2]);
		return 1;
	}
	ftp->state = RECEIVING_STATE;
	ftpsetup(ftp,ftpdr,NULL,ftpcds);
	/* Generate the command to start the transfer */
	if(argc > 1)
		return sndftpmsg(ftp,"NLST %s\r\n",argv[1]);
	else
		return sndftpmsg(ftp,"NLST\r\n","");
}

static int doput(int argc, char *argv[], void *p)
{

	char *remotename, *localname;
	struct ftp *ftp;

	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);

	if((ftp->fp = fopen(localname,"r")) == NULL){
		printf(cantread,localname);
		return 1;
	}
	ftp->state = SENDING_STATE;
	ftpsetup(ftp,NULL,ftpdt,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"STOR %s\r\n",remotename);
}

static int doappend(int argc, char *argv[], void *p)
{

	char *remotename, *localname;
	struct ftp *ftp;

	localname = argv[1];
	if(argc < 3)
		remotename = localname;
	else
		remotename = argv[2];

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);

	if((ftp->fp = fopen(localname,"r")) == NULL){
		printf(cantread,localname);
		return 1;
	}
	ftp->state = SENDING_STATE;
	ftpsetup(ftp,NULL,ftpdt,ftpcds);

	/* Generate the command to start the transfer */
	return sndftpmsg(ftp,"APPE %s\r\n",remotename);
}

static int doabort(int argc, char *argv[], void *p)
{
	struct ftp *ftp;

	ftp = Current->cb.ftp;
	if(ftp->fp != NULL && ftp->fp != stdout)
		fclose(ftp->fp);
	ftp->fp = NULL;

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
		ftp->data = NULL;
		printf("Get aborted\n");
		break;
	default:
		break;
	}
	ftp->state = COMMAND_STATE;
	return 0;
}

static int ftpsetup(struct ftp *ftp, void (*recv)(struct tcb *,int32), void (*send)(struct tcb *,int32), void (*state)(struct tcb *,enum tcp_state,enum tcp_state))
{

	struct mbuf *bp;
	struct socket lsocket;

	lsocket.address = ftp->control->conn.local.address;
	lsocket.port = Lport++;

	/* Compose and send PORT a,a,a,a,p,p message */

	if((bp = alloc_mbuf(35)) == NULL){   /* 5 more than worst case */
		printf(Nospace);
		return 0;
	}
	/* I know, this looks gross, but it works! */
	sprintf((char *) bp->data,"PORT %u,%u,%u,%u,%u,%u\r\n",
		hibyte(hiword(lsocket.address)),
		lobyte(hiword(lsocket.address)),
		hibyte(loword(lsocket.address)),
		lobyte(loword(lsocket.address)),
		hibyte(lsocket.port),
		lobyte(lsocket.port));
	bp->cnt = strlen((char *) bp->data);
	send_tcp(ftp->control,&bp);

	/* Post a listen on the data connection */
	ftp->data = open_tcp(&lsocket,NULL,TCP_PASSIVE,0,
		recv,send,state,0,(int)ftp);
	return 0;
}

/* FTP Client Control channel Receiver upcall routine */

void ftpccr(struct tcb *tcb, int32 cnt)
{

	struct ftp *ftp;
	struct mbuf *bp;

	if((ftp = (struct ftp *)tcb->user) == NULL){
		/* Unknown connection; kill it */
		close_tcp(tcb);
		return;
	}
	/* Hold output if we're not the current session */
	if(Mode != CONV_MODE || Current == NULL || Current->cb.ftp != ftp)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0){
		while(bp != NULL){
			fwrite(bp->data,1,(unsigned)bp->cnt,stdout);
			bp = free_mbuf(&bp);
		}
	}
}

/* FTP Client Control channel State change upcall routine */

static void ftpccs(struct tcb *tcb, enum tcp_state old, enum tcp_state new)
{

	int notify = 0;
	struct ftp *ftp;

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	ftp = (struct ftp *)tcb->user;

	if(Current != NULL && Current->cb.ftp == ftp)
		notify = 1;

	switch(new){
	case TCP_CLOSE_WAIT:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		close_tcp(tcb);
		break;
	case TCP_CLOSED:    /* heh heh */
		if(notify){
			printf("%s (%s",Tcpstates[new],Tcpreasons[tcb->reason & 0xff]);
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
		if(ftp != NULL)
			ftp_delete(ftp);
		break;
	default:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		break;
	}
}

/* FTP Client Data channel State change upcall handler */

static void ftpcds(struct tcb *tcb, enum tcp_state old, enum tcp_state new)
{
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULL){
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
			if(Current != NULL && Current->cb.ftp == ftp){
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
			ftp->fp = NULL;
			ftp->state = COMMAND_STATE;
			if(Current != NULL && Current->cb.ftp == ftp){
				printf("Get complete, %lu bytes received\n",
					tcb->rcv.nxt - tcb->irs - 2);
			}
		}
		break;
	case TCP_CLOSED:
		ftp->data = NULL;
		del_tcp(tcb);
		break;
	default:
		break;
	}
}

/* Send a message on the control channel */

static int sndftpmsg(struct ftp *ftp, char *fmt, char *arg)
{

	struct mbuf *bp;
	uint16 len;

	len = strlen(fmt) + strlen(arg) + 10;   /* fudge factor */
	if((bp = alloc_mbuf(len)) == NULL){
		printf(Nospace);
		return 1;
	}
	sprintf((char *) bp->data,fmt,arg);
	bp->cnt = strlen((char *) bp->data);
	send_tcp(ftp->control,&bp);
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

	int i;
	int len;
	struct mbuf *bp;

	len = 3;
	for (i = 1; i < argc; i++)
		len += strlen(argv[i]) + 1;
	if ((bp = alloc_mbuf(len)) == NULL) {
		printf(Nospace);
		return 1;
	}
	*bp->data = 0;
	for (i = 1; i < argc; i++) {
		if (i > 1)
			strcat((char *) bp->data, " ");
		strcat((char *) bp->data, argv[i]);
	}
	strcat((char *) bp->data, "\r\n");
	bp->cnt = strlen((char *) bp->data);
	send_tcp(Current->cb.ftp->control, &bp);
	return 0;
}

static int doftprestart(int argc, char *argv[], void *p)
{
	return sndftpmsg(Current->cb.ftp, "REST %s\r\n", argv[1]);
}

static int doftprhelp(int argc, char *argv[], void *p)
{
	if (argc < 2)
		return sndftpmsg(Current->cb.ftp, "HELP\r\n", "");
	return sndftpmsg(Current->cb.ftp, "HELP %s\r\n", argv[1]);
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
