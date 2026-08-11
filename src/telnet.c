/* @(#) $Id: telnet.c,v 1.25 2016/03/13 07:14:39 dl9sau Exp $ */

/* Internet Telnet client
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "telnet.h"
#include "session.h"
#include "proc.h"
#include "icmp.h"
#include "tcp.h"
#include "tty.h"
#include "commands.h"
#include "internet.h"
#include "netuser.h"
#include "cmdparse.h"

static void unix_send_tel(char *buf, int n);
static void send_tel(char *buf, int n);
static void tel_input(struct telnet *tn, struct mbuf *bp);
static void tn_tx(struct tcb *tcb, int32 cnt);
static void t_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static void free_telnet(struct telnet *tn);
static void willopt(struct telnet *tn, int opt);
static void wontopt(struct telnet *tn, int opt);
static void doopt(struct telnet *tn, int opt);
static void dontopt(struct telnet *tn, int opt);
static void answer(struct telnet *tn, int r1, int r2);

int Refuse_echo = 0;
int Tn_cr_mode = 0;    /* if true turn <cr> to <cr-nul> */
int Topt = 0;

char *T_options[] = {
	"Transmit Binary",
	"Echo",
	"",
	"Suppress Go Ahead",
	"",
	"Status",
	"Timing Mark"
};

int
dotopt(
int argc,
char *argv[],
void *p)
{
	setbool(&Topt,"Telnet option tracing",argc,argv);
	return 0;
}
/* Execute user telnet command */
int
dotelnet(
int argc,
char *argv[],
void *p)
{
	struct session *s;
	struct telnet *tn;
	struct tcb *tcb;
	struct socket lsocket,fsocket;

	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	if((fsocket.address = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	}
	if(argc < 3)
		fsocket.port = IPPORT_TELNET;
	else
		fsocket.port = tcp_port_number(argv[2]);

	/* Allocate a session descriptor */
	if((s = newsession()) == NULL){
		printf("Too many sessions\n");
		return 1;
	}
	if((s->name = (char *) malloc((unsigned)strlen(argv[1])+1)) != NULL)
		strcpy(s->name,argv[1]);
	s->type = TELNET;
	if ((Refuse_echo == 0) && (Tn_cr_mode != 0)) {
		s->parse = unix_send_tel;
	} else {
		s->parse = send_tel;
	}
	Current = s;

	/* Create and initialize a Telnet protocol descriptor */
	if((tn = (struct telnet *)calloc(1,sizeof(struct telnet))) == NULL){
		printf("%s", Nospace);
		s->type = NO_SESSION;
		return 1;
	}
	tn->session = s;        /* Upward pointer */
	tn->state = TS_DATA;
	s->cb.telnet = tn;      /* Downward pointer */

	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,0,
	 rcv_char,tn_tx,t_state,0,(long)tn);

	tn->tcb = tcb;  /* Downward pointer */
	go(argc, argv, p);
	return 0;
}

/* Process typed characters */
static void
unix_send_tel(
char *buf,
int n)
{
	int i;

	for (i=0; (i<n) && (buf[i] != '\r'); i++)
		;
	if (buf[i] == '\r') {
		buf[i] = '\n';
		n = i+1;
	}
	send_tel(buf,n);
}
static void
send_tel(
char *buf,
int n)
{
	struct mbuf *bp;
	if(Current == NULL || Current->cb.telnet == NULL
	 || Current->cb.telnet->tcb == NULL)
		return;
	bp = qdata(buf,n);
	send_tcp(Current->cb.telnet->tcb,&bp);
	/* If we're doing our own echoing and recording is enabled, record it */
	if(n >= 2 && buf[n-2] == '\r' && buf[n-1] == '\n') {
	  buf[n-2] = '\n';
	  n--;
	}
	if(!Current->cb.telnet->remote[TN_ECHO] && Current->record != NULL)
		fwrite(buf,1,n,Current->record);
}

