/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpcmd.c,v 1.14 1994/10/09 08:22:59 deyke Exp $ */

/* TCP control and status routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "cmdparse.h"
#include "commands.h"
#include "socket.h"
#include "session.h"

static int doirtt(int argc,char *argv[],void *p);
static int domss(int argc,char *argv[],void *p);
static int dortt(int argc,char *argv[],void *p);
static int dotcpkick(int argc,char *argv[],void *p);
static int dotcpreset(int argc,char *argv[],void *p);
static int dotcpstat(int argc,char *argv[],void *p);
static int dotcptr(int argc,char *argv[],void *p);
static int dowindow(int argc,char *argv[],void *p);
static int dosyndata(int argc,char *argv[],void *p);
static int tstat(void);

/* TCP subcommand table */
static struct cmds Tcpcmds[] = {
	"irtt",         doirtt,         0, 0,   NULLCHAR,
	"kick",         dotcpkick,      0, 2,   "tcp kick <tcb>",
	"mss",          domss,          0, 0,   NULLCHAR,
	"reset",        dotcpreset,     0, 2,   "tcp reset <tcb>",
	"rtt",          dortt,          0, 3,   "tcp rtt <tcb> <val>",
	"status",       dotcpstat,      0, 0,   NULLCHAR,
	"syndata",      dosyndata,      0, 0,   NULLCHAR,
	"trace",        dotcptr,        0, 0,   NULLCHAR,
	"window",       dowindow,       0, 0,   NULLCHAR,
	NULLCHAR,
};
int
dotcp(
int argc,
char *argv[],
void *p)
{
	return subcmd(Tcpcmds,argc,argv,p);
}
static int
dotcptr(
int argc,
char *argv[],
void *p)
{
	return setbool(&Tcp_trace,"TCP state tracing",argc,argv);
}

/* Eliminate a TCP connection */
static int
dotcpreset(
int argc,
char *argv[],
void *p)
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(!tcpval(tcb)){
		printf(Notval);
		return 1;
	}
	reset_tcp(tcb);
	return 0;
}

/* Set initial round trip time for new connections */
static int
doirtt(
int argc,
char *argv[],
void *p)
{
	struct tcp_rtt *tp;

	if(argc > 2){
		int32 addr;
		if((addr = resolve(argv[1])) == 0){
			printf(Badhost,argv[1]);
			return 1;
		}
		rtt_add(addr,atol(argv[2]));
		return 0;
	}
	setlong(&Tcp_irtt,"TCP default irtt",argc,argv);
	if(argc < 2){
		for(tp = Tcp_rtt;tp;tp=tp->next){
			if(tp->addr != 0){
				if(printf("%s: srtt %lu mdev %lu\n",
				 inet_ntoa(tp->addr),
				 tp->srtt,tp->mdev) == EOF)
					break;
			}
		}
	}
	return 0;
}

/* Set smoothed round trip time for specified TCB */
static int
dortt(
int argc,
char *argv[],
void *p)
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(!tcpval(tcb)){
		printf(Notval);
		return 1;
	}
	tcb->srtt = atol(argv[2]);
	return 0;
}

/* Force a retransmission */
static int
dotcpkick(
int argc,
char *argv[],
void *p)
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(kick_tcp(tcb) == -1){
		printf(Notval);
		return 1;
	}
	return 0;
}

/* Set default maximum segment size */
static int
domss(
int argc,
char *argv[],
void *p)
{
	return setshort(&Tcp_mss,"TCP MSS",argc,argv);
}

/* Set default window size */
static int
dowindow(
int argc,
char *argv[],
void *p)
{
	return setshort(&Tcp_window,"TCP window",argc,argv);
}

static int
dosyndata(
int argc,
char *argv[],
void *p)
{
	return setbool(&Tcp_syndata,"TCP syn+data piggybacking",argc,argv);
}

/* Display status of TCBs */
static int
dotcpstat(
int argc,
char *argv[],
void *p)
{
	register struct tcb *tcb;

	if(argc < 2){
		tstat();
		return 0;
	}

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(!tcpval(tcb)){
		printf(Notval);
		return 1;
	}
	st_tcp(tcb);
	return 0;
}

/* Dump TCP stats and summary of all TCBs
/*     &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State
 *     1234     0     0  xxx.xxx.xxx.xxx:xxxxx  xxx.xxx.xxx.xxx:xxxxx  Established
 */
