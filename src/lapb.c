/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapb.c,v 1.26 1993/02/23 21:34:10 deyke Exp $ */

/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "ip.h"

static void handleit __ARGS((struct ax25_cb *axp,int pid,struct mbuf *bp));
static void procdata __ARGS((struct ax25_cb *axp,struct mbuf *bp));
static int ackours __ARGS((struct ax25_cb *axp,int   n,int rex_all));
static void clr_ex __ARGS((struct ax25_cb *axp));
static void enq_resp __ARGS((struct ax25_cb *axp));
static void inv_rex __ARGS((struct ax25_cb *axp));
static void resequence __ARGS((struct ax25_cb *axp,struct mbuf *bp,int ns,int pf,int poll));

/* Process incoming frames */
int
lapb_input(iface,hdr,bp)
struct iface *iface;
struct ax25 *hdr;
struct mbuf *bp;                /* Rest of frame, starting with ctl */
{

	struct ax25_cb *axp;    /* Link control structure */
	int cmdrsp = hdr->cmdrsp;       /* Command/response flag */
	int control;
	int class;              /* General class (I/S/U) of frame */
	int16 type;             /* Specific type (I/RR/RNR/etc) of frame */
	char pf;                /* extracted poll/final bit */
	char poll = 0;
	char final = 0;
	int16 nr;               /* ACK number of incoming frame */
	int16 ns;               /* Seq number of incoming frame */
	int16 tmp;
	int digipeat;

	if(bp == NULLBUF                   ){
		free_p(bp);
		return -1;
	}

	/* Extract the various parts of the control field for easy use */
	if((control = PULLCHAR(&bp)) == -1){
		free_p(bp);     /* Probably not necessary */
		return -1;
	}
	type = ftype(control);
	class = type & 0x3;
	pf = control & PF;
	/* Check for polls and finals */
	if(pf){
		switch(cmdrsp){
		case LAPB_COMMAND:
			poll = YES;
			break;
		case LAPB_RESPONSE:
			final = YES;
			break;
		}
	}
	digipeat = (ismyax25addr(hdr->dest) == NULLIF);
	/* Extract sequence numbers, if present */
	switch(class){
	case I:
	case I+2:
		ns = (control >> 1) & MMASK;
	case S: /* Note fall-thru */
		nr = (control >> 5) & MMASK;
		break;
	}

	if(digipeat){
		struct ax25_cb *axlast = NULLAX25;
		for(axp = Ax25_cb; axp; axlast = axp,axp = axp->next)
			if(addreq(hdr->source,axp->hdr.dest) &&
			    addreq(hdr->dest,axp->hdr.source)){
				if(axlast != NULLAX25){
					axlast->next = axp->next;
					axp->next = Ax25_cb;
					Ax25_cb = axp;
				}
				break;
			}
	} else
		axp = find_ax25(hdr->source);
	if(axp == NULLAX25){
		axp = cr_ax25(" ");
		build_path(axp,iface,hdr,1);
		if(digipeat){
			axp->peer = cr_ax25(" ");
			axp->peer->peer = axp;
			build_path(axp->peer,NULLIF,hdr,0);
		}
	}

	if(cmdrsp == LAPB_UNKNOWN)
		axp->proto = V1;        /* Old protocol in use */

