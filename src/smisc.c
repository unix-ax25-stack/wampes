/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/smisc.c,v 1.5 1990/10/12 19:26:37 deyke Exp $ */

/* Miscellaneous servers */
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

char *Rempass = " ";    /* Remote access password */

static struct tcb *disc_tcb,*echo_tcb;
static struct udp_cb *remote_up;

static void disc_recv __ARGS((struct tcb *tcb, int cnt));
static void echo_recv __ARGS((struct tcb *tcb, int cnt));
static void echo_trans __ARGS((struct tcb *tcb, int cnt));
static void misc_state __ARGS((struct tcb *tcb, int old, int new));
static void uremote __ARGS((struct iface *iface, struct udp_cb *up, int cnt));
static int chkrpass __ARGS((struct mbuf **bpp));

/* Start up TCP discard server */
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
	disc_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,disc_recv,NULLVFP,misc_state,0,0);
	return 0;
}
/* Start up TCP echo server */
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
	echo_tcb = open_tcp(&lsocket,NULLSOCK,TCP_SERVER,0,echo_recv,echo_trans,misc_state,0,0);
	return 0;
}

/* Start remote exit/reboot server */
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
	open_udp(&sock,uremote);
	return 0;
}

/* Shut down miscellaneous servers */
dis0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(disc_tcb != NULLTCB)
		close_tcp(disc_tcb);
	return 0;
}
echo0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(echo_tcb != NULLTCB)
		close_tcp(echo_tcb);
	return 0;
}
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
/* Discard server receiver upcall */
static
void
disc_recv(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;

	if(recv_tcp(tcb,&bp,cnt) > 0)
		free_p(bp);                     /* Discard */
}

/* Echo server receive
 * Copies only as much will fit on the transmit queue
 */
static
void
echo_recv(tcb,cnt)
struct tcb *tcb;
int cnt;
{
	struct mbuf *bp;
	int acnt;

	if(cnt == 0){
		close_tcp(tcb);
		return;
	}
	acnt = min(cnt,tcb->snd.wnd);
	if(acnt > 0){
		/* Get only as much will fit in the send window */
		recv_tcp(tcb,&bp,tcb->snd.wnd);
		send_tcp(tcb,bp);
	}
}
/* Echo server transmit
 * Copies anything that might have been left in the receiver queue
 */
static
void
echo_trans(tcb,cnt)
struct tcb *tcb;
int16 cnt;
{
	struct mbuf *bp;

	if(tcb->rcvcnt > 0){
		/* Get only as much will fit in the send window */
		recv_tcp(tcb,&bp,cnt);
		send_tcp(tcb,bp);
	}
}

/* Log connection state changes; also respond to remote closes */
static
void
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
/* Process remote exit/reset command */
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
		i = chkrpass(&bp);
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
		i = chkrpass(&bp);
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
chkrpass(bpp)
struct mbuf **bpp;
{
	char *lbuf;
	int16 len;
	int rval = 0;

	len = len_p(*bpp);
	if(strlen(Rempass) == len) {
		lbuf = malloc(len);
		pullup(bpp,lbuf,len);
		if(strncmp(Rempass,lbuf,len) == 0)
			rval = 1;
		free(lbuf);
	}
	return rval;
}
