/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ftpserv.c,v 1.12 1991/07/16 17:55:11 deyke Exp $ */

/* Internet FTP Server
 * Copyright 1991 Phil Karn, KA9Q
 */

#define LINELEN         128     /* Length of command buffer */

#include <sys/types.h>

#include <stdio.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "dirutil.h"
#include "login.h"
#include "ftp.h"

static void Xprintf __ARGS((struct tcb *tcb, char *message, char *arg1, char *arg2, char *arg3));
static void ftpscs __ARGS((struct tcb *tcb, int old, int new));
static void ftpscr __ARGS((struct tcb *tcb, int cnt));
static void ftpsds __ARGS((struct tcb *tcb, int old, int new));
static char *errmsg __ARGS((char *filename));
static void ftpcommand __ARGS((struct ftp *ftp));
static int pport __ARGS((struct socket *sock, char *arg));
static void ftplogin __ARGS((struct ftp *ftp, char *pass));
static int permcheck __ARGS((struct ftp *ftp, char *file));

/* Command table */
static char *commands[] = {
	"user",
#define USER_CMD        0
	"acct",
#define ACCT_CMD        1
	"pass",
#define PASS_CMD        2
	"type",
#define TYPE_CMD        3
	"list",
#define LIST_CMD        4
	"cwd",
#define CWD_CMD         5
	"dele",
#define DELE_CMD        6
	"name",
#define NAME_CMD        7
	"quit",
#define QUIT_CMD        8
	"retr",
#define RETR_CMD        9
	"stor",
#define STOR_CMD        10
	"port",
#define PORT_CMD        11
	"nlst",
#define NLST_CMD        12
	"pwd",
#define PWD_CMD         13
	"xpwd",                 /* For compatibility with 4.2BSD */
#define XPWD_CMD        14
	"mkd ",
#define MKD_CMD         15
	"xmkd",                 /* For compatibility with 4.2BSD */
#define XMKD_CMD        16
	"xrmd",                 /* For compatibility with 4.2BSD */
#define XRMD_CMD        17
	"rmd ",
#define RMD_CMD         18
	"stru",
#define STRU_CMD        19
	"mode",
#define MODE_CMD        20
	NULLCHAR
};

/* Response messages */
static char banner[] = "220 %s FTP version %s ready at %s\r\n";
static char badcmd[] = "500 Unknown command\r\n";
static char unsupp[] = "500 Unsupported command or option\r\n";
static char givepass[] = "331 Enter PASS command\r\n";
static char logged[] = "230 User \"%s\" logged in\r\n";
static char typeok[] = "200 Type set to \"%s\"\r\n";
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
static void Xprintf(tcb,message,arg1,arg2,arg3)
struct tcb *tcb;
char *message,*arg1,*arg2,*arg3;
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
ftpstart(argc,argv,p)
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
ftp0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(ftp_tcb != NULLTCB)
		close_tcp(ftp_tcb);
	return 0;
}
/* FTP Server Control channel State change upcall handler */
static
void
ftpscs(tcb,old,new)
struct tcb *tcb;
char old,new;
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
ftpscr(tcb,cnt)
struct tcb *tcb;
int16 cnt;
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
ftpsds(tcb,old,new)
struct tcb *tcb;
char old,new;
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
#ifdef  CPM
		if(ftp->type == ASCII_TYPE)
			putc(CTLZ,ftp->fp);
#endif
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

static char  *errmsg(filename)
char  *filename;
{

  extern char  *sys_errlist[];
  extern int  errno;

  static char  buf[1024];

  sprintf(buf, "550 %s: %s.\r\n", filename, sys_errlist[errno]);
  return buf;
}

/*---------------------------------------------------------------------------*/

#define switch2user()   setresuid(ftp->uid, ftp->uid, 0); \
			setresgid(ftp->gid, ftp->gid, 0)

#define switchback()    setresuid(0, 0, 0); \
			setresgid(0, 0, 0)

#define checkperm()     if (!permcheck(ftp, file)) {                   \
			  Xprintf(ftp->control, noperm, file, "", ""); \
			  free(file);                                  \
			  return;                                      \
			}