	/* This section follows the SDL diagrams by K3NA fairly closely */
	switch(axp->state){
	case LAPB_DISCONNECTED:
		switch(type){
		case SABM:      /* Initialize or reset link */
			if(!digipeat){
			sendctl(axp,LAPB_RESPONSE,UA|pf);       /* Always accept */
			clr_ex(axp);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);/* Resets state counters */
			start_timer(&axp->t3);
			if(!axp->s_upcall){
				struct ax_route *axr;
				axr = ax_routeptr(axp->hdr.dest,0);
				if(axr && axr->jumpstart)
					axserv_open(axp,0);
			}
			} else {
				switch(axp->peer->state){
				case LAPB_DISCONNECTED:
					sendctl(axp->peer,LAPB_COMMAND,SABM|PF);
					start_timer(&axp->peer->t1);
					lapbstate(axp->peer,LAPB_SETUP);
					break;
				case LAPB_SETUP:
					if(axp->peer->routing_changes < 3){
						build_path(axp->peer,NULLIF,hdr,0);
						sendctl(axp->peer,LAPB_COMMAND,SABM|PF);
						start_timer(&axp->peer->t1);
					}
					break;
				default:
					if(poll)
						sendctl(axp,LAPB_RESPONSE,DM|pf);
					break;
				}
			}
			break;
		case DM:        /* Ignore to avoid infinite loops */
			break;
		default:        /* All others get DM */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		if(axp->state == LAPB_DISCONNECTED &&
		   (axp->peer == NULLAX25 || axp->peer->state == LAPB_DISCONNECTED)){
			if(axp->peer != NULLAX25)
				del_ax25(axp->peer);
			del_ax25(axp);
			free_p(bp);
			return 0;
		}
		break;
	case LAPB_SETUP:
		switch(type){
		case SABM:      /* Simultaneous open */
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:        /* Connection accepted */
			/* Note: xmit queue not cleared */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);
			if(axp->peer){
				sendctl(axp->peer,LAPB_RESPONSE,UA|PF);
				start_timer(&axp->peer->t3);
				lapbstate(axp->peer,LAPB_CONNECTED);
			}
			break;
		case DM:        /* Connection refused */
			if(axp->peer)
				sendctl(axp->peer,LAPB_RESPONSE,DM|PF);
			free_q(&axp->txq);
			stop_timer(&axp->t1);
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		default:        /* All other frames ignored */
			break;
		}
		break;
	case LAPB_DISCPENDING:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:
		case DM:
			stop_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		default:        /* Respond with DM only to command polls */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		break;
	case LAPB_CONNECTED:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			/* free_q(&axp->txq); */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			for(tmp = 0; tmp < 8; tmp++){
				free_p(axp->reseq[tmp].bp);
				axp->reseq[tmp].bp = NULLBUF;
			}
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);   /* Re-establish */
			break;
		case FRMR:
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);   /* Re-establish link */
			break;
		case RR:
		case RNR:
			if(type == RNR){
				if(!axp->flags.remotebusy)
					axp->flags.remotebusy = msclock();
				start_timer(&axp->t4);
			} else {
				axp->flags.remotebusy = NO;
				stop_timer(&axp->t4);
			}
			if(poll)
				enq_resp(axp);
			ackours(axp,nr,0);
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			if(poll)
				enq_resp(axp);
			ackours(axp,nr,1);
			/* stop_timer(&axp->t1); */
			/* start_timer(&axp->t3); */
			/* This may or may not actually invoke transmission,
			 * depending on whether this REJ was caused by
			 * our losing his prior ACK.
			 */
			/* inv_rex(axp); */
			break;
		case I:
			ackours(axp,nr,0); /** == -1) */
			resequence(axp,bp,ns,pf,poll);
			bp = NULLBUF;
			break;
		default:        /* All others ignored */
			break;
		}
		break;
	case LAPB_RECOVERY:
		switch(type){
		case SABM:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			for(tmp = 0; tmp < 8; tmp++){
				free_p(axp->reseq[tmp].bp);
				axp->reseq[tmp].bp = NULLBUF;
			}
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->response = DM;
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);   /* Re-establish */
			break;
		case FRMR:
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);   /* Re-establish link */
			break;
		case RR:
		case RNR:
			if(type == RNR){
				if(!axp->flags.remotebusy)
					axp->flags.remotebusy = msclock();
				start_timer(&axp->t4);
			} else {
				axp->flags.remotebusy = NO;
				stop_timer(&axp->t4);
			}
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr,1);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll)
					enq_resp(axp);
				ackours(axp,nr,0);
				/* Keep timer running even if all frames
				 * were acked, since we must see a Final
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			/* Don't insist on a Final response from the old proto */
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr,1);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll)
					enq_resp(axp);
				ackours(axp,nr,1);
				if(axp->unack != 0){
					/* This is certain to trigger output */
					inv_rex(axp);
				}
				/* A REJ that acks everything but doesn't
				 * have the F bit set can cause a deadlock.
				 * So make sure the timer is running.
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case I:
			ackours(axp,nr,0); /** == -1) */
			/* Make sure timer is running, since an I frame
			 * cannot satisfy a poll
			 */
			if(!run_timer(&axp->t1))
				start_timer(&axp->t1);
			resequence(axp,bp,ns,pf,poll);
			bp = NULLBUF;
			break;
		default:
			break;          /* Ignored */
		}
		break;
	}
	free_p(bp);     /* In case anything's left */

	/* See if we can send some data, perhaps piggybacking an ack.
	 * If successful, lapb_output will clear axp->response.
	 */
	lapb_output(axp);
	if(axp->response != 0){
		sendctl(axp,LAPB_RESPONSE,axp->response);
		axp->response = 0;
	}
	if((axp->state == LAPB_RECOVERY || axp->state == LAPB_CONNECTED) &&
	   (axp->flags.closed && !axp->txq ||
	    axp->flags.remotebusy && msclock() - axp->flags.remotebusy > 900000L)){
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
	}
	return 0;
}
/* Handle incoming acknowledgements for frames we've sent.
 * Free frames being acknowledged.
 * Return -1 to cause a frame reject if number is bad, 0 otherwise
 */
