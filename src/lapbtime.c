/* @(#) $Id: lapbtime.c,v 1.10 1996/08/19 16:30:14 deyke Exp $ */

/* LAPB (AX25) timer recovery routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "timer.h"
#include "lapb.h"
#include "dama.h"

static void tx_enq(struct ax25_cb *axp);

/* Called whenever timer T1 expires */
void
recover(void *p)
{
	struct ax25_cb *axp = (struct ax25_cb *)p;

	/* A DAMA slave does not retransmit of its own accord - it may not
	 * transmit at all until it is polled.  T1 keeps running, because its
	 * expiry is still the measure of how long the master has been silent,
	 * but the expiry itself does nothing: no retransmission, no enquiry,
	 * and NO retry count.  The connection is not failing, it is waiting,
	 * and counting the wait as failure is what would eventually kill it.
	 *
	 * That last part is where we part company with the Linux kernel.
	 * ax25_ds_t1_timeout() also stays silent, but it does count, and at
	 * N2 it sends DM and disconnects - taking the mailbox login and
	 * everything else above AX.25 with it.  Here, when the master has
	 * been away long enough, dama_holds() lets go by itself and this
	 * becomes an ordinary T1 again, which is precisely what repairs the
	 * link.  TNN does the same ("sendok = 1" when the slave timeout
	 * expires).  See doc/DAMA-SLAVE.md.
	 */
	if(dama_holds(axp)){
		/* With frames outstanding this is not merely waiting: the
		 * master has had a whole T1 to acknowledge them and has not,
		 * so they were lost.  Note that they must go again - the
		 * retransmission itself belongs in the window the next poll
		 * opens, not here - and count it, because a retransmission is
		 * a retry whenever it happens and something has to bound them.
		 *
		 * With nothing outstanding, nothing is counted.  That is the
		 * difference between a link that is failing and one that is
		 * waiting to be asked, and it is the whole reason we do not
		 * die where Linux does.
		 *
		 * The paper wants T1 above the poll interval for exactly this
		 * reason, and WAMPES gets there by itself: srt is measured
		 * over send-to-acknowledge, which under DAMA spans a full
		 * cycle, so T1 grows to fit the channel.
		 */
		if(axp->unack){
			axp->retries++;
			if(axp->n2 != 0 && axp->retries > axp->n2){
				sendctl(axp,LAPB_RESPONSE,DM|PF);
				free_q(&axp->txq);
				axp->reason = LB_TIMEOUT;
				lapbstate(axp,LAPB_DISCONNECTED);
				return;
			}
			axp->dama_rex = 1;
		}
		start_timer(&axp->t1);
		return;
	}

	axp->flags.retrans = 1;
	axp->retries++;
	if(axp->retries < Blimit)
		/* Back off retransmit timer */
		set_timer(&axp->t1,(dur_timer(&axp->t1)*5+2)/4);
	if(axp->maxframe > 1)
		axp->maxframe--;

	switch(axp->state){
	case LAPB_SETUP:
		if(axp->peer && axp->peer->state == LAPB_DISCONNECTED){
			if(axp->retries > 2){
				free_q(&axp->txq);
				axp->reason = LB_TIMEOUT;
				lapbstate(axp,LAPB_DISCONNECTED);
			} else {
				start_timer(&axp->t1);
			}
		} else {
		if(axp->n2 != 0 && axp->retries > axp->n2){
			free_q(&axp->txq);
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			sendctl(axp,LAPB_COMMAND,SABM|PF);
			start_timer(&axp->t1);
		}
		}
		break;
	case LAPB_DISCPENDING:
		if(axp->n2 != 0 && axp->retries > axp->n2){
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
		}
		break;
	case LAPB_CONNECTED:
	case LAPB_RECOVERY:
		if(axp->n2 != 0 && axp->retries > axp->n2){
			/* Give up */
			sendctl(axp,LAPB_RESPONSE,DM|PF);
			free_q(&axp->txq);
			axp->reason = LB_TIMEOUT;
			lapbstate(axp,LAPB_DISCONNECTED);
		} else {
			/* Transmit poll */
			tx_enq(axp);
			lapbstate(axp,LAPB_RECOVERY);
		}
		break;
	default:
		break;
	}
}

/* Send a poll (S-frame command with the poll bit set) */
void
pollthem(void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;
	if(axp->proto == V1)
		return; /* Not supported in the old protocol */

	/* T3 and T4 both land here, and both would put a command on the air
	 * to ask the other end whether it is still there.  On a DAMA channel
	 * that is the master's job, not ours - and asking anyway is exactly
	 * the "transmission not requested by the master" that gets a station
	 * warned and then disconnected on a node that enforces DAMA.
	 */
	if(dama_holds(axp)){
		/* Rearmed, and that is not a detail: this timer is one of the
		 * two that keep asking whether the master is still there.  A
		 * gate that returns without restarting its timer switches off
		 * the very thing that would have noticed the watchdog - found
		 * by watching a slave stay silent long past its timeout.
		 */
		start_timer(&axp->t3);
		return;
	}

	switch(axp->state){
	case LAPB_CONNECTED:
		axp->retries = 0;
		tx_enq(axp);
		lapbstate(axp,LAPB_RECOVERY);
		break;
	default:
		break;
	}
}
/* Transmit query */
static void
tx_enq(struct ax25_cb *axp)
{
	char ctl;
	struct mbuf *bp;

	/* I believe that retransmitting the oldest unacked
	 * I-frame tends to give better performance than polling,
	 * as long as the frame isn't too "large", because
	 * chances are that the I frame got lost anyway.
	 * This is an option in LAPB, but not in the official AX.25.
	 */
	if(axp->txq != NULL
	 && (len_p(axp->txq) < axp->pthresh || axp->proto == V1)){
		/* Retransmit oldest unacked I-frame */
		dup_p(&bp,axp->txq,0,len_p(axp->txq));
		ctl = PF | I | (((axp->vs - axp->unack) & MMASK) << 1)
		 | (axp->vr << 5);
		sendframe(axp,LAPB_COMMAND,ctl,&bp);
	} else {
		ctl = busy(axp)                      ? RNR|PF : RR|PF;
		sendctl(axp,LAPB_COMMAND,ctl);
	}
	/* axp->response = 0; */
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}
