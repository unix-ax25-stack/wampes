/* @(#) $Id: ftpserv.c,v 1.40 1998/04/28 01:34:07 deyke Exp $ */

/* Internet FTP Server
 * Copyright 1991 Phil Karn, KA9Q
 */

#define LINELEN         1024    /* Length of command buffer */

#include <sys/types.h>

#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "seek.h"
#include "seteugid.h"
#include "strerror.h"

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
static void ftpscs(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static void ftpscr(struct tcb *tcb, int32 cnt);
static void ftpsds(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static char *errmsg(const char *filename);
static void ftpcommand(struct ftp *ftp);
static int pport(struct socket *sock, char *arg);
static void ftplogin(struct ftp *ftp, char *pass);
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
	"xcwd",
	"appe",
	"cdup",
	"help",
	"mdtm",
	"noop",
	"rest",
	"size",
	"xcup",
	NULL
};

/* Response messages */
static char banner[] = "220 %s FTP version %s ready at %s\r\n";
static char badcmd[] = "500 Unknown command '%s'\r\n";
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
static void Xprintf(struct tcb *tcb,char *message,char *arg1,char *arg2,char *arg3)
{
	struct mbuf *bp;

	if(tcb == NULL)
		return;

	bp = alloc_mbuf(256);
	sprintf((char *) bp->data,message,arg1,arg2,arg3);
	bp->cnt = strlen((char *) bp->data);
	send_tcp(tcb,&bp);
}
/* Start up FTP service */
int
ftpstart(int argc,char *argv[],void *p)
{
	struct socket lsocket;

	if(ftp_tcb != NULL)
		close_tcp(ftp_tcb);
	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_FTP;
	else
		lsocket.port = tcp_port_number(argv[1]);

	ftp_tcb = open_tcp(&lsocket,NULL,TCP_SERVER,0,ftpscr,NULL,ftpscs,0,0);
	return 0;
}
/* FTP Server Control channel State change upcall handler */
static
void
ftpscs(struct tcb *tcb,enum tcp_state old,enum tcp_state new)
{
	struct ftp *ftp;
	char *cp,*cp1;

	switch(new){
	case TCP_ESTABLISHED:
		if((ftp = ftp_create(LINELEN)) == NULL){
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
		logmsg(tcb,"open FTP","");
		cp = ctime((time_t *) &Secclock);
		if((cp1 = strchr(cp,'\n')) != NULL)
			*cp1 = '\0';
		Xprintf(ftp->control,banner,Hostname,Version,cp);
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		break;
	case TCP_CLOSED:
		logmsg(tcb,"close FTP","");
		if((ftp = (struct ftp *)tcb->user) != NULL)
			ftp_delete(ftp);
		/* Check if server is being shut down */
		if(tcb == ftp_tcb)
			ftp_tcb = NULL;
		del_tcp(&tcb);
		break;
	default:
		break;
	}
}

/* FTP Server Control channel Receiver upcall handler */
static
void
ftpscr(struct tcb *tcb,int32 cnt)
{
	struct ftp *ftp;
	int c;
	struct mbuf *bp;

	if((ftp = (struct ftp *)tcb->user) == NULL){
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
ftpsds(struct tcb *tcb,enum tcp_state old,enum tcp_state new)
{
	struct ftp *ftp;

	if((ftp = (struct ftp *)tcb->user) == NULL){
		/* Unknown connection. Kill it */
		del_tcp(&tcb);
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
		ftp->fp = NULL;
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
			if(ftp->fp != NULL && ftp->fp != stdout)
				fclose(ftp->fp);
			ftp->fp = NULL;
			ftp->state = COMMAND_STATE;
			/* Kick command parser if something is waiting */
			if(ftp->control->rcvcnt != 0)
				ftpscr(ftp->control,ftp->control->rcvcnt);
		}
		/* Clear only if another transfer hasn't already started */
		if(ftp->data == tcb)
			ftp->data = NULL;
		del_tcp(&tcb);
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

#define AsUser(stmt)    strcpy(physname, ftp->root);               \
			strcat(physname, file);                    \
			seteugid(ftp->uid,ftp->gid);               \
			stmt;                                      \
			seteugid(0,0);

/* Parse and execute ftp commands */
static
void
ftpcommand(struct ftp *ftp)
{
	char *buf,*arg,*cp,**cmdp,*file;
	struct socket dport;
	int rest;
	int result;
	struct stat statbuf;
	char physname[1024];

	if(ftp->cnt == 0){
		/* Can't be a legal FTP command */
		Xprintf(ftp->control,badcmd,"","","");
		return;
	}
	buf = ftp->buf;

	/* Translate first word to lower case */
	for(cp = buf;*cp != ' ' && *cp != '\0';cp++)
		*cp = Xtolower(*cp);
	/* Find command in table; if not present, return syntax error */
	for(cmdp = commands;*cmdp != NULL;cmdp++)
		if(strncmp(*cmdp,buf,strlen(*cmdp)) == 0)
			break;
	if(*cmdp == NULL){
		logmsg(ftp->control,"%s",buf);
		Xprintf(ftp->control,badcmd,buf,"","");
		return;
	}
	/* Allow only USER, PASS and QUIT before logging in */
	if(ftp->cd == NULL || ftp->root == NULL){
		switch(cmdp-commands){
		case USER_CMD:
		case PASS_CMD:
		case QUIT_CMD:
			break;
		default:
			logmsg(ftp->control,"%s",buf);
			Xprintf(ftp->control,notlog,"","","");
			return;
		}
	}
	arg = &buf[strlen(*cmdp)];
	while(*arg == ' ')
		arg++;

	/* Execute specific command */
	switch(cmdp-commands){
	case USER_CMD:
		logmsg(ftp->control,"USER %s",arg);
		if(!strcmp(arg, "anonymous"))
			arg = "ftp";
		free(ftp->username);
		ftp->username = strdup(arg);
		Xprintf(ftp->control,givepass,"","","");
		break;
	case TYPE_CMD:
		logmsg(ftp->control,"TYPE %s",arg);
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
		logmsg(ftp->control,"QUIT","");
		Xprintf(ftp->control,bye,"","","");
		close_tcp(ftp->control);
		break;
	case RETR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		rest = ftp->rest;
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"RETR %s",file);
		AsUser(result = stat(physname, &statbuf));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
			FREE(file);
			return;
		}
		if(!S_ISREG(statbuf.st_mode)){
			Xprintf(ftp->control,"550 %s: not a plain file.\r\n",file,"","");
			FREE(file);
			return;
		}
		AsUser(ftp->fp = fopen(physname,"r"));
		if(ftp->fp == NULL ||
		   (rest && fseek(ftp->fp,rest,SEEK_SET))){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"RETR",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULL,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		FREE(file);
		break;
	case STOR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		rest = ftp->rest;
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"STOR %s",file);
		AsUser(ftp->fp = fopen(physname,"w"));
		if(ftp->fp == NULL ||
		   (rest && fseek(ftp->fp,rest,SEEK_SET))){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = RECEIVING_STATE;
			Xprintf(ftp->control,sending,"STOR",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,ftpdr,NULL,ftpsds,ftp->control->tos,(int)ftp);
		}
		FREE(file);
		break;
	case APPE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		ftp->rest = 0;
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"APPE %s",file);
		AsUser(ftp->fp = fopen(physname,"a"));
		if(ftp->fp == NULL){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = RECEIVING_STATE;
			Xprintf(ftp->control,sending,"APPE",arg,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,ftpdr,NULL,ftpsds,ftp->control->tos,(int)ftp);
		}
		FREE(file);
		break;
	case PORT_CMD:
		logmsg(ftp->control,"PORT %s",arg);
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
		logmsg(ftp->control,"LIST %s",file);
		AsUser(ftp->fp = dir(physname,1));
		if(ftp->fp == NULL){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"LIST",file,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULL,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		FREE(file);
		break;
	case NLST_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"NLST %s",file);
		AsUser(ftp->fp = dir(physname,0));
		if(ftp->fp == NULL){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"NLST",file,"");
			if (ftp->data) ftp->data->user = 0;
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULL,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		FREE(file);
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
		logmsg(ftp->control,"CWD  %s",file);
		AsUser(result = chdir(physname));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
			FREE(file);
		} else {
			chdir("/");
			Xprintf(ftp->control,pwdmsg,file,"","");
			if(ftp->cd) free(ftp->cd);
			ftp->cd = file;
		}
		break;
	case XPWD_CMD:
	case PWD_CMD:
		logmsg(ftp->control,"PWD","");
		Xprintf(ftp->control,pwdmsg,ftp->cd,"","");
		break;
	case ACCT_CMD:
		logmsg(ftp->control,"ACCT","");
		Xprintf(ftp->control,unimp,"","","");
		break;
	case DELE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"DELE %s",file);
		AsUser(result = remove(physname));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,deleok,"","","");
		}
		FREE(file);
		break;
	case PASS_CMD:
		logmsg(ftp->control,"PASS %s",arg);
		tcp_output(ftp->control);       /* Send the ack now */
		ftplogin(ftp,arg);
		break;
	case XMKD_CMD:
	case MKD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"MKD %s",file);
		AsUser(result = mkdir(physname,0755));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,mkdok,"","","");
		}
		FREE(file);
		break;
	case XRMD_CMD:
	case RMD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"RMD %s",file);
		AsUser(result = rmdir(physname));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			Xprintf(ftp->control,deleok,"","","");
		}
		FREE(file);
		break;
	case STRU_CMD:
		logmsg(ftp->control,"STRU %s",arg);
		if(Xtolower(arg[0]) != 'f')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	case MODE_CMD:
		logmsg(ftp->control,"MODE %s",arg);
		if(Xtolower(arg[0]) != 's')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	case SYST_CMD:
		logmsg(ftp->control,"SYST","");
		Xprintf(ftp->control,"215 UNIX Type: L8\r\n","","","");
		break;
	case HELP_CMD:
		logmsg(ftp->control,"HELP","");
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
		logmsg(ftp->control,"MDTM %s",file);
		AsUser(result = stat(physname, &statbuf));
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
		FREE(file);
		break;
	case NOOP_CMD:
		logmsg(ftp->control,"NOOP","");
		Xprintf(ftp->control,"200 NOOP command successful.\r\n","","","");
		break;
	case REST_CMD:
		ftp->rest = atoi(arg);
		logmsg(ftp->control,"REST %s",arg);
		Xprintf(ftp->control,"350 Restarting at %s. Send STORE or RETRIEVE to initiate transfer.\r\n",arg,"","");
		break;
	case SIZE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		logmsg(ftp->control,"SIZE %s",file);
		AsUser(result = stat(physname, &statbuf));
		if(result){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			char sizestr[80];
			sprintf(sizestr,"%ld",(long)statbuf.st_size);
			Xprintf(ftp->control,"213 %s\r\n",sizestr,"","");
		}
		FREE(file);
		break;
	default:
		logmsg(ftp->control,"%s",buf);
		Xprintf(ftp->control,unimp,"","","");
		break;
	}
}

/* Shut down FTP server */
int
ftp0(int argc,char *argv[],void *p)
{
	if(ftp_tcb != NULL)
		close_tcp(ftp_tcb);
	return 0;
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
		if((arg = strchr(arg,',')) == NULL)
			return -1;
		arg++;
	}
	sock->address = n;
	n = atoi(arg);
	if((arg = strchr(arg,',')) == NULL)
		return -1;
	arg++;
	n = atoi(arg) + (n << 8);
	sock->port = (uint16) n;
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

static void ftplogin(
struct ftp *ftp,
char *pass)
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
  ftp->uid = (int) pw->pw_uid;
  ftp->gid = (int) pw->pw_gid;
  if (ftp->cd) free(ftp->cd);
  ftp->cd = strdup(strcmp(pw->pw_name, "ftp") ? pw->pw_dir : "/");
  if (ftp->root) free(ftp->root);
  ftp->root = strdup(strcmp(pw->pw_name, "ftp") ? "" : pw->pw_dir);
  Xprintf(ftp->control, logged, pw->pw_name, "", "");
  logmsg(ftp->control, "%s logged in", pw->pw_name);
  return;

Fail:
  Xprintf(ftp->control, noperm, ftp->username, "", "");
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
