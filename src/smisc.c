/* @(#) $Id: smisc.c,v 1.19 1999/02/01 22:24:25 deyke Exp $ */

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
#include "main.h"

char *Rempass;          /* Remote access password */

static struct tcb *disc_tcb,*echo_tcb;
static struct udp_cb *remote_up;

static void disc_server(struct tcb *tcb, int32 cnt);
static void echo_server(struct tcb *tcb, int32 cnt);
static void uremote(struct iface *iface, struct udp_cb *up, int cnt);
static int chkrpass(struct mbuf *bp);
static void misc_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new);

/* Start TCP discard server */
int
dis1(
int argc,
char *argv[],
void *p)
{
	struct socket lsocket;

	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_DISCARD;
	else
		lsocket.port = tcp_port_number(argv[1]);
	disc_tcb = open_tcp(&lsocket,NULL,TCP_SERVER,0,disc_server,NULL,misc_state,0,0);
	return 0;
}
/* TCP discard server */
static void
disc_server(
struct tcb *tcb,
int32 cnt)
{
	struct mbuf *bp;

	if (recv_tcp(tcb, &bp, 0) > 0)
		free_p(&bp);
}
/* Stop TCP discard server */
int
dis0(
int argc,
char *argv[],
void *p)
{
	if(disc_tcb != NULL)
		close_tcp(disc_tcb);
	return 0;
}
/* Start TCP echo server */
int
echo1(
int argc,
char *argv[],
void *p)
{
	struct socket lsocket;

	lsocket.address = INADDR_ANY;
	if(argc < 2)
		lsocket.port = IPPORT_ECHO;
	else
		lsocket.port = tcp_port_number(argv[1]);
	echo_tcb = open_tcp(&lsocket,NULL,TCP_SERVER,0,echo_server,echo_server,misc_state,0,0);
	return 0;
}
/* TCP echo server
 * Copies only as much will fit on the transmit queue
 */
static void
echo_server(
struct tcb *tcb,
int32 cnt)
{
	int32 acnt;
	struct mbuf *bp;

	acnt = tcb->window - tcb->sndcnt;
	if (acnt > tcb->rcvcnt)
		acnt = tcb->rcvcnt;
	if (acnt > 0) {
		if (recv_tcp(tcb, &bp, acnt) > 0)
			send_tcp(tcb, &bp);
	}
}
/* Stop TCP echo server */
int
echo0(
int argc,
char *argv[],
void *p)
{
	if(echo_tcb != NULL)
		close_tcp(echo_tcb);
	return 0;
}
/* Start UDP remote server */
int
rem1(
int argc,
char *argv[],
void *p)
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
uremote(
struct iface *iface,
struct udp_cb *up,
int cnt)
{

	struct mbuf *bp;
	struct socket fsock;
	char command;
	int32 addr;

	/* recv_udp() returns -1 without touching bp, which would leave it an
	 * uninitialized stack pointer for PULLCHAR() and free_p() below.
	 */
	if(recv_udp(up,&fsock,&bp) < 0)
		return;
	command = PULLCHAR(&bp);
	switch(command & 0xff){
	case SYS__EXIT:
		if(chkrpass(bp) == 0){
			logmsg(NULL,"%s - Remote exit PASSWORD FAIL",
			 pinet_udp(&fsock));
		} else {
			logmsg(NULL,"%s - Remote exit PASSWORD OK",
			 pinet_udp(&fsock));
			main_exit = 1;
		}
		break;
	case KICK__ME:
		/* This used to need no password at all, and the address to act
		 * on comes out of the datagram - so anyone who could reach the
		 * port could make this node retransmit on any connection it
		 * holds.  It is the same service and the same secret as the
		 * exit command; there is no reason for it to be the open one.
		 *
		 * Without an address it acts on the sender, which needs no
		 * secret: that is a peer asking for its own connection to be
		 * kicked, and it can only ever name itself.
		 */
		if(len_p(bp) >= sizeof(int32)){
			addr = pull32(&bp);
			if(chkrpass(bp) == 0){
				logmsg(NULL,"%s - Remote kick PASSWORD FAIL",
				 pinet_udp(&fsock));
				break;
			}
		} else
			addr = fsock.address;
		kick(addr);
		/*** smtptick((void *)addr); ***/
		break;
	}
	free_p(&bp);
}
/* Check remote password */
/* The password travels in clear over UDP, so anyone on the path has it and no
 * amount of care here changes that - "remote" is only reasonable on a network
 * that is already trusted.  What can be done is not to hand out anything
 * extra: the comparison now takes the same path whatever the input, instead
 * of returning early on a length that does not match and then running
 * strncmp(), which stops at the first differing byte.
 */

static int
chkrpass(
struct mbuf *bp)
{
	char *lbuf;
	uint16 len;
	uint16 plen;
	uint16 i;
	int diff;

	if(Rempass == 0 || *Rempass == 0)
		return 0;
	plen = (uint16) strlen(Rempass);
	len = len_p(bp);
	lbuf = (char *) mallocw(len ? len : 1);
	pullup(&bp,lbuf,len);
	diff = (int) (len ^ plen);
	for(i = 0; i < plen; i++)
		diff |= (unsigned char) Rempass[i] ^
			(i < len ? (unsigned char) lbuf[i] : 0);
	free(lbuf);
	return diff == 0;
}
/* Stop UDP remote exit/reboot server */
int
rem0(
int argc,
char *argv[],
void *p)
{
	if(remote_up){
		del_udp(&remote_up);
		remote_up = 0;
	}
	return 0;
}

/* Log connection state changes; also respond to remote closes */
static void
misc_state(
struct tcb *tcb,
enum tcp_state old,enum tcp_state new)
{
	switch(new){
	case TCP_ESTABLISHED:
		logmsg(tcb,"open %s",tcp_port_name(tcb->conn.local.port));
		break;
	case TCP_CLOSE_WAIT:
		close_tcp(tcb);
		break;
	case TCP_CLOSED:
		logmsg(tcb,"close %s",tcp_port_name(tcb->conn.local.port));
		del_tcp(&tcb);
		/* Clean up if server is being shut down */
		if(tcb == disc_tcb)
			disc_tcb = NULL;
		else if(tcb == echo_tcb)
			echo_tcb = NULL;
		break;
	default:
		break;
	}
}
