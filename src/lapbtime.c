/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapbtime.c,v 1.1 1993/01/29 06:52:10 deyke Exp $ */

/* LAPB (AX25) timer recovery routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "timer.h"
#include "lapb.h"

/* Called whenever timer T1 expires */
void
recover(p)
void *p;
{
	register struct ax25_cb *axp = (struct ax25_cb *)p;

	axp->flags.retrans = 1;
	axp->retries++;
	{
		/* Back off retransmit timer */
		int32 tmp = (dur_timer(&axp->t1) * 5 + 2) / 4;
		if(tmp < 500) tmp = 500;
		set_timer(&axp->t1,tmp);
	}
	axp->cwind = 1;

	switch(axp->state){
	case LAPB_SETUP:
		if(axp->peer && axp->peer->state == LAPB_DISCONNECTED)
			if(axp->retries > 2){
				axp->reason = LB_TIMEOUT;
				lapbstate(axp,LAPB_DISCONNECTED);
			} else {
				start_timer(&axp->t1);
			}
		else
			if(axp->retries > N2){
				axp->reason = LB_TIMEOUT;
				lapbstate(axp,LAPB_DISCONNECTED);
			} else {
				send_packet(axp,SABM,POLL,NULLBUF);
			}
		break;
	case LAPB_DISCPENDING:
		if(axp->retries > N2){
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			send_packet(axp,DISC,POLL,NULLBUF);
		}
		break;
	case LAPB_CONNECTED:
		if(axp->retries > N2){
			/* Give up */
			lapbstate(axp,LAPB_DISCPENDING);
		} else if(!axp->flags.polling && !axp->flags.remotebusy &&
			   axp->unack && len_p(axp->txq) <= Pthresh){
			int old_vs;
			struct mbuf *bp;
			old_vs = axp->vs;
			axp->vs = (axp->vs - axp->unack) & 7;
			dup_p(&bp,axp->txq,0,MAXINT16);
			send_packet(axp,I,POLL,bp);
			axp->vs = old_vs;
		} else {
			/* Transmit poll */
			send_ack(axp,POLL);
		}
		break;
	}
}
