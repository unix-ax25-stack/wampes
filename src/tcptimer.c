/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcptimer.c,v 1.3 1990/10/12 19:26:49 deyke Exp $ */

/* TCP timeout routines */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "netuser.h"
#include "internet.h"
#include "tcp.h"

/* Timer timeout */
void
tcp_timeout(p)
void *p;
{
	register struct tcb *tcb;

	tcb = p;
	if(tcb == NULLTCB)
		return;

	/* Make sure the timer has stopped (we might have been kicked) */
	stop_timer(&tcb->timer);

	switch(tcb->state){
	case TCP_TIME_WAIT:     /* 2MSL timer has expired */
		close_self(tcb,NORMAL);
		break;
	default:                /* Retransmission timer has expired */
		tcb->flags.retran = 1;  /* Indicate > 1  transmission */
		tcb->backoff++;
		if (tcb->backoff > 10) {
			close_self(tcb,TIMEOUT);
			return;
		}
		tcb->snd.ptr = tcb->snd.una;
		/* Reduce slowstart threshold to half current window */
		tcb->ssthresh = tcb->cwind / 2;
		tcb->ssthresh = max(tcb->ssthresh,tcb->mss);
		/* Shrink congestion window to 1 packet */
		tcb->cwind = tcb->mss;
		tcp_output(tcb);
	}
}
/* Backoff function - the subject of much research */
int32
backoff(n)
int n;
{
	return 1L << n; /* Binary exponential back off */
}