/* Process incoming TELNET characters */
static void
tel_input(
struct telnet *tn,
struct mbuf *bp)
{
	int c;
	FILE *record;

	record = tn->session->record;
	/* Optimization for very common special case -- no special chars */
	if(tn->state == TS_DATA){
		while(bp != NULL && memchr(bp->data,IAC,bp->cnt) == NULL){
			while(bp->cnt-- != 0){
				c = *bp->data++;
				putchar(c);
				if(record != NULL && c != '\r')
					putc(c,record);
			}
			free_mbuf(&bp);
		}
	}
	while((c = PULLCHAR(&bp)) != -1){
		switch(tn->state){
		case TS_DATA:
			if(c == IAC){
				tn->state = TS_IAC;
			} else {
				if(!tn->remote[TN_TRANSMIT_BINARY])
					c &= 0x7f;
				putchar(c);
				if(record != NULL && c != '\r')
					putc(c,record);
			}
			break;
		case TS_IAC:
			switch(c){
			case WILL:
				tn->state = TS_WILL;
				break;
			case WONT:
				tn->state = TS_WONT;
				break;
			case DO:
				tn->state = TS_DO;
				break;
			case DONT:
				tn->state = TS_DONT;
				break;
			case IAC:
				putchar(c);
				tn->state = TS_DATA;
				break;
			default:
				tn->state = TS_DATA;
				break;
			}
			break;
		case TS_WILL:
			willopt(tn,c & 0xff);
			tn->state = TS_DATA;
			break;
		case TS_WONT:
			wontopt(tn,c & 0xff);
			tn->state = TS_DATA;
			break;
		case TS_DO:
			doopt(tn,c & 0xff);
			tn->state = TS_DATA;
			break;
		case TS_DONT:
			dontopt(tn,c & 0xff);
			tn->state = TS_DATA;
			break;
		}
	}
}

/* Telnet receiver upcall routine */
void
rcv_char(
struct tcb *tcb,
int32 cnt)
{
	struct mbuf *bp;
	struct telnet *tn;
	FILE *record;

	if((tn = (struct telnet *)tcb->user) == NULL){
		/* Unknown connection; ignore it */
		return;
	}
	/* Hold output if we're not the current session */
	if(Mode != CONV_MODE || Current == NULL
	 || Current->type != TELNET || Current->cb.telnet != tn)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0)
		tel_input(tn,bp);

	if((record = tn->session->record) != NULL)
		fflush(record);
}
/* Handle transmit upcalls. Used only for file uploading */
static void
tn_tx(
struct tcb *tcb,
int32 cnt)
{
	struct telnet *tn;
	struct session *s = 0;
	struct mbuf *bp;
	int size;

	if((tn = (struct telnet *)tcb->user) == NULL
	 || (s = tn->session) == NULL
	 || s->upload == NULL)
		return;
	if((bp = alloc_mbuf((uint16) cnt)) == NULL)
		return;
	if((size = fread(bp->data,1,(unsigned) cnt,s->upload)) > 0){
		bp->cnt = (uint16)size;
		send_tcp(tcb,&bp);
	} else {
		free_p(&bp);
	}
	if(size != cnt){
		/* Error or end-of-file */
		fclose(s->upload);
		s->upload = NULL;
		free(s->ufile);
		s->ufile = NULL;
	}
}

/* State change upcall routine */
static void
t_state(
struct tcb *tcb,
enum tcp_state old,enum tcp_state new)
{
	struct telnet *tn;
	char notify = 0;

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	tn = (struct telnet *)tcb->user;

	if(Current != NULL && Current->type == TELNET && Current->cb.telnet == tn)
	{
		notify = 1;
		cooked();       /* prettify things... -- hyc */
	}

	switch(new){
	case TCP_CLOSE_WAIT:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		close_tcp(tcb);
		break;
	case TCP_CLOSED:    /* court adjourned */
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
		del_tcp(&tcb);
		if(tn != NULL)
			free_telnet(tn);
		break;
	default:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		break;
	}
}
/* Delete telnet structure */
static void
free_telnet(
struct telnet *tn)
{
	if(tn->session != NULL)
		freesession(tn->session);

