/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftpserv.c,v 1.27 1994/09/05 12:47:10 deyke Exp $ */

/* Internet FTP Server
 * Copyright 1991 Phil Karn, KA9Q
 */

#define LINELEN         128     /* Length of command buffer */

#include <sys/types.h>

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "seteugid.h"

#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "dirutil.h"
#include "login.h"
#include "ftp.h"
#include "ftpserv.h"

static void Xprintf(struct tcb *tcb, char *message, char *arg1, char *arg2, char *arg3);
static void ftpscs(struct tcb *tcb, int old, int new);
static void ftpscr(struct tcb *tcb, int cnt);
static void ftpsds(struct tcb *tcb, int old, int new);
static char *errmsg(const char *filename);
static void ftpcommand(struct ftp *ftp);
static int pport(struct socket *sock, char *arg);
static void ftplogin(struct ftp *ftp, char *pass);
static int permcheck(struct ftp *ftp, char *file);
static int user_denied(const char *username);

/* Command table */
static char *commands[] = {
	"user",
	"acct",
	"pass",
	"type",
	"list",
	"cwd",
	"dele",
	"name",
	"quit",
	"retr",
	"stor",
	"port",
	"nlst",
	"pwd",
	"xpwd",                 /* For compatibility with 4.2BSD */
	"mkd ",
	"xmkd",                 /* For compatibility with 4.2BSD */
	"xrmd",                 /* For compatibility with 4.2BSD */
	"rmd ",
	"stru",
	"mode",
	"syst",
	"xmd5",
	"appe",
	"cdup",
	"help",
	"mdtm",
	"noop",
	"rest",
	"size",
	"xcup",
	"xcwd",
	NULLCHAR
};

/* Response messages */
static char banner[] = "220 %s FTP version %s ready at %s\r\n";
static char badcmd[] = "500 Unknown command\r\n";
static char unsupp[] = "500 Unsupported command or option\r\n";
static char givepass[] = "331 Enter PASS command\r\n";
static char logged[] = "230 User \"%s\" logged in\r\n";
static char typeok[] = "200 Type set to \"%s\"%s\r\n";
static char only8[] = "501 Only logical bytesize 8 supported\r\n";
static char deleok[] = "250 File deleted\r\n";
static char mkdok[] = "200 MKD ok\r\n";
static char pwdmsg[] = "257 \"%s\" is current directory\r\n";
static char badtype[] = "501 Unknown type \"%s\"\r\n";
static char badport[] = "501 Bad port syntax\r\n";
static char unimp[] = "502 Command not yet implemented\r\n";
static char bye[] = "221 Goodbye!\r\n";
static char sending[] = "150 Opening data connection for %s %s\r\n";
static char portok[] = "200 Port command okay\r\n";
static char rxok[] = "226 File received OK\r\n";
static char txok[] = "226 File sent OK\r\n";
static char noperm[] = "550 %s: Permission denied\r\n";
static char noconn[] = "425 Data connection reset\r\n";
static char notlog[] = "530 Please log in with USER and PASS\r\n";
static char okay[] = "200 Ok\r\n";

static struct tcb *ftp_tcb;