/* Parse and execute ftp commands */
static
void
ftpcommand(ftp)
register struct ftp *ftp;
{
	char *cmd,*arg,*cp,**cmdp,*file;
	char *mode;
	int ok;
	struct socket dport;

	cmd = ftp->buf;
	if(ftp->cnt == 0){
		/* Can't be a legal FTP command */
		Xprintf(ftp->control,badcmd,"","","");
		return;
	}
	cmd = ftp->buf;

#ifdef  UNIX
	/* Translate first word to lower case */
	for(cp = cmd;*cp != ' ' && *cp != '\0';cp++)
		*cp = tolower(*cp);
#else
	/* Translate entire buffer to lower case */
	for(cp = cmd;*cp != '\0';cp++)
		*cp = tolower(*cp);
#endif
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
		if(ftp->username)
			free(ftp->username);
		if((ftp->username = strdup(arg)) == NULLCHAR){
			close_tcp(ftp->control);
			break;
		}
		Xprintf(ftp->control,givepass,"","","");
		break;
	case TYPE_CMD:
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
			}       /* Note fall-thru */
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
		Xprintf(ftp->control,bye,"","","");
		close_tcp(ftp->control);
		break;
	case RETR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		if(ftp->type == IMAGE_TYPE)
			mode = READ_BINARY;
		else
			mode = "r";
		switch2user();
		ftp->fp = fopen(file,mode);
		switchback();
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			log(ftp->control,"RETR %s",file);
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"RETR",arg,"");
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case STOR_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		if(ftp->type == IMAGE_TYPE)
			mode = WRITE_BINARY;
		else
			mode = "w";
		switch2user();
		ftp->fp = fopen(file,mode);
		switchback();
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			log(ftp->control,"STOR %s",file);
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = RECEIVING_STATE;
			Xprintf(ftp->control,sending,"STOR",arg,"");
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
#ifndef CPM
	case LIST_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ftp->fp = dir(file,1);
		switchback();
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"LIST",file,"");
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case NLST_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ftp->fp = dir(file,0);
		switchback();
		if(ftp->fp == NULLFILE){
			Xprintf(ftp->control,errmsg(file),"","","");
		} else {
			dport.address = INADDR_ANY;
			dport.port = IPPORT_FTPD;
			ftp->state = SENDING_STATE;
			Xprintf(ftp->control,sending,"NLST",file,"");
			ftp->data = open_tcp(&dport,&ftp->port,TCP_ACTIVE,
			 0,NULLVFP,ftpdt,ftpsds,ftp->control->tos,(int)ftp);
		}
		free(file);
		break;
	case CWD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ok = (chdir(file) == 0);
		switchback();
		if(ok){
			chdir("/");
			Xprintf(ftp->control,pwdmsg,file,"","");
			if(ftp->cd) free(ftp->cd);
			ftp->cd = file;
		} else {
			Xprintf(ftp->control,errmsg(file),"","","");
			free(file);
		}
		break;
	case XPWD_CMD:
	case PWD_CMD:
		Xprintf(ftp->control,pwdmsg,ftp->cd,"","");
		break;
#else
	case LIST_CMD:
	case NLST_CMD:
	case CWD_CMD:
	case XPWD_CMD:
	case PWD_CMD:
#endif
	case ACCT_CMD:
		Xprintf(ftp->control,unimp,"","","");
		break;
	case DELE_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ok = (unlink(file) == 0);
		switchback();
		if(ok){
			log(ftp->control,"DELE %s",file);
			Xprintf(ftp->control,deleok,"","","");
		} else {
			Xprintf(ftp->control,errmsg(file),"","","");
		}
		free(file);
		break;
	case PASS_CMD:
		tcp_output(ftp->control);       /* Send the ack now */
		ftplogin(ftp,arg);
		break;
#ifndef CPM
	case XMKD_CMD:
	case MKD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ok = (mkdir(file,0755) == 0);
		switchback();
		if(ok){
			log(ftp->control,"MKD %s",file);
			Xprintf(ftp->control,mkdok,"","","");
		} else {
			Xprintf(ftp->control,errmsg(file),"","","");
		}
		free(file);
		break;
	case XRMD_CMD:
	case RMD_CMD:
		/* Disk operation; return ACK now */
		tcp_output(ftp->control);
		file = pathname(ftp->cd,arg);
		checkperm();
		switch2user();
		ok = (rmdir(file) == 0);
		switchback();
		if(ok){
			log(ftp->control,"RMD %s",file);
			Xprintf(ftp->control,deleok,"","","");
		} else {
			Xprintf(ftp->control,errmsg(file),"","","");
		}
		free(file);
		break;
	case STRU_CMD:
		if(tolower(arg[0]) != 'f')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	case MODE_CMD:
		if(tolower(arg[0]) != 's')
			Xprintf(ftp->control,unsupp,"","","");
		else
			Xprintf(ftp->control,okay,"","","");
		break;
	}
#endif
}
static
int
pport(sock,arg)
struct socket *sock;
char *arg;
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

#ifdef SPASSWD
#include <shadow.h>
#endif

/* Attempt to log in the user whose name is in ftp->username and password
 * in pass
 */

static void ftplogin(ftp, pass)
struct ftp *ftp;
char  *pass;
{

  char  *username = ftp->username;
  struct passwd *pw;

#ifdef RESTRICTED       /* because of no setresuid/gid */
  username = "ftp";
#endif

#ifdef SPASSWD
  struct spwd *sw;

 if ((pw = getpasswdentry(username, 0)) &&
     (sw = getspwdentry(username)) &&
     (!*sw->sp_pwdp || !strcmp(pw->pw_name, "ftp")))
#else
  if ((pw = getpasswdentry(username, 0)) &&
      (!*pw->pw_passwd || !strcmp(pw->pw_name, "ftp")))
#endif
  {
    ftp->uid = pw->pw_uid;
    ftp->gid = pw->pw_gid;
    if (ftp->cd) free(ftp->cd);
    ftp->cd = strdup(pw->pw_dir);
    if (ftp->path) free(ftp->path);
    ftp->path = strdup(strcmp(pw->pw_name, "ftp") ? "/" : pw->pw_dir);
    Xprintf(ftp->control, logged, pw->pw_name, "", "");
    log(ftp->control, "%s logged in", pw->pw_name);
  } else
    Xprintf(ftp->control, noperm, username, "", "");
}

/*---------------------------------------------------------------------------*/

/* Return 1 if the file operation is allowed, 0 otherwise */

static int  permcheck(ftp, file)
struct ftp *ftp;
char  *file;
{
  if (file == NULLCHAR || ftp->path == NULLCHAR) return 0;

  /* The target file must be under the user's allowed search path */

  if (strncmp(file, ftp->path, strlen(ftp->path))) return 0;
  return 1;
}