	if(tn != NULL)
		free(tn);
}

int
doecho(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		if(Refuse_echo)
			printf("Refuse\n");
		else
			printf("Accept\n");
	} else {
		if(argv[1][0] == 'r')
			Refuse_echo = 1;
		else if(argv[1][0] == 'a')
			Refuse_echo = 0;
		else
			return -1;
	}
	return 0;
}
/* set for unix end of line for remote echo mode telnet */
int
doeol(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		if(Tn_cr_mode)
			printf("null\n");
		else
			printf("standard\n");
	} else {
		if(argv[1][0] == 'n')
			Tn_cr_mode = 1;
		else if(argv[1][0] == 's')
			Tn_cr_mode = 0;
		else {
			printf("Usage: %s [standard|null]\n",argv[0]);
			return -1;
		}
	}
	return 0;
}

/* The guts of the actual Telnet protocol: negotiating options */
static void
willopt(
struct telnet *tn,
int opt)
{
	int ack;

	if(Topt){
		printf("recv: will ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	switch(opt){
	case TN_TRANSMIT_BINARY:
	case TN_ECHO:
	case TN_SUPPRESS_GA:
		if(tn->remote[opt] == 1)
			return;         /* Already set, ignore to prevent loop */
		if(opt == TN_ECHO){
			if(Refuse_echo){
				/* User doesn't want to accept */
				ack = DONT;
				break;
			} else
				raw();          /* Put tty into raw mode */
		}
		tn->remote[opt] = 1;
		ack = DO;
		break;
	default:
		ack = DONT;     /* We don't know what he's offering; refuse */
	}
	answer(tn,ack,opt);
}
static void
wontopt(
struct telnet *tn,
int opt)
{
	if(Topt){
		printf("recv: wont ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	if(opt <= NOPTIONS){
		if(tn->remote[opt] == 0)
			return;         /* Already clear, ignore to prevent loop */
		tn->remote[opt] = 0;
		if(opt == TN_ECHO)
			cooked();       /* Put tty into cooked mode */
	}
	answer(tn,DONT,opt);    /* Must always accept */
}
static void
doopt(
struct telnet *tn,
int opt)
{
	int ack;

	if(Topt){
		printf("recv: do ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	switch(opt){
	case TN_SUPPRESS_GA:
		if(tn->local[opt] == 1)
			return;         /* Already set, ignore to prevent loop */
		tn->local[opt] = 1;
		ack = WILL;
		break;
	default:
		ack = WONT;     /* Don't know what it is */
	}
	answer(tn,ack,opt);
}
static void
dontopt(
struct telnet *tn,
int opt)
{
	if(Topt){
		printf("recv: dont ");
		if(opt <= NOPTIONS)
			printf("%s\n",T_options[opt]);
		else
			printf("%u\n",opt);
	}
	if(opt <= NOPTIONS){
		if(tn->local[opt] == 0){
			/* Already clear, ignore to prevent loop */
			return;
		}
		tn->local[opt] = 0;
	}
	answer(tn,WONT,opt);
}
static
void
answer(
struct telnet *tn,
int r1,int r2)
{
	struct mbuf *bp;
	char s[3];

	if(Topt){
		switch(r1){
		case WILL:
			printf("sent: will ");
			break;
		case WONT:
			printf("sent: wont ");
			break;
		case DO:
			printf("sent: do ");
			break;
		case DONT:
			printf("sent: dont ");
			break;
		}
		if(r2 <= NOPTIONS)
			printf("%s\n",T_options[r2]);
		else
			printf("%u\n",r2);
	}
	s[0] = (char) IAC;
	s[1] = (char) r1;
	s[2] = (char) r2;
	bp = qdata(s,(uint16)3);
	send_tcp(tn->tcb,&bp);
}
