/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/session.c,v 1.6 1991/02/24 20:17:37 deyke Exp $ */

/* NOS User Session control
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "socket.h"
#include "iface.h"
#include "tcp.h"
#include "netrom.h"
#include "axclient.h"
#include "ftp.h"
#include "telnet.h"
#include "finger.h"
#include "icmp.h"
#include "netuser.h"
#include "session.h"
#include "cmdparse.h"
#include "timer.h"
#include "commands.h"
#include "hardware.h"

struct session *Sessions;
struct session *Current;
char Notval[] = "Not a valid control block\n";
static char Badsess[] = "Invalid session\n";

/* Convert a character string containing a decimal session index number
 * into a pointer. If the arg is NULLCHAR, use the current default session.
 * If the index is out of range or unused, return NULLSESSION.
 */
static struct session *
sessptr(cp)
char *cp;
{
	register struct session *sp;
	unsigned int i;

	if(cp == NULLCHAR){
		sp = Current;
	} else {
		i = (unsigned)atoi(cp);
		if(i >= Nsessions)
			sp = NULLSESSION;
		else
			sp = &Sessions[i];
	}
	if(sp == NULLSESSION || sp->type == FREE)
		sp = NULLSESSION;

	return sp;
}

/* Select and display sessions */
int
dosession(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	sp = (struct session *)p;

	if(argc > 1){
		if((Current = sessptr(argv[1])) != NULLSESSION){
			go(0,NULL,sp);
		} else
			tprintf("Session %s not active\n",argv[1]);
		return 0;
	}
	tprintf(" #       &CB Type   Rcv-Q  State        Remote socket\n");
	for(sp=Sessions; sp < &Sessions[Nsessions];sp++){
		switch(sp->type){
		case TELNET:
			tprintf("%c%-3d%8lx Telnet  %4d  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.telnet->tcb,
			 sp->cb.telnet->tcb->rcvcnt,
			 Tcpstates[sp->cb.telnet->tcb->state],
			 pinet_tcp(&sp->cb.telnet->tcb->conn.remote));
			break;
		case FTP:
			tprintf("%c%-3d%8lx FTP     %4d  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.ftp->control,
			 sp->cb.ftp->control->rcvcnt,
			 Tcpstates[sp->cb.ftp->control->state],
			 pinet_tcp(&sp->cb.ftp->control->conn.remote));
			break;
#ifdef  AX25
		case AX25TNC:
			tprintf("%c%-3d%8lx AX25    %4d  %-13s%-s",
			 (Current == sp) ? '*' : ' ',
			 (int) (sp - Sessions),
			 (long) sp->cb.ax25,
			 sp->cb.ax25->rcvcnt,
			 ax25states[sp->cb.ax25->state],
			 pathtostr(sp->cb.ax25));
			break;
#endif
		case FINGER:
			tprintf("%c%-3d%8lx Finger  %4d  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.finger->tcb,
			 sp->cb.finger->tcb->rcvcnt,
			 Tcpstates[sp->cb.finger->tcb->state],
			 pinet_tcp(&sp->cb.finger->tcb->conn.remote));
			break;
#ifdef NETROM
		case NRSESSION:
			tprintf("%c%-3d%8lx NETROM  %4d  %-13s%-s",
			 (Current == sp) ? '*' : ' ',
			 (int) (sp - Sessions),
			 (long) sp->cb.netrom,
			 sp->cb.netrom->rcvcnt,
			 ax25states[sp->cb.netrom->state],
			 nr_addr2str(sp->cb.netrom));
			break;
#endif
		default:
			continue;
		}
		if(sp->rfile != NULLCHAR)
			tprintf("    Record: %s ",sp->rfile);
		if(sp->ufile != NULLCHAR)
			tprintf("    Upload: %s",sp->ufile);
		tprintf("\n");
	}
	return 0;
}
/* Enter conversational mode with current session */
int
go(argc,argv,p)
int argc;
char *argv[];
void *p;
{

	void rcv_char(),ftpccr(),fingcli_rcv();

	if(Current == NULLSESSION || Current->type == FREE)
		return 0;
	mode = CONV_MODE;
	switch(Current->type){
	case TELNET:
		if(Current->cb.telnet->remote[TN_ECHO])
			raw();  /* Re-establish raw mode if it was set */
		rcv_char(Current->cb.telnet->tcb,0); /* Get any pending input */
		break;
	case FTP:
		ftpccr(Current->cb.ftp->control,0);
		break;
#ifdef  AX25
	case AX25TNC:
		axclient_recv_upcall(Current->cb.ax25,0);
		break;
#endif
	case FINGER:
		fingcli_rcv(Current->cb.finger->tcb,0) ;
		break ;
#ifdef  NETROM
	case NRSESSION:
		nrclient_recv_upcall(Current->cb.netrom,0);
		break;
#endif
	}
	return 0;
}
int
doclose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		tprintf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		close_tcp(sp->cb.telnet->tcb);
		break;
	case FTP:
		close_tcp(sp->cb.ftp->control);
		break;
#ifdef  AX25
	case AX25TNC:
		close_ax(sp->cb.ax25);
		break;
#endif
	case FINGER:
		close_tcp(sp->cb.finger->tcb);
		break;
#ifdef NETROM
	case NRSESSION:
		close_nr(sp->cb.netrom);
		break;
