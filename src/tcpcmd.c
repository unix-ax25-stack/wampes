/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpcmd.c,v 1.3 1990/10/12 19:26:41 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "cmdparse.h"
#include "commands.h"

static int doirtt __ARGS((int argc,char *argv[],void *p));
static int domss __ARGS((int argc,char *argv[],void *p));
static int dortt __ARGS((int argc,char *argv[],void *p));
static int dotcpkick __ARGS((int argc,char *argv[],void *p));
static int dotcpreset __ARGS((int argc,char *argv[],void *p));
static int dotcpstat __ARGS((int argc,char *argv[],void *p));
static int dotcptr __ARGS((int argc,char *argv[],void *p));
static int dowindow __ARGS((int argc,char *argv[],void *p));
static int tstat __ARGS((void));

/* TCP subcommand table */
static struct cmds Tcpcmds[] = {
	"irtt",         doirtt,         0, 0,   NULLCHAR,
	"kick",         dotcpkick,      0, 2,   "tcp kick <tcb>",
	"mss",          domss,          0, 0,   NULLCHAR,
	"reset",        dotcpreset,     0, 2,   "tcp reset <tcb>",
	"rtt",          dortt,          0, 3,   "tcp rtt <tcb> <val>",
	"status",       dotcpstat,      0, 0,   NULLCHAR,
	"trace",        dotcptr,        0, 0,   NULLCHAR,
	"window",       dowindow,       0, 0,   NULLCHAR,
	NULLCHAR,
};
int
dotcp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Tcpcmds,argc,argv,p);
}
static int
dotcptr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Tcp_trace,"TCP state tracing",argc,argv);
}

/* Eliminate a TCP connection */
static int
dotcpreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(!tcpval(tcb)){
		tprintf(Notval);
		return 1;
	}
	close_self(tcb,RESET);
	return 0;
}

/* Set initial round trip time for new connections */
static int
doirtt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct tcp_rtt *tp;

	setlong(&Tcp_irtt,"TCP default irtt",argc,argv);
	if(argc < 2){
		for(tp = &Tcp_rtt[0];tp < &Tcp_rtt[RTTCACHE];tp++){
			if(tp->addr != 0){
				if(tprintf("%s: srtt %lu mdev %lu\n",
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
dortt(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(!tcpval(tcb)){
		tprintf(Notval);
		return 1;
	}
	tcb->srtt = atol(argv[2]);
	return 0;
}

/* Force a retransmission */
static int
dotcpkick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;

	tcb = (struct tcb *)ltop(htol(argv[1]));
	if(kick_tcp(tcb) == -1){
		tprintf(Notval);
		return 1;
	}
	return 0;
}

/* Set default maximum segment size */
static int
domss(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Tcp_mss,"TCP MSS",argc,argv);
}

/* Set default window size */
static int
dowindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setshort(&Tcp_window,"TCP window",argc,argv);
}

/* Display status of TCBs */
static int
dotcpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;

	if(argc < 2){
		tstat();
	} else {
		tcb = (struct tcb *)ltop(htol(argv[1]));
		if(tcpval(tcb))
			st_tcp(tcb);
		else
			tprintf(Notval);
	}
	return 0;
}

/* Dump TCP stats and summary of all TCBs
/*     &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State
 *     1234     0     0  xxx.xxx.xxx.xxx:xxxxx  xxx.xxx.xxx.xxx:xxxxx  Established
 */
static int
tstat()
{
	register int i;
	register struct tcb *tcb;
	int j;

    if(!Shortstatus){
	for(j=i=1;i<=NUMTCPMIB;i++){
		if(Tcp_mib[i].name == NULLCHAR)
			continue;
		tprintf("(%2u)%-20s%10lu",i,Tcp_mib[i].name,
		 Tcp_mib[i].value.integer);
		if(j++ % 2)
			tprintf("     ");
		else
			tprintf("\n");
	}
	if((j % 2) == 0)
		tprintf("\n");
    }

	tprintf("    &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State\n");
	for(i=0;i<NTCB;i++){
		for(tcb=Tcbs[i];tcb != NULLTCB;tcb = tcb->next){
			tprintf("%8lx%6u%6u  ",ptol(tcb),tcb->rcvcnt,tcb->sndcnt);
			tprintf("%-23s",pinet_tcp(&tcb->conn.local));
			tprintf("%-23s",pinet_tcp(&tcb->conn.remote));
			tprintf("%-s",Tcpstates[tcb->state]);
			if(tcb->state == TCP_LISTEN && tcb->flags.clone)
				tprintf(" (S)");
			if(tprintf("\n") == EOF)
				return 0;
		}
	}
	return 0;
}
/* Dump a TCP control block in detail */
void
st_tcp(tcb)
struct tcb *tcb;
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
	tprintf("Local: %s",pinet_tcp(&tcb->conn.local));
	tprintf(" Remote: %s",pinet_tcp(&tcb->conn.remote));
	tprintf(" State: %s\n",Tcpstates[tcb->state]);
	tprintf("      Init seq    Unack     Next Resent CWind Thrsh  Wind  MSS Queue      Total\n");
	tprintf("Send:");
	tprintf("%9lx",tcb->iss);
	tprintf("%9lx",tcb->snd.una);
	tprintf("%9lx",tcb->snd.nxt);
	tprintf("%7lu",tcb->resent);
	tprintf("%6u",tcb->cwind);
	tprintf("%6u",tcb->ssthresh);
	tprintf("%6u",tcb->snd.wnd);
	tprintf("%5u",tcb->mss);
	tprintf("%6u",tcb->sndcnt);
	tprintf("%11lu\n",sent);

	tprintf("Recv:");
	tprintf("%9lx",tcb->irs);
	tprintf("         ");
	tprintf("%9lx",tcb->rcv.nxt);
	tprintf("%7lu",tcb->rerecv);
	tprintf("      ");
	tprintf("      ");
	tprintf("%6u",tcb->rcv.wnd);
	tprintf("     ");
	tprintf("%6u",tcb->rcvcnt);
	tprintf("%11lu\n",recvd);

	if(tcb->reseq != (struct reseq *)NULL){
		register struct reseq *rp;

		tprintf("Reassembly queue:\n");
		for(rp = tcb->reseq;rp != (struct reseq *)NULL; rp = rp->next){
			if(tprintf("  seq x%lx %u bytes\n",
			 rp->seg.seq,rp->length) == EOF)
				return;
		}
	}
	if(tcb->backoff > 0)
		tprintf("Backoff %u ",tcb->backoff);
	if(tcb->flags.retran)
		tprintf("Retrying ");
	switch(tcb->timer.state){
	case TIMER_STOP:
		tprintf("Timer stopped ");
		break;
	case TIMER_RUN:
		tprintf("Timer running (%ld/%ld ms) ",
		 (long)MSPTICK * read_timer(&tcb->timer),
		 (long)MSPTICK * dur_timer(&tcb->timer));
		break;
	case TIMER_EXPIRE:
		tprintf("Timer expired ");
	}
	tprintf("SRTT %ld ms Mean dev %ld ms\n",tcb->srtt,tcb->mdev);
}
