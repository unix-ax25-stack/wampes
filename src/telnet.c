/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/telnet.c,v 1.16 1993/05/17 13:45:21 deyke Exp $ */

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
#include "netuser.h"

static void unix_send_tel(char *buf, int n);
static void send_tel(char *buf, int n);
static void tel_input(struct telnet *tn, struct mbuf *bp);
static void tn_tx(struct tcb *tcb, int cnt);
static void t_state(struct tcb *tcb, int old, int new);
static void free_telnet(struct telnet *tn);
static void willopt(struct telnet *tn, int opt);
static void wontopt(struct telnet *tn, int opt);
static void doopt(struct telnet *tn, int opt);
static void dontopt(struct telnet *tn, int opt);
static void answer(struct telnet *tn, int r1, int r2);

int Refuse_echo = 0;
int Tn_cr_mode = 0;    /* if true turn <cr> to <cr-nul> */

#ifdef  DEBUG
char *T_options[] = {
	"Transmit Binary",
	"Echo",
	"",
	"Suppress Go Ahead",
	"",
	"Status",
	"Timing Mark"
};
#endif

/* Execute user telnet command */
int
dotelnet(argc,argv,p)
int argc;
char *argv[];
void *p;
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
	if((s = newsession()) == NULLSESSION){
		printf("Too many sessions\n");
		return 1;
	}
	if((s->name = malloc((unsigned)strlen(argv[1])+1)) != NULLCHAR)
		strcpy(s->name,argv[1]);
	s->type = TELNET;
	if ((Refuse_echo == 0) && (Tn_cr_mode != 0)) {
		s->parse = unix_send_tel;
	} else {
		s->parse = send_tel;
	}
	Current = s;

	/* Create and initialize a Telnet protocol descriptor */
	if((tn = (struct telnet *)calloc(1,sizeof(struct telnet))) == NULLTN){
		printf(Nospace);
		s->type = FREE;
		return 1;
	}
	tn->session = s;        /* Upward pointer */
	tn->state = TS_DATA;
	s->cb.telnet = tn;      /* Downward pointer */

	tcb = open_tcp(&lsocket,&fsocket,TCP_ACTIVE,0,
	 rcv_char,tn_tx,t_state,0,(int)tn);

	tn->tcb = tcb;  /* Downward pointer */
	go(argc, argv, p);
	return 0;
}

/* Process typed characters */
static void
unix_send_tel(buf,n)
char *buf;
int n;
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
send_tel(buf,n)
char *buf;
int n;
{
	if(Current == NULLSESSION || Current->cb.telnet == NULLTN
	 || Current->cb.telnet->tcb == NULLTCB)
		return;
	send_tcp(Current->cb.telnet->tcb,qdata(buf,n));
	/* If we're doing our own echoing and recording is enabled, record it */
	if(n >= 2 && buf[n-2] == '\r' && buf[n-1] == '\n') {
	  buf[n-2] = '\n';
	  n--;
	}
	if(!Current->cb.telnet->remote[TN_ECHO] && Current->record != NULLFILE)
		fwrite(buf,1,n,Current->record);
}

/* Process incoming TELNET characters */
static void
tel_input(tn,bp)
register struct telnet *tn;
struct mbuf *bp;
{
	int c;
	FILE *record;

	record = tn->session->record;
	/* Optimization for very common special case -- no special chars */
	if(tn->state == TS_DATA){
		while(bp != NULLBUF && memchr(bp->data,IAC,bp->cnt) == NULLCHAR){
			while(bp->cnt-- != 0){
				c = *bp->data++;
				putchar(c);
				if(record != NULLFILE && c != '\r')
					putc(c,record);
			}
			bp = free_mbuf(bp);
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
				if(record != NULLFILE && c != '\r')
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
			willopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_WONT:
			wontopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DO:
			doopt(tn,c);
			tn->state = TS_DATA;
			break;
		case TS_DONT:
			dontopt(tn,c);
			tn->state = TS_DATA;
			break;
		}
	}
}

