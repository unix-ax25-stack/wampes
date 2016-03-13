/* @(#) $Id: finger.c,v 1.18 2016/03/13 07:14:38 dl9sau Exp $ */

/*
 *
 *      Finger support...
 *
 *      Finger client routines.  Written by Michael T. Horne - KA7AXD.
 *      Copyright 1988 by Michael T. Horne, All Rights Reserved.
 *      Permission granted for non-commercial use and copying, provided
 *      that this notice is retained.
 *
 */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "timer.h"
#include "internet.h"
#include "icmp.h"
#include "netuser.h"
#include "tcp.h"
#include "ftp.h"
#include "telnet.h"
#include "iface.h"
#include "ax25.h"
#include "finger.h"
#include "session.h"

static struct finger *alloc_finger(void);
static int free_finger(struct finger *finger);
static void f_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new);

/*
 *
 *      Open up a socket to a remote (or the local) host on its finger port.
 *
 */

int
dofinger(
int argc,
char *argv[],
void *p)
{
	struct session *s;
	struct tcb *tcb;
	struct socket lsocket;
	struct socket fsocket;
	struct finger *finger;
	char *host;

	if (argc < 2) {
		printf("usage: %s [user | user@host | @host]\n", argv[0]);
		return(1);
	}

	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;

/*
 *      Extract user/host information.  It can be of the form:
 *
 *      finger user,                    # finger local user
 *      finger user@host,               # finger remote user
 *      finger @host                    # finger host (give system status)
 *
 */
	if ((finger = alloc_finger()) == NULL)
		return(1);

	if ((host = strrchr(argv[1], '@')) == NULL) {
		fsocket.address = Loopback.addr;/* no host, use local */
		if ((finger->user = (char *) malloc(strlen(argv[1]) + 3)) == NULL) {
			free_finger(finger);
			return(1);
		}
		strcpy(finger->user, argv[1]);
		strcat(finger->user, "\015\012");
	}
	else {
		*host++ = '\0';         /* null terminate user name */
		if (*host == '\0') {    /* must specify host */
			printf("%s: no host specified\n", argv[0]);
			printf("usage: %s [user | user@host | @host]\n",
				argv[0]);
			free_finger(finger);
			return(1);
		}
		if ((fsocket.address = resolve(host)) == 0) {
			printf("%s: ", argv[0]);
			printf(Badhost, host);
			free_finger(finger);
			return(1);
		}
		if ((finger->user = (char *) malloc(strlen(argv[1])+3))==NULL) {
			free_finger(finger);
			return 1;
		}
		strcpy(finger->user, argv[1]);
		strcat(finger->user, "\015\012");
	}

	fsocket.port = IPPORT_FINGER;           /* use finger wnp */

	/* Allocate a session descriptor */
	if ((s = newsession()) == NULL){
		printf("Too many sessions\n");
		free_finger(finger);
		return 1;
	}
	Current = s;
	s->cb.finger = finger;
	finger->session = s;

	if (!host)                              /* if no host specified */
		host = Hostname;                /* use local host name */
	if ((s->name = (char *) malloc(strlen(host)+1)) != NULL)
		strcpy(s->name, host);

	s->type = FINGER;
	s->parse = 0;

	tcb = open_tcp(&lsocket, &fsocket, TCP_ACTIVE, 0,
	 fingcli_rcv, 0, f_state, 0, (long) finger);

	finger->tcb = tcb;
	go(argc, argv, p);
	return 0;
}

/*
 *      Allocate a finger structure for the new session
 */
static struct finger *
alloc_finger(void)
{
	struct finger *tmp;

	if ((tmp = (struct finger *) malloc(sizeof(struct finger))) == NULL)
		return(NULL);
	tmp->session = NULL;
	tmp->user = NULL;
	return(tmp);
}

/*
 *      Free a finger structure
 */
static int
free_finger(
struct finger *finger)
{
	if (finger != NULL) {
		if (finger->session != NULL)
			freesession(finger->session);
		if (finger->user != NULL)
			free(finger->user);
		free(finger);
	}
	return 0;
}

/* Finger receiver upcall routine */
void
fingcli_rcv(
struct tcb *tcb,
int32 cnt)
{
	struct mbuf *bp;
	uint8 *buf;

	/* Make sure it's a valid finger session */
	if ((struct finger *) tcb->user == NULL) {
		return;
	}

	/* Hold output if we're not the current session */
	if (Mode != CONV_MODE || Current == NULL
		|| Current->type != FINGER)
		return;

	/*
	 *      We process the incoming data stream and make sure it
	 *      meets our requirments.  A check is made for control-Zs
	 *      since these characters lock up DoubleDos when sent to
	 *      the console (don't you just love it...).
	 */

	if (recv_tcp(tcb, &bp, cnt) > 0)
		while (bp != NULL) {
			buf = bp->data;
			while(bp->cnt--) {
				switch(*buf) {
					case '\012':    /* ignore LF */
					case '\032':    /* NO ^Z's! */
						break;
					case '\015':
						fputc('\015', stdout);
						fputc('\012', stdout);
						break;
					default:
						fputc(*buf, stdout);
						break;
				}
				buf++;
			}
			free_mbuf(&bp);
		}
}

/* State change upcall routine */
static void
f_state(
struct tcb *tcb,
enum tcp_state old,     /* old state */
enum tcp_state new)     /* new state */
{
	struct finger *finger;
	char notify = 0;
	struct mbuf *bp;

	finger = (struct finger *)tcb->user;

	if(Current != NULL && Current->type == FINGER)
		notify = 1;

	switch(new){

	case TCP_CLOSE_WAIT:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		close_tcp(tcb);
		break;

	case TCP_CLOSED:    /* finish up */
		if(notify) {
			printf("%s (%s", Tcpstates[new], Tcpreasons[tcb->reason & 0xff]);
			if (tcb->reason == NETWORK){
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
		if(finger != NULL)
			free_finger(finger);
		del_tcp(&tcb);
		break;
	case TCP_ESTABLISHED:
		if (notify) {
			printf("%s\n",Tcpstates[new]);
		}
		printf("[%s]\n", Current->name);
		bp = qdata(finger->user, (uint16) strlen(finger->user));
		send_tcp(tcb, &bp);
		break;

	default:
		if(notify)
			printf("%s\n",Tcpstates[new]);
		break;
	}
}

