/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/smisc.c,v 1.10 1994/09/11 18:34:46 deyke Exp $ */

/* Miscellaneous Internet servers: discard, echo and remote
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "remote.h"
#include "tcp.h"
#include "udp.h"
#include "commands.h"
#include "hardware.h"

char *Rempass;          /* Remote access password */

static struct tcb *disc_tcb,*echo_tcb;
static struct udp_cb *remote_up;

static void disc_server(struct tcb *tcb, int cnt);
static void echo_server(struct tcb *tcb, int cnt);
static void uremote(struct iface *iface, struct udp_cb *up, int cnt);
static int chkrpass(struct mbuf *bp);
static void misc_state(struct tcb *tcb, int old, int new);

/* Start TCP discard server */
int
dis1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket lsocket;

	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_DISCARD;
	else
		lsocket.port = tcp_port_number(argv[1]);
	disc_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,disc_server,NULLVFP,misc_state,0,0);
	return 0;
}
/* TCP discard server */
static void
disc_server(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	struct mbuf *bp;

	if (recv_tcp(tcb, &bp, 0) > 0)
		free_p(bp);
}
/* Stop TCP discard server */
int
dis0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(disc_tcb != NULLTCB)
		close_tcp(disc_tcb);
	return 0;
}
/* Start TCP echo server */
int
echo1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket lsocket;

	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_ECHO;
	else
		lsocket.port = tcp_port_number(argv[1]);
	echo_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,echo_server,echo_server,misc_state,0,0);
	return 0;
}
/* TCP echo server
 * Copies only as much will fit on the transmit queue
 */
static void
echo_server(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	struct mbuf *bp;
	int acnt;

	acnt = tcb->window - tcb->sndcnt;
	if (acnt > tcb->rcvcnt)
		acnt = tcb->rcvcnt;
	if (acnt > 0) {
		if (recv_tcp(tcb, &bp, acnt) > 0)
			send_tcp(tcb, bp);
	}
}
/* Stop TCP echo server */
int
echo0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(echo_tcb != NULLTCB)
		close_tcp(echo_tcb);
	return 0;
}
/* Start UDP remote server */
int
rem1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct socket sock;

	sock.address = INADDR_ANY;
	if(argc < 2)
		sock.port = IPPORT_REMOTE;
	else
		sock.port = atoi(argv[1]);
	remote_up = open_udp(&sock,uremote);
	return 0;
}
/* Process remote command */
static void
uremote(iface,up,cnt)
struct iface *iface;
struct udp_cb *up;
int cnt;
{

	struct mbuf *bp;
	struct socket fsock;
	int i;
	char command;
	int32 addr;

	recv_udp(up,&fsock,&bp);
	command = PULLCHAR(&bp);
	switch(uchar(command)){
#ifdef  MSDOS   /* Only present on PCs running MSDOS */
	case SYS_RESET:
		i = chkrpass(bp);
		log(Rem,"%s - Remote reset %s",
		 pinet_udp((struct sockaddr *)&fsock),
		 i == 0 ? "PASSWORD FAIL" : "" );
		if(i != 0){
			iostop();
			sysreset();     /* No return */
		}
		break;
#endif
	case SYS_EXIT:
		i = chkrpass(bp);
		log(NULLTCB,"%s - Remote exit %s",
		 pinet_udp(&fsock),
		 i == 0 ? "PASSWORD FAIL" : "" );
		if(i != 0){
			iostop();
			exit(0);
		}
		break;
	case KICK_ME:
		if(len_p(bp) >= sizeof(int32))
			addr = pull32(&bp);
		else
			addr = fsock.address;
		kick(addr);
		/*** smtptick((void *)addr); ***/
		break;
	}
	free_p(bp);
}
/* Check remote password */
static int
chkrpass(bp)
struct mbuf *bp;
{
	char *lbuf;
	uint16 len;
	int rval = 0;

	len = len_p(bp);
	if(Rempass == 0 || *Rempass == 0 || strlen(Rempass) != len)
		return rval;
	lbuf = mallocw(len);
	pullup(&bp,lbuf,len);
	if(strncmp(Rempass,lbuf,len) == 0)
		rval = 1;
	free(lbuf);
	return rval;
}
/* Stop UDP remote exit/reboot server */
int
rem0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(remote_up){
		del_udp(remote_up);
		remote_up = 0;
	}
	return 0;
}

/* Log connection state changes; also respond to remote closes */
static void
misc_state(tcb,old,new)
register struct tcb *tcb;
char old,new;
{
	switch(new){
	case TCP_ESTABLISHED:
		log(tcb,"open %s",tcp_port_name(tcb->conn.local.port));
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		break;
	case TCP_CLOSED:
		log(tcb,"close %s",tcp_port_name(tcb->conn.local.port));
		del_tcp(tcb);
		/* Clean up if server is being shut down */
		if(tcb == disc_tcb)
			disc_tcb = NULLTCB;
		else if(tcb == echo_tcb)
			echo_tcb = NULLTCB;
		break;
	}
}