static int
ackours(axp,n,rex_all)
struct ax25_cb *axp;
int16 n;
int rex_all;
{
	struct mbuf *bp;
	int acked = 0;  /* Count of frames acked by this ACK */
	int16 oldest;   /* Seq number of oldest unacked I-frame */
	int32 rtt,abserr;
	int32 tmp;

	/* Free up acknowledged frames by purging frames from the I-frame
	 * transmit queue. Start at the remote end's last reported V(r)
	 * and keep going until we reach the new sequence number.
	 * If we try to free a null pointer,
	 * then we have a frame reject condition.
	 */
	oldest = (axp->vs - axp->unack) & MMASK;
	while(axp->unack != 0 && oldest != n){
		if((bp = dequeue(&axp->txq)) == NULLBUF){
			/* Acking unsent frame */
			return -1;
		}
		free_p(bp);
		axp->unack--;
		acked++;
		if(axp->flags.rtt_run && axp->rtt_seq == oldest){
			/* A frame being timed has been acked */
			axp->flags.rtt_run = 0;
			/* Update only if frame wasn't retransmitted */
			if(!axp->flags.retrans){
				rtt = msclock() - axp->rtt_time;
				abserr = (rtt > axp->srt) ? rtt - axp->srt :
				 axp->srt - rtt;

				/* Run SRT and mdev integrators */
				axp->srt = ((axp->srt * 7) + rtt + 4) >> 3;
				axp->mdev = ((axp->mdev*3) + abserr + 2) >> 2;
				/* Update timeout */
				tmp = 4*axp->mdev+axp->srt;
				set_timer(&axp->t1,max(tmp,500));
				if(axp->maxframe < Maxframe)
					axp->maxframe++;
			}
		}
		axp->flags.retrans = 0;
		axp->retries = 0;
		oldest = (oldest + 1) & MMASK;
	}
	if(axp->unack == 0){
		/* All frames acked, stop timeout */
		stop_timer(&axp->t1);
		start_timer(&axp->t3);
	} else if(acked != 0) {
		/* Partial ACK; restart timer */
		start_timer(&axp->t1);
	}
	if(rex_all){
		axp->vs -= axp->unack;
		axp->vs &= MMASK;
		axp->unack = 0;
	}
	if(acked != 0){
		/* If user has set a transmit upcall, indicate how many frames
		 * may be queued
		 */
		tmp = (axp->maxframe - len_q(axp->txq)) * axp->paclen;
		if(axp->t_upcall != NULLVFP && tmp > 0)
			(*axp->t_upcall)(axp,tmp);
		if(axp->peer && axp->peer->flags.rnrsent && !busy(axp->peer))
			sendctl(axp->peer,LAPB_RESPONSE,RR);
	}
	return 0;
}

/* Establish data link */
void
est_link(axp)
struct ax25_cb *axp;
{
	clr_ex(axp);
	axp->retries = 0;
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}
/* Clear exception conditions */
static void
clr_ex(axp)
struct ax25_cb *axp;
{
	axp->flags.remotebusy = NO;
	stop_timer(&axp->t4);
	axp->flags.rejsent = NO;
	axp->response = 0;
	stop_timer(&axp->t3);
}
/* Enquiry response */
static void
enq_resp(axp)
struct ax25_cb *axp;
{
	char ctl;