#endif
	}
	return 0;
}
int
doreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		tprintf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		reset_tcp(sp->cb.telnet->tcb);
		break;
	case FTP:
		if(sp->cb.ftp->data != NULLTCB){
			reset_tcp(sp->cb.ftp->data);
			sp->cb.ftp->data = NULLTCB;
		}
		reset_tcp(sp->cb.ftp->control);
		break;
#ifdef  AX25
	case AX25TNC:
		reset_ax(sp->cb.ax25);
		break;
#endif
	case FINGER:
		reset_tcp(sp->cb.finger->tcb);
		break;
#ifdef NETROM
	case NRSESSION:
		reset_nr(sp->cb.netrom);
		break;
#endif
	}
	return 0;
}
int
dokick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULLCHAR)) == NULLSESSION){
		tprintf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		if(kick_tcp(sp->cb.telnet->tcb) == -1){
			tprintf(Notval);
			return 1;
		}
		break;
	case FTP:
		if(kick_tcp(sp->cb.ftp->control) == -1){

			tprintf(Notval);
			return 1;
		}
		if(sp->cb.ftp->data != NULLTCB)
			kick_tcp(sp->cb.ftp->data);
		break;
#ifdef  AX25
	case AX25TNC:
		break;
#endif
	case FINGER:
		if(kick_tcp(sp->cb.finger->tcb) == -1){
			tprintf(Notval);
			return 1;
		}
		break;
#ifdef NETROM
	case NRSESSION:
		break;
#endif
	}
	return 0;
}
struct session *
newsession()
{
	register int i;

	for(i=0;i<Nsessions;i++)
		if(Sessions[i].type == FREE)
			return &Sessions[i];
	return NULLSESSION;
}
void
freesession(sp)
struct session *sp;
{
	if(sp == NULLSESSION)
		return;
	if(sp->record != NULLFILE){
		fclose(sp->record);
		sp->record = NULLFILE;
	}
	if(sp->rfile != NULLCHAR){
		free(sp->rfile);
		sp->rfile = NULLCHAR;
	}
	if(sp->upload != NULLFILE){
		fclose(sp->upload);
		sp->upload = NULLFILE;
	}
	if(sp->ufile != NULLCHAR){
		free(sp->ufile);
		sp->ufile = NULLCHAR;
	}
	if(sp->name != NULLCHAR){
		free(sp->name);
		sp->name = NULLCHAR;
	}
	sp->type = FREE;
	memset(sp, 0, sizeof(struct session));
	if(sp == Current)
		Current = NULLSESSION;
}
/* Control session recording */
int
dorecord(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(Current == NULLSESSION){
		tprintf("No current session\n");
		return 1;
	}
	if(argc > 1){
		if(Current->rfile != NULLCHAR){
			fclose(Current->record);
			free(Current->rfile);
			Current->record = NULLFILE;
			Current->rfile = NULLCHAR;
		}
		/* Open new record file, unless file name is "off", which means
		 * disable recording
		 */
		if(strcmp(argv[1],"off") != 0
		 && (Current->record = fopen(argv[1],"a")) != NULLFILE){
			Current->rfile = malloc((unsigned)strlen(argv[1])+1);
			strcpy(Current->rfile,argv[1]);
		}
	}
	if(Current->rfile != NULLCHAR)
		tprintf("Recording into %s\n",Current->rfile);
	else
		tprintf("Recording off\n");
	return 0;
}
/* Control file transmission */
int
doupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct tcb *tcb;
	struct ax25_cb *axp;
	struct circuit *cb;

	if(Current == NULLSESSION){
		tprintf("No current session\n");
		return 1;
	}
	if(argc > 1){
		switch(Current->type){
		case FTP:
			tprintf("Uploading on FTP control channel not supported\n");
			return 1;
		case FINGER:
			tprintf("Uploading on FINGER session not supported\n");
			return 1;
		}
		/* Abort upload */
		if(Current->upload != NULLFILE){
			fclose(Current->upload);
			Current->upload = NULLFILE;
		}
		if(Current->ufile != NULLCHAR){
			free(Current->ufile);
			Current->ufile = NULLCHAR;
		}
		if(strcmp(argv[1],"stop") != 0){
			/* Open upload file */
			if((Current->upload = fopen(argv[1],"r")) == NULLFILE){
				tprintf("Can't read %s\n",argv[1]);
				return 1;
			}
			Current->ufile = malloc((unsigned)strlen(argv[1])+1);
			strcpy(Current->ufile,argv[1]);
			/* All set, kick transmit upcall to get things rolling */
			switch(Current->type){
#ifdef  AX25
			case AX25TNC:
				axp = Current->cb.ax25;
				axclient_send_upcall(axp, space_ax(axp));
				break;
#endif
#ifdef NETROM
			case NRSESSION:
				cb = Current->cb.netrom;
				nrclient_send_upcall(cb, space_nr(cb));
				break;
#endif
			case TELNET:
				tcb = Current->cb.telnet->tcb;
				if(tcb->snd.wnd > tcb->sndcnt)
					(*tcb->t_upcall)(tcb,tcb->snd.wnd - tcb->sndcnt);
				break;
			}
		}
	}
	if(Current->ufile != NULLCHAR)
		tprintf("Uploading %s\n",Current->ufile);
	else
		tprintf("Uploading off\n");
	return 0;
}

