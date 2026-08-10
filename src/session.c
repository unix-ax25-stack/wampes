/* @(#) $Id: session.c,v 1.23 1999/02/11 19:26:49 deyke Exp $ */

/* NOS User Session control
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "tcp.h"
#include "netuser.h"
#include "ftp.h"
#include "icmp.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "socket.h"
#include "cmdparse.h"
#include "commands.h"
#include "main.h"
#include "axclient.h"
#include "finger.h"
#include "netrom.h"

struct session *Sessions;
struct session *Current;
char Notval[] = "Not a valid control block\n";
static char Badsess[] = "Invalid session\n";

static struct session *sessptr(char *cp);

/* Convert a character string containing a decimal session index number
 * into a pointer. If the arg is NULL, use the current default session.
 * If the index is out of range or unused, return NULL.
 */
static struct session *
sessptr(
char *cp)
{
	register struct session *sp;
	unsigned int i;

	if(cp == NULL){
		sp = Current;
	} else {
		i = (unsigned)atoi(cp);
		if(i >= Nsessions)
			sp = NULL;
		else
			sp = &Sessions[i];
	}
	if(sp == NULL || sp->type == NO_SESSION)
		sp = NULL;

	return sp;
}

/* Select and display sessions */
int
dosession(
int argc,
char *argv[],
void *p)
{
	struct session *sp;

	sp = (struct session *)p;

	if(argc > 1){
		if((Current = sessptr(argv[1])) != NULL){
			go(0,NULL,sp);
		} else
			printf("Session %s not active\n",argv[1]);
		return 0;
	}
	printf(" #       &CB Type   Rcv-Q  State        Remote socket\n");
	for(sp=Sessions; sp < &Sessions[Nsessions];sp++){
		switch(sp->type){
		case TELNET:
			printf("%c%-3d%8lx Telnet  %4ld  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.telnet->tcb,
			 (long)sp->cb.telnet->tcb->rcvcnt,
			 Tcpstates[sp->cb.telnet->tcb->state],
			 pinet_tcp(&sp->cb.telnet->tcb->conn.remote));
			break;
		case FTP:
			printf("%c%-3d%8lx FTP     %4ld  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.ftp->control,
			 (long)sp->cb.ftp->control->rcvcnt,
			 Tcpstates[sp->cb.ftp->control->state],
			 pinet_tcp(&sp->cb.ftp->control->conn.remote));
			break;
		case AX25TNC:
			printf("%c%-3d%8lx AX25    %4d  %-13s%-s",
			 (Current == sp) ? '*' : ' ',
			 (int) (sp - Sessions),
			 (long) sp->cb.ax25,
			 len_p(sp->cb.ax25->rxq),
			 Ax25states[sp->cb.ax25->state],
			 ax25hdr_to_string(&sp->cb.ax25->hdr));
			break;
		case FINGER:
			printf("%c%-3d%8lx Finger  %4ld  %-13s%-s",
			 (Current == sp)? '*':' ',
			 (int)(sp - Sessions),
			 (long)sp->cb.finger->tcb,
			 (long)sp->cb.finger->tcb->rcvcnt,
			 Tcpstates[sp->cb.finger->tcb->state],
			 pinet_tcp(&sp->cb.finger->tcb->conn.remote));
			break;
		case NRSESSION:
			printf("%c%-3d%8lx NETROM  %4d  %-13s%-s",
			 (Current == sp) ? '*' : ' ',
			 (int) (sp - Sessions),
			 (long) sp->cb.netrom,
			 sp->cb.netrom->rcvcnt,
			 Nr4states[sp->cb.netrom->state],
			 nr_addr2str(sp->cb.netrom));
			break;
		default:
			continue;
		}
		if(sp->rfile != NULL)
			printf("    Record: %s ",sp->rfile);
		if(sp->ufile != NULL)
			printf("    Upload: %s",sp->ufile);
		printf("\n");
	}
	return 0;
}
/* Enter conversational mode with current session */
int
go(
int argc,
char *argv[],
void *p)
{
	if(Current == NULL || Current->type == NO_SESSION)
		return 0;
	Mode = CONV_MODE;
	switch(Current->type){
	case TELNET:
		if(Current->cb.telnet->remote[TN_ECHO])
			raw();  /* Re-establish raw mode if it was set */
		rcv_char(Current->cb.telnet->tcb,0); /* Get any pending input */
		break;
	case FTP:
		ftpccr(Current->cb.ftp->control,0);
		break;
	case AX25TNC:
		axclient_recv_upcall(Current->cb.ax25,0);
		break;
	case FINGER:
		fingcli_rcv(Current->cb.finger->tcb,0) ;
		break ;
	case NRSESSION:
		nrclient_recv_upcall(Current->cb.netrom,0);
		break;
	default:
		break;
	}
	return 0;
}
int
doclose(
int argc,
char *argv[],
void *p)
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULL)) == NULL){
		printf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		close_tcp(sp->cb.telnet->tcb);
		break;
	case FTP:
		close_tcp(sp->cb.ftp->control);
		break;
	case AX25TNC:
		disc_ax25(sp->cb.ax25);
		break;
	case FINGER:
		close_tcp(sp->cb.finger->tcb);
		break;
	case NRSESSION:
		close_nr(sp->cb.netrom);
		break;
	default:
		break;
	}
	return 0;
}
int
doreset(
int argc,
char *argv[],
void *p)
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULL)) == NULL){
		printf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		reset_tcp(sp->cb.telnet->tcb);
		break;
	case FTP:
		if(sp->cb.ftp->data != NULL){
			reset_tcp(sp->cb.ftp->data);
			sp->cb.ftp->data = NULL;
		}
		reset_tcp(sp->cb.ftp->control);
		break;
	case AX25TNC:
		reset_ax25(sp->cb.ax25);
		break;
	case FINGER:
		reset_tcp(sp->cb.finger->tcb);
		break;
	case NRSESSION:
		reset_nr(sp->cb.netrom);
		break;
	default:
		break;
	}
	return 0;
}
int
dokick(
int argc,
char *argv[],
void *p)
{
	struct session *sp;

	if((sp = sessptr(argc > 1 ? argv[1] : NULL)) == NULL){
		printf(Badsess);
		return -1;
	}
	switch(sp->type){
	case TELNET:
		if(kick_tcp(sp->cb.telnet->tcb) == -1){
			printf(Notval);
			return 1;
		}
		break;
	case FTP:
		if(kick_tcp(sp->cb.ftp->control) == -1){

			printf(Notval);
			return 1;
		}
		if(sp->type == FTP &&
		    sp->cb.ftp != NULL &&
		    sp->cb.ftp->data != NULL)
			kick_tcp(sp->cb.ftp->data);
		break;
	case AX25TNC:
		if(kick_ax25(sp->cb.ax25) == -1){
			printf(Notval);
			return 1;
		}
		break;
	case FINGER:
		if(kick_tcp(sp->cb.finger->tcb) == -1){
			printf(Notval);
			return 1;
		}
		break;
	case NRSESSION:
		if(kick_nr(sp->cb.netrom) == -1){
			printf(Notval);
			return 1;
		}
		break;
	default:
		break;
	}
	return 0;
}
struct session *
newsession(void)
{
	register int i;

	for(i=0;i<Nsessions;i++)
		if(Sessions[i].type == NO_SESSION)
			return &Sessions[i];
	return NULL;
}
void
freesession(
struct session *sp)
{
	if(sp == NULL)
		return;
	if(sp->record != NULL){
		fclose(sp->record);
		sp->record = NULL;
	}
	if(sp->rfile != NULL){
		free(sp->rfile);
		sp->rfile = NULL;
	}
	if(sp->upload != NULL){
		fclose(sp->upload);
		sp->upload = NULL;
	}
	if(sp->ufile != NULL){
		free(sp->ufile);
		sp->ufile = NULL;
	}
	if(sp->name != NULL){
		free(sp->name);
		sp->name = NULL;
	}
	sp->type = NO_SESSION;
	memset(sp, 0, sizeof(struct session));
	if(sp == Current)
		Current = NULL;
}
/* Control session recording */
int
dorecord(
int argc,
char *argv[],
void *p)
{
	if(Current == NULL){
		printf("No current session\n");
		return 1;
	}
	if(argc > 1){
		if(Current->rfile != NULL){
			fclose(Current->record);
			free(Current->rfile);
			Current->record = NULL;
			Current->rfile = NULL;
		}
		/* Open new record file, unless file name is "off", which means
		 * disable recording
		 */
		if(strcmp(argv[1],"off") != 0
		 && (Current->record = fopen(argv[1],"a")) != NULL){
			Current->rfile = (char *) malloc((unsigned)strlen(argv[1])+1);
			strcpy(Current->rfile,argv[1]);
		}
	}
	if(Current->rfile != NULL)
		printf("Recording into %s\n",Current->rfile);
	else
		printf("Recording off\n");
	return 0;
}
/* Control file transmission */
int
doupload(
int argc,
char *argv[],
void *p)
{
	struct tcb *tcb;
	struct ax25_cb *axp;
	struct circuit *cb;

	if(Current == NULL){
		printf("No current session\n");
		return 1;
	}
	if(argc > 1){
		switch(Current->type){
		case FTP:
			printf("Uploading on FTP control channel not supported\n");
			return 1;
		case FINGER:
			printf("Uploading on FINGER session not supported\n");
			return 1;
		default:
			break;
		}
		/* Abort upload */
		if(Current->upload != NULL){
			fclose(Current->upload);
			Current->upload = NULL;
		}
		if(Current->ufile != NULL){
			free(Current->ufile);
			Current->ufile = NULL;
		}
		if(strcmp(argv[1],"stop") != 0){
			/* Open upload file */
			if((Current->upload = fopen(argv[1],"r")) == NULL){
				printf("Can't read %s\n",argv[1]);
				return 1;
			}
			Current->ufile = (char *) malloc((unsigned)strlen(argv[1])+1);
			strcpy(Current->ufile,argv[1]);
			/* All set, kick transmit upcall to get things rolling */
			switch(Current->type){
			case AX25TNC:
				axp = Current->cb.ax25;
				axclient_send_upcall(axp, space_ax25(axp));
				break;
			case NRSESSION:
				cb = Current->cb.netrom;
				nrclient_send_upcall(cb, space_nr(cb));
				break;
			case TELNET:
				tcb = Current->cb.telnet->tcb;
				if(tcb->snd.wnd > tcb->sndcnt)
					(*tcb->t_upcall)(tcb,tcb->snd.wnd - tcb->sndcnt);
				break;
			default:
				break;
			}
		}
	}
	if(Current->ufile != NULL)
		printf("Uploading %s\n",Current->ufile);
	else
		printf("Uploading off\n");
	return 0;
}