static int
tstat(void)
{
	register int i;
	register struct tcb *tcb;
	int j;

    if(!Shortstatus){
	for(j=i=1;i<=NUMTCPMIB;i++){
		if(Tcp_mib[i].name == NULLCHAR)
			continue;
		printf("(%2u)%-20s%10lu",i,Tcp_mib[i].name,
		 Tcp_mib[i].value.integer);
		if(j++ % 2)
			printf("     ");
		else
			printf("\n");
	}
	if((j % 2) == 0)
		printf("\n");
    }

	printf("    &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State\n");
	for(tcb=Tcbs;tcb != NULLTCB;tcb = tcb->next){
		printf("%8lx%6lu%6lu  ",ptol(tcb),tcb->rcvcnt,tcb->sndcnt);
		printf("%-22.22s ",pinet_tcp(&tcb->conn.local));
		printf("%-22.22s ",pinet_tcp(&tcb->conn.remote));
		printf("%-s",Tcpstates[tcb->state]);
		if(tcb->state == TCP_LISTEN && tcb->flags.clone)
			printf(" (S)");
		if(printf("\n") == EOF)
			return 0;
	}
	return 0;
}
/* Dump a TCP control block in detail */
void
st_tcp(
struct tcb *tcb)
{
	int32 sent,recvd;

	if(tcb == NULLTCB)
		return;
	/* Compute total data sent and received; take out SYN and FIN */
	sent = tcb->snd.una - tcb->iss; /* Acknowledged data only */
	recvd = tcb->rcv.nxt - tcb->irs;
	switch(tcb->state){
	case TCP_LISTEN:
	case TCP_SYN_SENT:      /* Nothing received or acked yet */
		sent = recvd = 0;
		break;
	case TCP_SYN_RECEIVED:
		recvd--;        /* Got SYN, no data acked yet */
		sent = 0;
		break;
	case TCP_ESTABLISHED:   /* Got and sent SYN */
	case TCP_FINWAIT1:      /* FIN not acked yet */
		sent--;
		recvd--;
		break;
	case TCP_FINWAIT2:      /* Our SYN and FIN both acked */
		sent -= 2;
		recvd--;
		break;
	case TCP_CLOSE_WAIT:    /* Got SYN and FIN, our FIN not yet acked */
	case TCP_CLOSING:
	case TCP_LAST_ACK:
		sent--;
		recvd -= 2;
		break;
	case TCP_TIME_WAIT:     /* Sent and received SYN/FIN, all acked */
		sent -= 2;
		recvd -= 2;
		break;
	}
	printf("Local: %s",pinet(&tcb->conn.local));
	printf(" Remote: %s",pinet(&tcb->conn.remote));
	printf(" State: %s\n",Tcpstates[tcb->state]);
	printf("         Unack     Next Resent CWind Thrsh  Wind  MSS Queue  Thruput      Total\n");
	printf("Send: %08lx %08lx%7lu%6lu%6lu%6lu%5lu%6lu%9lu%11lu\n",
	 tcb->snd.una,tcb->snd.nxt,tcb->resent,tcb->cwind,tcb->ssthresh,
	 tcb->snd.wnd,tcb->mss,tcb->sndcnt,tcb->txbw,sent);

	printf("Recv:          %08lx%7lu            %6lu     %6lu%9lu%11lu\n",
	 tcb->rcv.nxt,tcb->rerecv,tcb->rcv.wnd,tcb->rcvcnt,tcb->rxbw,recvd);

	printf("Dup acks   Backoff   Timeouts   Source Quench   Unreachables   Power\n");
	printf("%8u%10u%11lu%16lu%15lu",tcb->dupacks,tcb->backoff,tcb->timeouts,
	 tcb->quench,tcb->unreach);
	if(tcb->srtt != 0)
		printf("%8lu",1000*tcb->txbw/tcb->srtt);
	else
		printf("     INF");
	if(tcb->flags.retran)
		printf(" Retry");
	printf("\n");

	printf("Timer        Count  Duration  Last RTT      SRTT      mdev\n");
	switch(tcb->timer.state){
	case TIMER_STOP:
		printf("stopped");
		break;
	case TIMER_RUN:
		printf("running");
		break;
	case TIMER_EXPIRE:
		printf("expired");
		break;
	}
	printf(" %10lu%10lu%10lu%10lu%10lu\n",(long)read_timer(&tcb->timer),
	 (long)dur_timer(&tcb->timer),tcb->rtt,tcb->srtt,tcb->mdev);

	if(tcb->reseq != (struct reseq *)NULL){
		register struct reseq *rp;

		printf("Reassembly queue:\n");
		for(rp = tcb->reseq;rp != (struct reseq *)NULL; rp = rp->next){
			if(printf("  seq x%lx %u bytes\n",
			 rp->seg.seq,rp->length) == EOF)
				return;
		}
	}
}