/* Do printf on a tcp connection */
/*VARARGS2*/
static void Xprintf(struct tcb *tcb,char *message,char *arg1,char *arg2,char *arg3)
{
	struct mbuf *bp;

	if(tcb == NULLTCB)
		return;

	bp = alloc_mbuf(256);
	sprintf(bp->data,message,arg1,arg2,arg3);
	bp->cnt = strlen(bp->data);
	send_tcp(tcb,bp);
}
/* Start up FTP service */
int ftpstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket lsocket;

	if(ftp_tcb != NULLTCB)
		close_tcp(ftp_tcb);
	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_FTP;
	else
		lsocket.port = tcp_port_number(argv[1]);

	ftp_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,ftpscr,NULLVFP,ftpscs,0,0);
	return 0;
}
/* Shut down FTP server */
int ftp0(int argc,char *argv[],void *p)
{
	if(ftp_tcb != NULLTCB)
		close_tcp(ftp_tcb);
	return 0;
}
/* FTP Server Control channel State change upcall handler */
static
void
ftpscs(struct tcb *tcb,int old,int new)
{
	struct ftp *ftp;
	char *cp,*cp1;

	switch(new){
/* Setting QUICKSTART piggybacks the server's banner on the SYN/ACK segment;
 * leaving it unset waits for the three-way handshake to complete before
 * sending the banner. Piggybacking unfortunately breaks some old TCPs,
 * so its use is not (yet) recommended.
*/
#ifdef  QUICKSTART
	case TCP_SYN_RECEIVED:
#else
	case TCP_ESTABLISHED:
#endif
		if((ftp = ftp_create(LINELEN)) == NULLFTP){
			/* No space, kill connection */
			close_tcp(tcb);
			return;
		}
		ftp->control = tcb;             /* Downward link */
		tcb->user = (int)ftp;           /* Upward link */

		/* Set default data port */
		ftp->port.address = tcb->conn.remote.address;
		ftp->port.port = IPPORT_FTPD;

		/* Note current directory */
		log(tcb,"open FTP");
		cp = ctime((long *) &Secclock);
		if((cp1 = strchr(cp,'\n')) != NULLCHAR)
			*cp1 = '\0';
		Xprintf(ftp->control,banner,Hostname,Version,cp);
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		break;
	case TCP_CLOSED:
		log(tcb,"close FTP");
		if((ftp = (struct ftp *)tcb->user) != NULLFTP)
			ftp_delete(ftp);
		/* Check if server is being shut down */
		if(tcb == ftp_tcb)
			ftp_tcb = NULLTCB;
		del_tcp(tcb);
		break;
	}
}

/* FTP Server Control channel Receiver upcall handler */
static
void
ftpscr(struct tcb *tcb,int cnt)
{
	register struct ftp *ftp;
	int c;
	struct mbuf *bp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection, just kill it */
		close_tcp(tcb);
		return;
	}
	switch(ftp->state){
	case COMMAND_STATE:
		/* Assemble an input line in the session buffer. Return if incomplete */
		recv_tcp(tcb,&bp,0);
		while((c = PULLCHAR(&bp)) != -1){
			switch(c){
			case '\r':      /* Strip cr's */
				continue;
			case '\n':      /* Complete line; process it */
				ftp->buf[ftp->cnt] = '\0';
				ftpcommand(ftp);
				ftp->cnt = 0;
				break;
			default:        /* Assemble line */
				if(ftp->cnt != LINELEN-1)
					ftp->buf[ftp->cnt++] = c;
				break;
			}
		}
		/* else no linefeed present yet to terminate command */
		break;
	case SENDING_STATE:
	case RECEIVING_STATE:
		/* Leave commands pending on receive queue until
		 * present command is done
		 */
		break;
	}
}

/* FTP server data channel connection state change upcall handler */
static void
ftpsds(struct tcb *tcb,int old,int new)
{
	register struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULLFTP){
		/* Unknown connection. Kill it */
		del_tcp(tcb);
	} else if((old == TCP_FINWAIT1 || old == TCP_CLOSING) && ftp->state == SENDING_STATE){
		/* We've received an ack of our FIN while sending; we're done */
		ftp->state = COMMAND_STATE;
		Xprintf(ftp->control,txok,"","","");
		/* Kick command parser if something is waiting */
		if(ftp->control->rcvcnt != 0)
			ftpscr(ftp->control,ftp->control->rcvcnt);
	} else if(ftp->state == RECEIVING_STATE && new == TCP_CLOSE_WAIT){
		/* FIN received on incoming file */
		close_tcp(tcb);
		if(ftp->fp != stdout)
			fclose(ftp->fp);
		ftp->fp = NULLFILE;
		ftp->state = COMMAND_STATE;
		Xprintf(ftp->control,rxok,"","","");
		/* Kick command parser if something is waiting */
		if(ftp->control->rcvcnt != 0)
			ftpscr(ftp->control,ftp->control->rcvcnt);
	} else if(new == TCP_CLOSED){
		if(tcb->reason != NORMAL){
			/* Data connection was reset, complain about it */
			Xprintf(ftp->control,noconn,"","","");
			/* And clean up */
			if(ftp->fp != NULLFILE && ftp->fp != stdout)
				fclose(ftp->fp);
			ftp->fp = NULLFILE;
			ftp->state = COMMAND_STATE;
			/* Kick command parser if something is waiting */
			if(ftp->control->rcvcnt != 0)
				ftpscr(ftp->control,ftp->control->rcvcnt);
		}
		/* Clear only if another transfer hasn't already started */
		if(ftp->data == tcb)
			ftp->data = NULLTCB;
		del_tcp(tcb);
	}
}