	ctl = busy(axp) ? RNR|PF : RR|PF;
	sendctl(axp,LAPB_RESPONSE,ctl);
	axp->response = 0;
	/* stop_timer(&axp->t3); */
}
/* Invoke retransmission */
static void
inv_rex(axp)
struct ax25_cb *axp;
{
	axp->vs -= axp->unack;
	axp->vs &= MMASK;
	axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(axp,cmdrsp,cmd)
struct ax25_cb *axp;
int cmdrsp;
int cmd;
{
	switch(cmd & ~PF){
	case RR:
	case REJ:
	case UA:
		axp->flags.rnrsent = 0;
		break;
	case RNR:
		axp->flags.rnrsent = 1;
		break;
	}
	if((ftype((char)cmd) & 0x3) == S)       /* Insert V(R) if S frame */
		cmd |= (axp->vr << 5);
	return sendframe(axp,cmdrsp,cmd,NULLBUF);
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(axp)
register struct ax25_cb *axp;
{
	register struct mbuf *bp;
	struct mbuf *tbp;
	char control;
	int sent = 0;
	int i;

	if(axp == NULLAX25
	 || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED)
	 || axp->flags.remotebusy)
		return 0;

	/* Dig into the send queue for the first unsent frame */
	bp = axp->txq;
	for(i = 0; i < axp->unack; i++){
		if(bp == NULLBUF)
			break;  /* Nothing to do */
		bp = bp->anext;
	}
	/* Start at first unsent I-frame, stop when either the
	 * number of unacknowledged frames reaches the maxframe limit,
	 * or when there are no more frames to send
	 */
	while(bp != NULLBUF && axp->unack < axp->maxframe){
		control = I | (axp->vs++ << 1) | (axp->vr << 5);
		axp->vs &= MMASK;
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp == NULLBUF)
			return sent;    /* Probably out of memory */
		sendframe(axp,LAPB_COMMAND,control,tbp);
		axp->unack++;
		/* We're implicitly acking any data he's sent, so stop any
		 * delayed ack
		 */
		axp->response = 0;
		if(!run_timer(&axp->t1)){
			stop_timer(&axp->t3);
			start_timer(&axp->t1);
		}
		sent++;
		bp = bp->anext;
		if(!axp->flags.rtt_run){
			/* Start round trip timer */
			axp->rtt_seq = (control >> 1) & MMASK;
			axp->rtt_time = msclock();
			axp->flags.rtt_run = 1;
		}
	}
	return sent;
}
/* Set new link state */
void
lapbstate(axp,s)
struct ax25_cb *axp;
int s;
{
	int oldstate;

	oldstate = axp->state;
	axp->state = s;
	if(s == LAPB_DISCONNECTED){
		stop_timer(&axp->t1);
		stop_timer(&axp->t3);
		stop_timer(&axp->t4);
		free_q(&axp->txq);
		if (axp->peer)
			disc_ax25(axp->peer);
		if (axp->s_upcall == NULLVFP &&
		    (!axp->peer || axp->peer->state == LAPB_DISCONNECTED)) {
			if (axp->peer != NULLAX25)
				del_ax25(axp->peer);
			del_ax25(axp);
			return;
		}
	}
	/* Don't bother the client unless the state is really changing */
	if(oldstate != s && axp->s_upcall != NULLVFP)
		(*axp->s_upcall)(axp,oldstate,s);
}
/* Resequence a valid incoming I frame */
static void
resequence(axp,bp,ns,pf,poll)
struct ax25_cb *axp;
struct mbuf *bp;
int ns;
int pf;
int poll;
{

	char *p;
	int cnt;
	int old_vr;
	int rejcond;
	int sum;
	int tmp;
	struct axreseq *rp;
	struct mbuf *tp;

	if(bp == NULLBUF)
		return;

	rp = &axp->reseq[ns];
	if(ns != ((axp->vr - 1) & MMASK) && rp->bp == NULLBUF){
		for(sum = 0,tp = bp;tp;tp = tp->next)
			for(p = tp->data,cnt = tp->cnt;cnt > 0;cnt--)
				sum += *p++ & 0xff;
		if(ns == axp->vr || sum != rp->sum){
			rp->sum = sum;
			rp->bp = bp;
			bp = NULLBUF;
		}
	}
	if(bp != NULLBUF)
		free_p(bp);

	old_vr = axp->vr;
	while(axp->reseq[axp->vr].bp != NULLBUF){
		axp->vr = (axp->vr + 1) & MMASK;
		axp->flags.rejsent = NO;
	}
	rejcond = 0;
	for(tmp = (axp->vr + 1) & MMASK;tmp != old_vr;tmp = (tmp + 1) & MMASK)
		if(axp->reseq[tmp].bp != NULLBUF){
			rejcond = 1;
			break;
		}

	if(rejcond && !axp->flags.rejsent){
		axp->flags.rejsent = YES;
		sendctl(axp,LAPB_RESPONSE,REJ|pf);
	} else {
		tmp = busy(axp) ? RNR : RR;
		if(poll)
			sendctl(axp,LAPB_RESPONSE,tmp|pf);
		else
			axp->response = tmp;
	}

	while((bp = axp->reseq[old_vr].bp) != NULLBUF){
		axp->reseq[old_vr].bp = NULLBUF;
		old_vr = (old_vr + 1) & MMASK;
		if(axp->peer)
			send_ax25(axp->peer,bp,-1);
		else
			procdata(axp,bp);
	}
}

