/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpcmd.c,v 1.2 1990/08/23 17:34:06 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "cmdparse.h"

static int dotcpreset __ARGS((int argc, char *argv [], void *p));
static int doirtt __ARGS((int argc, char *argv [], void *p));
static int dortt __ARGS((int argc, char *argv [], void *p));
static int dotcpkick __ARGS((int argc, char *argv [], void *p));
static int domss __ARGS((int argc, char *argv [], void *p));
static int dowindow __ARGS((int argc, char *argv [], void *p));
static int dotcpstat __ARGS((int argc, char *argv [], void *p));
static int tstat __ARGS((void));

/* TCP connection states */
char *tcpstates[] = {
	"Closed",
	"Listen",
	"SYN sent",
	"SYN received",
	"Established",
	"FIN wait 1",
	"FIN wait 2",
	"Close wait",
	"Closing",
	"Last ACK",
	"Time wait"
};

/* TCP closing reasons */
char *reasons[] = {
	"Normal",
	"Reset",
	"Timeout",
	"ICMP"
};
/* TCP subcommand table */
struct cmds tcpcmds[] = {
	"irtt",         doirtt,         0, 0,      NULLCHAR,
	"kick",         dotcpkick,      0, 2,      "tcp kick <tcb>",
	"mss",          domss,          0, 0,      NULLCHAR,
	"reset",        dotcpreset,     0, 2,      "tcp reset <tcb>",
	"rtt",          dortt,          0, 3,      "tcp rtt <tcb> <val>",
	"status",       dotcpstat,      0, 0,      NULLCHAR,
	"window",       dowindow,       0, 0,      NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0,
		"tcp subcommands: irtt kick mss reset rtt status window"
};
int
dotcp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(tcpcmds,argc,argv,p);
}

/* Eliminate a TCP connection */
static int
dotcpreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;
	extern char notval[];

	tcb = (struct tcb *)htol(argv[1]);
	if(!tcpval(tcb)){
		printf(notval);
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
	if(argc < 2)
		printf("%lu\n",tcp_irtt);
	else
		tcp_irtt = atol(argv[1]);
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
	extern char notval[];

	tcb = (struct tcb *)htol(argv[1]);
	if(!tcpval(tcb)){
		printf(notval);
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
	extern char notval[];

	tcb = (struct tcb *)htol(argv[1]);
	if(kick_tcp(tcb) == -1){
		printf(notval);
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
	if(argc < 2)
		printf("%u\n",tcp_mss);
	else
		tcp_mss = atoi(argv[1]);
	return 0;
}

/* Set default window size */
static int
dowindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2)
		printf("%u\n",tcp_window);
	else
		tcp_window = atoi(argv[1]);
	return 0;
}

/* Display status of TCBs */
static int
dotcpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct tcb *tcb;
	extern char notval[];

	if(argc < 2){
		tstat();
	} else {
		tcb = (struct tcb *)htol(argv[1]);
		if(tcpval(tcb))
			state_tcp(tcb);
		else
			printf(notval);
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
	char *psocket();

	printf("conout %u conin %u reset out %u runt %u chksum err %u bdcsts %u\n",
		tcp_stat.conout,tcp_stat.conin,tcp_stat.resets,tcp_stat.runt,
		tcp_stat.checksum,tcp_stat.bdcsts);
	printf("    &TCB Rcv-Q Snd-Q  Local socket           Remote socket          State\n");
	for(i=0;i<NTCB;i++){
		for(tcb=tcbs[i];tcb != NULLTCB;tcb = tcb->next){
			printf("%8lx%6u%6u  ",(long)tcb,tcb->rcvcnt,tcb->sndcnt);
			printf("%-23s",psocket(&tcb->conn.local));
			printf("%-23s",psocket(&tcb->conn.remote));
			printf("%-s",tcpstates[tcb->state]);
			if(tcb->state == LISTEN && (tcb->flags & CLONE))
				printf(" (S)");
			printf("\n");
		}
	}
	fflush(stdout);
	return 0;
}
/* Dump a TCP control block in detail */
void
state_tcp(tcb)
struct tcb *tcb;
{
	int32 sent,recvd;

	if(tcb == NULLTCB)
		return;
	/* Compute total data sent and received; take out SYN and FIN */
	sent = tcb->snd.una - tcb->iss; /* Acknowledged data only */
	recvd = tcb->rcv.nxt - tcb->irs;
	switch(tcb->state){
	case LISTEN:
	case SYN_SENT:          /* Nothing received or acked yet */
		sent = recvd = 0;
		break;
	case SYN_RECEIVED:
		recvd--;        /* Got SYN, no data acked yet */
		sent = 0;
		break;
	case ESTABLISHED:       /* Got and sent SYN */
	case FINWAIT1:          /* FIN not acked yet */
		sent--;
		recvd--;
		break;
	case FINWAIT2:          /* Our SYN and FIN both acked */
		sent -= 2;
		recvd--;
		break;
	case CLOSE_WAIT:        /* Got SYN and FIN, our FIN not yet acked */
	case CLOSING:
	case LAST_ACK:
		sent--;
		recvd -= 2;
		break;
	case TIME_WAIT:         /* Sent and received SYN/FIN, all acked */
		sent -= 2;
		recvd -= 2;
		break;
	}
	printf("Local: %s",psocket(&tcb->conn.local));
	printf(" Remote: %s",psocket(&tcb->conn.remote));
	printf(" State: %s\n",tcpstates[tcb->state]);
	printf("      Init seq    Unack     Next Resent CWind Thrsh  Wind  MSS Queue      Total\n");
	printf("Send:");
	printf("%9lx",tcb->iss);
	printf("%9lx",tcb->snd.una);
	printf("%9lx",tcb->snd.nxt);
	printf("%7lu",tcb->resent);
	printf("%6u",tcb->cwind);
	printf("%6u",tcb->ssthresh);
	printf("%6u",tcb->snd.wnd);
	printf("%5u",tcb->mss);
	printf("%6u",tcb->sndcnt);
	printf("%11lu\n",sent);

	printf("Recv:");
	printf("%9lx",tcb->irs);
	printf("         ");
	printf("%9lx",tcb->rcv.nxt);
	printf("%7lu",tcb->rerecv);
	printf("      ");
	printf("      ");
	printf("%6u",tcb->rcv.wnd);
	printf("     ");
	printf("%6u",tcb->rcvcnt);
	printf("%11lu\n",recvd);

	if(tcb->reseq != (struct reseq *)NULL){
		register struct reseq *rp;

		printf("Reassembly queue:\n");
		for(rp = tcb->reseq;rp != (struct reseq *)NULL; rp = rp->next){
			printf("  seq x%lx %u bytes\n",rp->seg.seq,rp->length);
		}
	}
	if(tcb->backoff > 0)
		printf("Backoff %u ",tcb->backoff);
	if(tcb->flags & RETRAN)
		printf("Retrying ");
	switch(tcb->timer.state){
	case TIMER_STOP:
		printf("Timer stopped ");
		break;
	case TIMER_RUN:
		printf("Timer running (%ld/%ld ms) ",
		 (long)MSPTICK * read_timer(&tcb->timer),
		 (long)MSPTICK * dur_timer(&tcb->timer));
		break;
	case TIMER_EXPIRE:
		printf("Timer expired ");
	}
	printf("SRTT %ld ms Mean dev %ld ms\n",tcb->srtt,tcb->mdev);
	fflush(stdout);
}