/*---------------------------------------------------------------------------*/

static char *errmsg(const char *filename)
{
  static char buf[1024];

  sprintf(buf, "550 %s: %s.\r\n", filename, strerror(errno));
  return buf;
}

/*---------------------------------------------------------------------------*/

#define AsUser(stmt)    if (!permcheck(ftp,file)) {                \
			  Xprintf(ftp->control,noperm,file,"",""); \
			  free(file);                              \
			  return;                                  \
			}                                          \
			seteugid(ftp->uid,ftp->gid);               \
			stmt;                                      \
			seteugid(0,0);

/* Parse and execute ftp commands */
static
void
ftpcommand(struct ftp *ftp)
{
	char *cmd,*arg,*cp,**cmdp,*file;
	struct socket dport;
	int rest;
	int result;
	struct stat statbuf;

	cmd = ftp->buf;
	if(ftp->cnt == 0){
		/* Can't be a legal FTP command */
		Xprintf(ftp->control,badcmd,"","","");
		return;
	}
	cmd = ftp->buf;

	/* Translate first word to lower case */
	for(cp = cmd;*cp != ' ' && *cp != '\0';cp++)
		*cp = Xtolower(*cp);
	/* Find command in table; if not present, return syntax error */
	for(cmdp = commands;*cmdp != NULLCHAR;cmdp++)
		if(strncmp(*cmdp,cmd,strlen(*cmdp)) == 0)
			break;
	if(*cmdp == NULLCHAR){
		Xprintf(ftp->control,badcmd,"","","");
		return;
	}
	/* Allow only USER, PASS and QUIT before logging in */
	if(ftp->cd == NULLCHAR || ftp->path == NULLCHAR){
		switch(cmdp-commands){
		case USER_CMD:
		case PASS_CMD:
		case QUIT_CMD:
			break;
		default:
			Xprintf(ftp->control,notlog,"","","");
			return;
		}
	}
	arg = &cmd[strlen(*cmdp)];
	while(*arg == ' ')
		arg++;

	/* Execute specific command */
	switch(cmdp-commands){
	case USER_CMD:
		if(!strcmp(arg, "anonymous"))
			arg = "ftp";
		free(ftp->username);
		ftp->username = strdup(arg);
		Xprintf(ftp->control,givepass,"","","");
		break;
	case TYPE_CMD:
		log(ftp->control,"TYPE %s",arg);
		switch(arg[0]){
		case 'A':
		case 'a':       /* Ascii */
			ftp->type = ASCII_TYPE;
			Xprintf(ftp->control,typeok,"A","","");
			break;
		case 'l':
		case 'L':
			while(*arg != ' ' && *arg != '\0')
				arg++;
			if(*arg == '\0' || *++arg != '8'){
				Xprintf(ftp->control,only8,"","","");
				break;
			}
			ftp->type = LOGICAL_TYPE;
			ftp->logbsize = 8;
			Xprintf(ftp->control,typeok,"L"," (byte size 8)","");
			break;
		case 'B':
		case 'b':       /* Binary */
		case 'I':
		case 'i':       /* Image */
			ftp->type = IMAGE_TYPE;
			Xprintf(ftp->control,typeok,"I","","");
			break;
		default:        /* Invalid */
			Xprintf(ftp->control,badtype,arg,"","");
			break;
		}
		break;
	case QUIT_CMD:
		log(ftp->control,"QUIT","");
		Xprintf(ftp->control,bye,"","","");
		close_tcp(ftp->control);
		break;
	case RETR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		rest = ftp->rest;
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		log(ftp->control,"RETR %s",file);
		AsUser(ftp->fp = fopen(file,"r"));
		if(ftp->fp == NULLFILE ||
		   (rest && fseek(ftp->fp,rest,SEEK_SET))){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"RETR",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case STOR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		rest = ftp->rest;
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		log(ftp->control,"STOR %s",file);
		AsUser(ftp->fp = fopen(file,"w"));
		if(ftp->fp == NULLFILE ||
		   (rest && fseek(ftp->fp,rest,SEEK_SET))){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = RECEIVING_STATE;
			Xprintf(ftp->control,sending,"STOR",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,ftpdr,NULLVFP,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case APPE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		log(ftp->control,"APPE %s",file);
		AsUser(ftp->fp = fopen(file,"a"));
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = RECEIVING_STATE;
			Xprintf(ftp->control,sending,"APPE",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,ftpdr,NULLVFP,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case PORT_CMD:
		if(pport(&ftp->port,arg) == -1){
			Xprintf(ftp->control,badport,"","","");
		} else {
			Xprintf(ftp->control,portok,"","","");
		}
		break;
	case LIST_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"LIST %s",file);
		AsUser(ftp->fp = dir(file,1));
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"LIST",file,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case NLST_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"NLST %s",file);
		AsUser(ftp->fp = dir(file,0));
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"NLST",file,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case XCWD_CMD:
	case CWD_CMD:
	case XCUP_CMD:
	case CDUP_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		if(cmdp-commands == XCUP_CMD || cmdp-commands == CDUP_CMD)
			arg = "..";
		file = pathname(ftp->cd,arg);
		log(ftp->control,"CWD  %s",file);
		AsUser(result = chdir(file));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
			free(file);
		} else {
			chdir("/");
			Xprintf(ftp->control,pwdmsg,file,"","");
			if(ftp->cd) free(ftp->cd);
			ftp->cd = file;
		}
		break;
	case XPWD_CMD:
	case PWD_CMD:
		log(ftp->control,"PWD","");
		Xprintf(ftp->control,pwdmsg,ftp->cd,"","");
		break;
	case ACCT_CMD:
		log(ftp->control,"ACCT","");
		Xprintf(ftp->control,unimp,"","","");
		break;
	case DELE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"DELE %s",file);
		AsUser(result = remove(file));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,deleok,"","","");
		}
		free(file);
		break;
	case PASS_CMD:
		tcp_output(ftp->control);       /* Send the ack now */
		ftplogin(ftp,arg);
		break;
	case XMKD_CMD:
	case MKD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"MKD %s",file);
		AsUser(result = mkdir(file,0755));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,mkdok,"","","");
		}
		free(file);
		break;
	case XRMD_CMD:
	case RMD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"RMD %s",file);
		AsUser(result = rmdir(file));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,deleok,"","","");
		}
		free(file);
		break;
	case STRU_CMD:
		log(ftp->control,"STRU %s",arg);
		if(Xtolower(arg[0]) != 'f')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	case MODE_CMD:
		log(ftp->control,"MODE %s",arg);
		if(Xtolower(arg[0]) != 's')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	case SYST_CMD:
		log(ftp->control,"SYST","");
		Xprintf(ftp->control,"215 UNIX Type: L8\r\n","","","");
		break;
	case HELP_CMD:
		{
		char line[80];
		int i;
		Xprintf(ftp->control,"214- The following commands are recognized\r\n","","","");
		*line = 0;
		for(i = 0;commands[i];i++){
			sprintf(line + strlen(line),"    %-4s",commands[i]);
			if(strlen(line) >= 64){
				Xprintf(ftp->control,"%s\r\n",line,"","");
				*line = 0;
			}
		}
		if(*line)
			Xprintf(ftp->control,"%s\r\n",line,"","");
		Xprintf(ftp->control,"214\r\n","","","");
		}
		break;
	case MDTM_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"MDTM %s",file);
		AsUser(result = stat(file, &statbuf));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			char mdtmstr[80];
			struct tm *ptm;
			ptm = gmtime(&statbuf.st_mtime);
			sprintf(mdtmstr,
				"%04d%02d%02d%02d%02d%02d",
				ptm->tm_year + 1900,
				ptm->tm_mon + 1,
				ptm->tm_mday,
				ptm->tm_hour,
				ptm->tm_min,
				ptm->tm_sec);
			Xprintf(ftp->control,"213 %s\r\n",mdtmstr,"","");
		}
		free(file);
		break;
	case NOOP_CMD:
		log(ftp->control,"NOOP","");
		Xprintf(ftp->control,"200 NOOP command successful.\r\n","","","");
		break;
	case REST_CMD:
		ftp->rest = atoi(arg);
		log(ftp->control,"REST %s",arg);
		Xprintf(ftp->control,"350 Restarting at %s. Send STORE or RETRIEVE to initiate transfer.\r\n",arg,"","");
		break;
	case SIZE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		log(ftp->control,"SIZE %s",file);
		AsUser(ftp->fp = fopen(file,"r"));
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			char sizestr[80];
			fseek(ftp->fp,0,SEEK_END);
			sprintf(sizestr,"%d",ftell(ftp->fp));
			fclose(ftp->fp);
			ftp->fp = 0;
			Xprintf(ftp->control,"213 %s\r\n",sizestr,"","");
		}
		free(file);
		break;
	default:
		Xprintf(ftp->control,unimp,"","","");
		break;
	}
}
static
int
pport(struct socket *sock,char *arg)
{
	int32 n;
	int i;

	n = 0;
	for(i=0;i<4;i++){
		n = atoi(arg) + (n << 8);
		if((arg = strchr(arg,',')) == NULLCHAR)
			return -1;
		arg++;
	}
	sock->address = n;
	n = atoi(arg);
	if((arg = strchr(arg,',')) == NULLCHAR)
		return -1;
	arg++;
	n = atoi(arg) + (n << 8);
	sock->port = n;
	return 0;
}

