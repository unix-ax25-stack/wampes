/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpsubr.c,v 1.6 1990/10/12 19:26:48 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "timer.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"
#include "ip.h"

static int16 hash_tcb __ARGS((struct connection *conn));

/* TCP connection states */
char *Tcpstates[] = {
	"",
	"Closed",
	"Listen",
	"SYN sent",
	"SYN received",
	"Established",
	"FIN wait 1",
	"FIN wait 2",
	"Close wait",
	"Last ACK",
	"Closing",
	"Time wait"
};

/* TCP closing reasons */
char *Tcpreasons[] = {
	"Normal",
	"Reset/Refused",
	"Timeout",
	"ICMP"
};
struct tcb *Tcbs[NTCB];
int16 Tcp_mss = DEF_MSS;        /* Maximum segment size to be sent with SYN */
int32 Tcp_irtt = DEF_RTT;       /* Initial guess at round trip time */
int Tcp_trace;                  /* State change tracing flag */
struct tcp_rtt Tcp_rtt[RTTCACHE];
struct mib_entry Tcp_mib[] = {
	NULLCHAR,               0,
	"tcpRtoAlgorithm",      4,      /* Van Jacobsen's algorithm */
	"tcpRtoMin",            MSPTICK,        /* No lower bound */
	"tcpRtoMax",            MAXINT32,       /* No upper bound */
	"tcpMaxConn",           -1L,    /* No limit */
	"tcpActiveOpens",       0,
	"tcpPassiveOpens",      0,
	"tcpAttemptFails",      0,
	"tcpEstabResets",       0,
	"tcpCurrEstab",         0,
	"tcpInSegs",            0,
	"tcpOutSegs",           0,
	"tcpRetransSegs",       0,
	NULLCHAR,               0,      /* Connection state goes here */
	"tcpInErrs",            0,
	"tcpOutRsts",           0,
};

/* Lookup connection, return TCB pointer or NULLTCB if nonexistant */
struct tcb *
lookup_tcb(conn)
struct connection *conn;
{
	register struct tcb *tcb;

	tcb = Tcbs[hash_tcb(conn)];
	while(tcb != NULLTCB){
		/* Yet another structure compatibility hack */
		if(conn->local.address == tcb->conn.local.address
		 && conn->remote.address == tcb->conn.remote.address
		 && conn->local.port == tcb->conn.local.port
		 && conn->remote.port == tcb->conn.remote.port)
			break;
		tcb = tcb->next;
	}
	return tcb;
}

/* Create a TCB, return pointer. Return pointer if TCB already exists. */
struct tcb *
create_tcb(conn)
struct connection *conn;
{
	register struct tcb *tcb;
	struct tcp_rtt *tp;

	if((tcb = lookup_tcb(conn)) != NULLTCB)
		return tcb;
	tcb = (struct tcb *)callocw(1,sizeof (struct tcb));
	ASSIGN(tcb->conn,*conn);

	tcb->state = TCP_CLOSED;
	tcb->cwind = tcb->mss = Tcp_mss;
	tcb->ssthresh = 65535;
	if((tp = rtt_get(tcb->conn.remote.address)) != NULLRTT){
		tcb->srtt = tp->srtt;
		tcb->mdev = tp->mdev;
	} else {
		tcb->srtt = Tcp_irtt;   /* mdev = 0 */
	}
	/* Initialize timer intervals */
	tcb->timer.start = tcb->srtt / MSPTICK;
	tcb->timer.func = tcp_timeout;
	tcb->timer.arg = tcb;

	link_tcb(tcb);
	return tcb;
}

/* Close our TCB */
void
close_self(tcb,reason)
register struct tcb *tcb;
int reason;
{
	struct reseq *rp1;
	register struct reseq *rp;

	if(tcb == NULLTCB)
		return;

	stop_timer(&tcb->timer);
	tcb->reason = reason;

	/* Flush reassembly queue; nothing more can arrive */
	for(rp = tcb->reseq;rp != NULLRESEQ;rp = rp1){
		rp1 = rp->next;
		free_p(rp->bp);
		free((char *)rp);
	}
	tcb->reseq = NULLRESEQ;
	setstate(tcb,TCP_CLOSED);
}

/* Sequence number comparisons
 * Return true if x is between low and high inclusive,
 * false otherwise
 */
int
seq_within(x,low,high)
register int32 x,low,high;
{
	if(low <= high){
		if(low <= x && x <= high)
			return 1;
	} else {
		if(low >= x && x >= high)
			return 1;
	}
	return 0;
}
int
seq_lt(x,y)
register int32 x,y;
{
	return (long)(x-y) < 0;
}
#ifdef  notdef
int
seq_le(x,y)
register int32 x,y;
{
	return (long)(x-y) <= 0;
}
#endif  /* notdef */
int
seq_gt(x,y)
register int32 x,y;
{
	return (long)(x-y) > 0;
}
int
seq_ge(x,y)
register int32 x,y;
{
	return (long)(x-y) >= 0;
}

/* Hash a connect structure into the hash chain header array */
static int16
hash_tcb(conn)
struct connection *conn;
{
	register int16 hval;