/* Process a valid incoming I frame */
static void
procdata(axp,bp)
struct ax25_cb *axp;
struct mbuf *bp;
{
	int pid;
	int seq;

	/* Extract level 3 PID */
	if((pid = PULLCHAR(&bp)) == -1)
		return; /* No PID */

	if(axp->segremain != 0){
		/* Reassembly in progress; continue */
		seq = PULLCHAR(&bp);
		if(pid == PID_SEGMENT
		 && (seq & SEG_REM) == axp->segremain - 1){
			/* Correct, in-order segment */
			append(&axp->rxasm,bp);
			if((axp->segremain = (seq & SEG_REM)) == 0){
				/* Done; kick it upstairs */
				bp = axp->rxasm;
				axp->rxasm = NULLBUF;
				pid = PULLCHAR(&bp);
				handleit(axp,pid,bp);
			}
		} else {
			/* Error! */
			free_p(axp->rxasm);
			axp->rxasm = NULLBUF;
			axp->segremain = 0;
			free_p(bp);
		}
	} else {
		/* No reassembly in progress */
		if(pid == PID_SEGMENT){
			/* Start reassembly */
			seq = PULLCHAR(&bp);
			if(!(seq & SEG_FIRST)){
				free_p(bp);     /* not first seg - error! */
			} else {
				/* Put first segment on list */
				axp->segremain = seq & SEG_REM;
				axp->rxasm = bp;
			}
		} else {
			/* Normal frame; send upstairs */
			handleit(axp,pid,bp);
		}
	}
}
/* New-style frame segmenter. Returns queue of segmented fragments, or
 * original packet if small enough
 */
struct mbuf *
segmenter(bp,ssize)
struct mbuf *bp;        /* Complete packet */
int16 ssize;            /* Max size of frame segments */
{
	struct mbuf *result = NULLBUF;
	struct mbuf *bptmp;
	int16 len,offset;
	int segments;

	/* See if packet is too small to segment. Note 1-byte grace factor
	 * so the PID will not cause segmentation of a 256-byte IP datagram.
	 */
	len = len_p(bp);
	if(len <= ssize+1)
		return bp;      /* Too small to segment */

	ssize -= 2;             /* ssize now equal to data portion size */
	segments = 1 + (len - 1) / ssize;       /* # segments  */
	offset = 0;

	while(segments != 0){
		offset += dup_p(&bptmp,bp,offset,ssize);
		if(bptmp == NULLBUF){
			free_q(&result);
			break;
		}
		/* Make room for segmentation header */
		bptmp = pushdown(bptmp,2);
		bptmp->data[0] = PID_SEGMENT;
		bptmp->data[1] = --segments;
		if(offset == ssize)
			bptmp->data[1] |= SEG_FIRST;
		enqueue(&result,bptmp);
	}
	free_p(bp);
	return result;
}

static void
handleit(axp,pid,bp)
struct ax25_cb *axp;
int pid;
struct mbuf *bp;
{
	struct axlink *ipp;

	for(ipp = Axlink;ipp->funct        ;ipp++){
		if(ipp->pid == pid)
			break;
	}
	if(ipp->funct        )
		(*ipp->funct)(axp->iface,axp,axp->hdr.dest,axp->hdr.source,bp,0);
	else
		free_p(bp);
}

int
busy(axp)
struct ax25_cb *axp;
{
	return axp->peer ? space_ax25(axp->peer) <= 0 :
			   len_p(axp->rxq) >= axp->window;
}

void
build_path(axp,ifp,hdr,reverse)
struct ax25_cb *axp;
struct iface *ifp;
struct ax25 *hdr;
int reverse;
{
	int i;

	axp->routing_changes++;
	if(reverse){
		addrcp(axp->hdr.dest,hdr->source);
		addrcp(axp->hdr.source,hdr->dest);
		for(i = 0; i < hdr->ndigis; i++)
			addrcp(axp->hdr.digis[i],hdr->digis[hdr->ndigis-1-i]);
		axp->hdr.ndigis = hdr->ndigis;
		axp->hdr.nextdigi = 0;
		for(i = axp->hdr.ndigis - 1; i >= 0; i--)
			if(ismyax25addr(axp->hdr.digis[i])){
				axp->hdr.nextdigi = i + 1;
				break;
			}
		axp->iface = ifp;
	} else {
		axroute(hdr,&axp->iface);
		axp->hdr = *hdr;
	}
	axp->srt = 0;
	axp->mdev = (T1init * (1 + 2 * (axp->hdr.ndigis - axp->hdr.nextdigi)) + 2) / 4;
	set_timer(&axp->t1, 4 * axp->mdev);
}