/* Telnet receiver upcall routine */
void
rcv_char(tcb,cnt)
register struct tcb *tcb;
int cnt;
{
	struct mbuf *bp;
	struct telnet *tn;
	FILE *record;

	if((tn = (struct telnet *)tcb->user) == NULLTN){
		/* Unknown connection; ignore it */
		return;
	}
	/* Hold output if we're not the current session */
	if(Mode != CONV_MODE || Current == NULLSESSION
	 || Current->type != TELNET || Current->cb.telnet != tn)
		return;

	if(recv_tcp(tcb,&bp,cnt) > 0)
		tel_input(tn,bp);

	if((record = tn->session->record) != NULLFILE)
		fflush(record);
}
/* Handle transmit upcalls. Used only for file uploading */
static void
tn_tx(tcb,cnt)
struct tcb *tcb;
uint16 cnt;
{
	struct telnet *tn;
	struct session *s;
	struct mbuf *bp;
	int size;

	if((tn = (struct telnet *)tcb->user) == NULLTN
	 || (s = tn->session) == NULLSESSION
	 || s->upload == NULLFILE)
		return;
	if((bp = alloc_mbuf(cnt)) == NULLBUF)
		return;
	if((size = fread(bp->data,1,cnt,s->upload)) > 0){
		bp->cnt = (uint16)size;
		send_tcp(tcb,bp);
	} else {
		free_p(bp);
	}
	if(size != cnt){
		/* Error or end-of-file */
		fclose(s->upload);
		s->upload = NULLFILE;
		free(s->ufile);
		s->ufile = NULLCHAR;
	}
}

/* State change upcall routine */
static void
t_state(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	struct telnet *tn;
	char notify = 0;

	/* Can't add a check for unknown connection here, it would loop
	 * on a close upcall! We're just careful later on.
	 */
	tn = (struct telnet *)tcb->user;

	if(Current != NULLSESSION && Current->type == TELNET && Current->cb.telnet == tn)
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
		if(tn != NULLTN)
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
free_telnet(tn)
struct telnet *tn;
{
	if(tn->session != NULLSESSION)
		freesession(tn->session);

	if(tn != NULLTN)
		free((char *)tn);
}

int
doecho(argc,argv,p)
int argc;
char *argv[];
void *p;
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
doeol(argc,argv,p)
int argc;
char *argv[];
void *p;
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
willopt(tn,opt)
struct telnet *tn;
int opt;
{
	int ack;

#ifdef  DEBUG
	printf("recv: will ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",T_options[opt]);
	else
		printf("%u\n",opt);
#endif

	switch(uchar(opt)){
	case TN_TRANSMIT_BINARY:
	case TN_ECHO:
	case TN_SUPPRESS_GA:
		if(tn->remote[uchar(opt)] == 1)
			return;         /* Already set, ignore to prevent loop */
		if(uchar(opt) == TN_ECHO){
			if(Refuse_echo){
				/* User doesn't want to accept */
				ack = DONT;
				break;
			} else
				raw();          /* Put tty into raw mode */
		}
		tn->remote[uchar(opt)] = 1;
		ack = DO;
		break;
	default:
		ack = DONT;     /* We don't know what he's offering; refuse */
	}
	answer(tn,ack,opt);
}
static void
wontopt(tn,opt)
struct telnet *tn;
int opt;
{
#ifdef  DEBUG
	printf("recv: wont ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",T_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	if(uchar(opt) <= NOPTIONS){
		if(tn->remote[uchar(opt)] == 0)
			return;         /* Already clear, ignore to prevent loop */
		tn->remote[uchar(opt)] = 0;
		if(uchar(opt) == TN_ECHO)
			cooked();       /* Put tty into cooked mode */
	}
	answer(tn,DONT,opt);    /* Must always accept */
}
static void
doopt(tn,opt)
struct telnet *tn;
int opt;
{
	int ack;

#ifdef  DEBUG
	printf("recv: do ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",T_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	switch(uchar(opt)){
#ifdef  FUTURE  /* Use when local options are implemented */
		if(tn->local[uchar(opt)] == 1)
			return;         /* Already set, ignore to prevent loop */
		tn->local[uchar(opt)] = 1;
		ack = WILL;
		break;
#endif
	default:
		ack = WONT;     /* Don't know what it is */
	}
	answer(tn,ack,opt);
}
static void
dontopt(tn,opt)
struct telnet *tn;
int opt;
{
#ifdef  DEBUG
	printf("recv: dont ");
	if(uchar(opt) <= NOPTIONS)
		printf("%s\n",T_options[uchar(opt)]);
	else
		printf("%u\n",uchar(opt));
#endif
	if(uchar(opt) <= NOPTIONS){
		if(tn->local[uchar(opt)] == 0){
			/* Already clear, ignore to prevent loop */
			return;
		}
		tn->local[uchar(opt)] = 0;
	}
	answer(tn,WONT,opt);
}
static
void
answer(tn,r1,r2)
struct telnet *tn;
int r1,r2;
{
	struct mbuf *bp;
	char s[3];

#ifdef  DEBUG
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
	if(r2 <= 6)
		printf("%s\n",T_options[r2]);
	else
		printf("%u\n",r2);
#endif

	s[0] = IAC;
	s[1] = r1;
	s[2] = r2;
	bp = qdata(s,(uint16)3);
	send_tcp(tn->tcb,bp);
}