	/* Compute hash function on connection structure */
	hval = hiword(conn->remote.address);
	hval ^= loword(conn->remote.address);
#ifdef  notdef  /* Never changes, so not really needed */
	hval ^= hiword(conn->local.address);
	hval ^= loword(conn->local.address);
#endif
	hval ^= conn->remote.port;
	hval ^= conn->local.port;
	return (int16)(hval % NTCB);
}
/* Insert TCB at head of proper hash chain */
void
link_tcb(tcb)
register struct tcb *tcb;
{
	register struct tcb **tcbhead;

	tcb->prev = NULLTCB;
	tcbhead = &Tcbs[hash_tcb(&tcb->conn)];
	tcb->next = *tcbhead;
	if(tcb->next != NULLTCB)
		tcb->next->prev = tcb;

	*tcbhead = tcb;
}
/* Remove TCB from whatever hash chain it may be on */
void
unlink_tcb(tcb)
register struct tcb *tcb;
{
	register struct tcb **tcbhead;

	tcbhead = &Tcbs[hash_tcb(&tcb->conn)];
	if(tcb->prev == NULLTCB)
		*tcbhead = tcb->next;   /* We're the first one on the chain */
	else
		tcb->prev->next = tcb->next;
	if(tcb->next != NULLTCB)
		tcb->next->prev = tcb->prev;
}
void
setstate(tcb,newstate)
register struct tcb *tcb;
register int newstate;
{
	register char oldstate;

	oldstate = tcb->state;
	tcb->state = newstate;
	if(Tcp_trace)
		printf("TCB %lx %s -> %s\n",ptol(tcb),
		 Tcpstates[oldstate],Tcpstates[newstate]);

	/* Update MIB variables */
	if(oldstate == TCP_CLOSED && newstate == TCP_SYN_SENT)
		tcpActiveOpens++;
	else if(oldstate == TCP_LISTEN && newstate == TCP_SYN_RECEIVED)
		tcpPassiveOpens++;
	else if((oldstate == TCP_SYN_SENT || oldstate == TCP_SYN_RECEIVED)
	 && newstate == TCP_CLOSED)
		tcpAttemptFails++;
	else if(oldstate == TCP_SYN_RECEIVED && newstate == TCP_LISTEN)
		tcpAttemptFails++;
	else if((oldstate == TCP_ESTABLISHED || oldstate == TCP_CLOSE_WAIT)
	 && newstate == TCP_CLOSED)
		tcpEstabResets++;

	if(oldstate == TCP_ESTABLISHED || oldstate == TCP_CLOSE_WAIT)
		tcpCurrEstab--;

	if(newstate == TCP_ESTABLISHED || newstate == TCP_CLOSE_WAIT)
		tcpCurrEstab++;

	if(tcb->s_upcall)
		(*tcb->s_upcall)(tcb,oldstate,newstate);

	switch(newstate){
	case TCP_ESTABLISHED:
		/* Notify the user that he can begin sending data */
		if(tcb->t_upcall && tcb->window > tcb->sndcnt)
			(*tcb->t_upcall)(tcb,tcb->window - tcb->sndcnt);
		break;
	}
}
/* Round trip timing cache routines.
 * These functions implement a very simple system for keeping track of
 * network performance for future use in new connections.
 * The emphasis here is on speed of update (rather than optimum cache hit
 * ratio) since rtt_add is called every time a TCP connection updates
 * its round trip estimate.
 */
void
rtt_add(addr,rtt)
int32 addr;             /* Destination IP address */
int32 rtt;
{
	register struct tcp_rtt *tp;
	int32 abserr;

	if(addr == 0)
		return;
	tp = &Tcp_rtt[(unsigned short)addr % RTTCACHE];
	if(tp->addr != addr){
		/* New entry */
		tp->addr = addr;
		tp->srtt = rtt;
		tp->mdev = 0;
	} else {
		/* Run our own SRTT and MDEV integrators, with rounding */
		abserr = (rtt > tp->srtt) ? rtt - tp->srtt : tp->srtt - rtt;
		tp->srtt = ((AGAIN-1)*tp->srtt + rtt + (AGAIN/2)) >> LAGAIN;
		tp->mdev = ((DGAIN-1)*tp->mdev + abserr + (DGAIN/2)) >> LDGAIN;
	}
}
struct tcp_rtt *
rtt_get(addr)
int32 addr;
{
	register struct tcp_rtt *tp;

	if(addr == 0)
		return NULLRTT;
	tp = &Tcp_rtt[(unsigned short)addr % RTTCACHE];
	if(tp->addr != addr)
		return NULLRTT;
	return tp;
}

/* TCP garbage collection - called by storage allocator when free space
 * runs low. The send and receive queues are crunched. If the situation
 * is red, the resequencing queue is discarded; otherwise it is
 * also crunched.
 */
void
tcp_garbage(red)
int red;
{
	register struct tcb *tcb;
	int i;
	struct reseq *rp,*rp1;

	for(i=0;i<NTCB;i++){
		for(tcb = Tcbs[i];tcb != NULLTCB;tcb = tcb->next){
			mbuf_crunch(&tcb->rcvq);
			mbuf_crunch(&tcb->sndq);
			for(rp = tcb->reseq;rp != NULLRESEQ;rp = rp1){
				rp1 = rp->next;
				if(red){
					free_p(rp->bp);
					free((char *)rp);
				} else {
					mbuf_crunch(&rp->bp);
				}
			}
			if(red)
				tcb->reseq = NULLRESEQ;
		}
	}
}