/*---------------------------------------------------------------------------*/

#include <pwd.h>

#ifdef __386BSD__

#define crypt(key, salt) (key)

#else

char *crypt();

#endif

/* Attempt to log in the user whose name is in ftp->username and password
 * in pass
 */

static void ftplogin(ftp,pass)
struct ftp *ftp;
char *pass;
{

  char salt[3];
  struct passwd *pw;

  if (user_denied(ftp->username)) goto Fail;
  pw = getpasswdentry(ftp->username, 0);
  if (!pw) goto Fail;
  salt[0] = pw->pw_passwd[0];
  salt[1] = pw->pw_passwd[1];
  salt[2] = 0;
  if (pw->pw_passwd[0] &&
      strcmp(pw->pw_name, "ftp") &&
      strcmp(crypt(pass, salt), pw->pw_passwd)) goto Fail;
  ftp->uid = pw->pw_uid;
  ftp->gid = pw->pw_gid;
  if (ftp->cd) free(ftp->cd);
  ftp->cd = strdup(pw->pw_dir);
  if (ftp->path) free(ftp->path);
  ftp->path = strdup(strcmp(pw->pw_name, "ftp") ? "/" : pw->pw_dir);
  Xprintf(ftp->control, logged, pw->pw_name, "", "");
  log(ftp->control, "%s logged in", pw->pw_name);
  return;

Fail:
  Xprintf(ftp->control, noperm, ftp->username, "", "");
}

/*---------------------------------------------------------------------------*/

/* Return 1 if the file operation is allowed, 0 otherwise */

static int permcheck(struct ftp *ftp,char *file)
{
  if (file == NULLCHAR || ftp->path == NULLCHAR) return 0;

  /* The target file must be under the user's allowed search path */

  if (strncmp(file, ftp->path, strlen(ftp->path))) return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

static int user_denied(const char *username)
{

  FILE *fp;
  char buf[80];

  if ((fp = fopen("/etc/ftpusers", "r"))) {
    while (fgets(buf, sizeof(buf), fp)) {
      rip(buf);
      if (!strcmp(buf, username)) {
	fclose(fp);
	return 1;
      }
    }
    fclose(fp);
  }
  return 0;
}

