/* @(#) $Id: lapb.c,v 1.44 2016/03/13 14:50:38 dl9sau Exp $ */

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
#include "slhc.h"

static void handleit(struct ax25_cb *axp,int pid,struct mbuf **bp);
static void procdata(struct ax25_cb *axp,struct mbuf **bp);
static int ackours(struct ax25_cb *axp,uint n,int rex_all);
static void clr_ex(struct ax25_cb *axp);
static void enq_resp(struct ax25_cb *axp);
static void inv_rex(struct ax25_cb *axp);
static void resequence(struct ax25_cb *axp,struct mbuf **bpp,int ns,int pf,int poll);

/* Process incoming frames */
int
lapb_input(
struct iface *iface,
struct ax25 *hdr,
struct mbuf **bpp               /* Rest of frame, starting with ctl */
){

	struct ax25_cb *axp;    /* Link control structure */
	enum lapb_cmdrsp cmdrsp = hdr->cmdrsp;  /* Command/response flag */
	int control;
	int class;              /* General class (I/S/U) of frame */
	uint type;              /* Specific type (I/RR/RNR/etc) of frame */
	char pf;                /* extracted poll/final bit */
	char poll = 0;
	char final = 0;
	uint nr = 0;            /* ACK number of incoming frame */
	uint ns = 0;            /* Seq number of incoming frame */
	uint tmp;
	int digipeat;
	int32 bugfix;

	if(bpp == NULL || *bpp == NULL){
		free_p(bpp);
		return -1;
	}

	/* Extract the various parts of the control field for easy use */
	if((control = PULLCHAR(bpp)) == -1){
		free_p(bpp);    /* Probably not necessary */
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
		default:
			break;
		}
	}
	/* Are we the addressee, or are we being asked to relay?  Besides the
	 * interfaces' own callsigns this has to count the ones our links use
	 * and the ones we listen for - see ax_answers_to().  Widened, never
	 * narrowed: a frame for another interface's callsign counts as ours
	 * here exactly as it always did.
	 */
	digipeat = (ismyax25addr(hdr->dest) == NULL
		    && !ax_answers_to(iface,hdr->dest));
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
		struct ax25_cb *axlast = NULL;
		for(axp = Ax25_cb; axp; axlast = axp,axp = axp->next)
			if(addreq(hdr->source,axp->hdr.dest) &&
			    addreq(hdr->dest,axp->hdr.source)){
				if(axlast != NULL){
					axlast->next = axp->next;
					axp->next = Ax25_cb;
					Ax25_cb = axp;
				}
				break;
			}
	} else
		axp = find_ax25(hdr->source);
	if(axp == NULL){
		axp = cr_ax25((uint8 *) " ");
		build_path(axp,iface,hdr,1,0);
		if(digipeat){
			axp->peer = cr_ax25((uint8 *) " ");
			axp->peer->peer = axp;
			build_path(axp->peer,NULL,hdr,0,0);
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
			start_timer(&axp->t5);
			if(axp->services == NULL){
				struct ax_route *axr;
				axr = ax_routeptr(axp->hdr.dest,0);
				/* A callsign we listen for is handed over as
				 * soon as the link stands, not when the
				 * caller first types something - whatever is
				 * behind it may want to greet, and a mailbox
				 * always does.  hdr.source is the address
				 * that was called; the header is already
				 * turned round here.
				 */
				if(axr && axr->jumpstart)
					axserv_start(axp,PID_NO_L3);
				axserv_connected(axp);
#ifdef	AX25_VJCOMP
                            /* MW: Reset VJ structures */
                            if (axp->slcomp) {
                              slhc_free(axp->slcomp);
                              axp->slcomp = NULL;
                              printf("VJ reset on ax_bc %p\n", axp);
                            }
                            if (axr && axr->vjcomp) {
                                axp->slcomp_enable = 1;
                                printf("VJ init on ax_cb %p\n", axp);
                            }
#endif
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
						build_path(axp->peer,NULL,hdr,0,0);
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
		   (axp->peer == NULL || axp->peer->state == LAPB_DISCONNECTED)){
			if(axp->peer != NULL)
				del_ax25(axp->peer);
			del_ax25(axp);
			free_p(bpp);
			return 0;
		}
		break;
	case LAPB_LISTEN:
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
			start_timer(&axp->t5);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);
			if(axp->peer){
				sendctl(axp->peer,LAPB_RESPONSE,UA|PF);
				start_timer(&axp->peer->t3);
				start_timer(&axp->peer->t5);
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
			free_p(bpp);
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
			free_p(bpp);
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
			start_timer(&axp->t5);
			for(tmp = 0; tmp < 8; tmp++){
				free_p(&axp->reseq[tmp].bp);
			}
			axp->unack = axp->vr = axp->vs = 0;
#ifdef	AX25_VJCOMP
                       /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                              slhc_free(axp->slcomp);
                              axp->slcomp = NULL;
                              printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish */
			break;
		case FRMR:
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish link */
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
			start_timer(&axp->t5);
			ackours(axp,nr,0); /** == -1) */
			resequence(axp,bpp,ns,pf,poll);
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
			start_timer(&axp->t5);
			for(tmp = 0; tmp < 8; tmp++){
				free_p(&axp->reseq[tmp].bp);
			}
			axp->unack = axp->vr = axp->vs = 0;
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			/* axp->response = DM; */
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish */
			break;
		case FRMR:
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish link */
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
			start_timer(&axp->t5);
			ackours(axp,nr,0); /** == -1) */
			/* Make sure timer is running, since an I frame
			 * cannot satisfy a poll
			 */
			if(!run_timer(&axp->t1))
				start_timer(&axp->t1);
			resequence(axp,bpp,ns,pf,poll);
			break;
		default:
			break;          /* Ignored */
		}
		break;
	}
	free_p(bpp);    /* In case anything's left */

	/* See if we can send some data, perhaps piggybacking an ack.
	 * If successful, lapb_output will clear axp->response.
	 */
	lapb_output(axp);
	/* if(axp->response != 0){ */
	/*      sendctl(axp,LAPB_RESPONSE,axp->response); */
	/*      axp->response = 0; */
	/* } */
	if((axp->state == LAPB_RECOVERY || axp->state == LAPB_CONNECTED) &&
	   ((axp->flags.closed && !axp->txq) ||
	    (axp->flags.remotebusy && (bugfix = msclock() - axp->flags.remotebusy) > 900000L))){
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
ackours(
struct ax25_cb *axp,
uint n,
int rex_all
){
	struct mbuf *bp;
	int acked = 0;  /* Count of frames acked by this ACK */
	uint oldest;    /* Seq number of oldest unacked I-frame */
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
		if((bp = dequeue(&axp->txq)) == NULL){
			/* Acking unsent frame */
			return -1;
		}
		free_p(&bp);
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
			axp->flags.retrans = 0;
		}
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
		axp->flags.retrans = 1;
		axp->vs -= axp->unack;
		axp->vs &= MMASK;
		axp->unack = 0;
	}
	if(acked != 0){
		/* If user has set a transmit upcall, indicate how many frames
		 * may be queued
		 */
		tmp = (axp->maxframe - len_q(axp->txq)) * axp->paclen;
		if(tmp > 0){
			/* Room again.  Every consumer is told; the send queue
			 * is shared and whoever has something puts it in,
			 * first come first served.
			 */
			struct axservice *sp;
			struct axservice *spnext;

			for(sp = axp->services; sp != NULL; sp = spnext){
				spnext = sp->next;
				if(sp->t_upcall != NULL)
					(*sp->t_upcall)(sp,(int)tmp);
			}
		}
		if(axp->peer && axp->peer->flags.rnrsent && !busy(axp->peer))
			sendctl(axp->peer,LAPB_RESPONSE,RR);
	}
	return 0;
}

/* Establish data link */
void
est_link(struct ax25_cb *axp)
{
	clr_ex(axp);
	axp->retries = 0;
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}
/* Clear exception conditions */
static void
clr_ex(struct ax25_cb *axp)
{
	axp->flags.remotebusy = NO;
	stop_timer(&axp->t4);
	axp->flags.rejsent = NO;
	/* axp->response = 0; */
	stop_timer(&axp->t3);
}
/* Enquiry response */
static void
enq_resp(struct ax25_cb *axp)
{
	char ctl;

	ctl = busy(axp) ? RNR|PF : RR|PF;
	sendctl(axp,LAPB_RESPONSE,ctl);
	/* axp->response = 0; */
	/* stop_timer(&axp->t3); */
}
/* Invoke retransmission */
static void
inv_rex(struct ax25_cb *axp)
{
	axp->vs -= axp->unack;
	axp->vs &= MMASK;
	axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(
struct ax25_cb *axp,
enum lapb_cmdrsp cmdrsp,
int cmd
){
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
	return sendframe(axp,cmdrsp,cmd,NULL);
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(struct ax25_cb *axp)
{
	struct mbuf *bp;
	struct mbuf *tbp;
	char control;
	int sent = 0;
	int i;

	if(axp == NULL
	 || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED)
	 || axp->flags.remotebusy)
		return 0;

	/* Dig into the send queue for the first unsent frame */
	bp = axp->txq;
	for(i = 0; i < axp->unack; i++){
		if(bp == NULL)
			break;  /* Nothing to do */
		bp = bp->anext;
	}
	/* Start at first unsent I-frame, stop when either the
	 * number of unacknowledged frames reaches the maxframe limit,
	 * or when there are no more frames to send
	 */
	while(bp != NULL && axp->unack < axp->maxframe){
		control = I | (axp->vs++ << 1) | (axp->vr << 5);
		axp->vs &= MMASK;
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp == NULL)
			return sent;    /* Probably out of memory */
		sendframe(axp,LAPB_COMMAND,control,&tbp);
		axp->unack++;
		/* We're implicitly acking any data he's sent, so stop any
		 * delayed ack
		 */
		/* axp->response = 0; */
		if(!run_timer(&axp->t1)){
			stop_timer(&axp->t3);
			start_timer(&axp->t1);
		}
		start_timer(&axp->t5);
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
/* General purpose AX.25 frame output */
int
sendframe(
struct ax25_cb *axp,
enum lapb_cmdrsp cmdrsp,
int ctl,
struct mbuf **bpp
){
	struct iface *ifp;
	struct mbuf *bp;

	if(bpp == NULL){
		bp = NULL;
		bpp = &bp;
	}
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;
	if ((ctl & 3) != U)
		stop_timer(&axp->t2);
	axp->hdr.cmdrsp = cmdrsp;
	htonax25(&axp->hdr,bpp);
	if ((ifp = axp->iface)) {
		if (ifp->forw)
			ifp = ifp->forw;
		logsrc(ifp,ifp->hwaddr);
		logdest(ifp,axp->hdr.nextdigi != axp->hdr.ndigis ? axp->hdr.digis[axp->hdr.nextdigi] : axp->hdr.dest);
		return (*ifp->raw)(ifp,bpp);
	}
	free_p(bpp);
	return -1;
}
/* Set new link state */
void
lapbstate(
struct ax25_cb *axp,
enum lapb_state s
){
	enum lapb_state oldstate;

	oldstate = axp->state;
	axp->state = s;
	if(s == LAPB_DISCONNECTED){
		stop_timer(&axp->t1);
		stop_timer(&axp->t2);
		stop_timer(&axp->t3);
		stop_timer(&axp->t4);
		stop_timer(&axp->t5);
		free_q(&axp->txq);
		if (axp->peer)
			disc_ax25(axp->peer);
		if (axp->services == NULL &&
		    (!axp->peer || axp->peer->state == LAPB_DISCONNECTED)) {
			if (axp->peer != NULL)
				del_ax25(axp->peer);
			del_ax25(axp);
			return;
		}
	}
	/* Don't bother the consumers unless the state is really changing.
	 * Walk with the next pointer in hand: one of them may close itself
	 * here, and closing the last takes the link with it.
	 */
	if(oldstate != s){
		struct axservice *sp;
		struct axservice *spnext;

		for(sp = axp->services; sp != NULL; sp = spnext){
			spnext = sp->next;
			if(sp->s_upcall != NULL)
				(*sp->s_upcall)(sp,oldstate,s);
		}
	}
}
/* Resequence a valid incoming I frame */
static void
resequence(
struct ax25_cb *axp,
struct mbuf **bpp,
int ns,
int pf,
int poll)
{

	int cnt;
	int old_vr;
	int rejcond;
	int sum;
	int tmp;
	struct axreseq *rp;
	struct mbuf *bp;
	struct mbuf *tp;
	uint8 *p;

	if(bpp == NULL || *bpp == NULL)
		return;

	rp = &axp->reseq[ns];
	if(ns != ((axp->vr - 1) & MMASK) && rp->bp == NULL){
		for(sum = 0,tp = *bpp;tp;tp = tp->next)
			for(p = tp->data,cnt = tp->cnt;cnt > 0;cnt--)
				sum += *p++;
		if(ns == axp->vr || sum != rp->sum){
			rp->sum = sum;
			rp->bp = *bpp;
			*bpp = NULL;
		}
	}
	if(*bpp != NULL)
		free_p(bpp);

	old_vr = axp->vr;
	while(axp->reseq[axp->vr].bp != NULL){
		axp->vr = (axp->vr + 1) & MMASK;
		axp->flags.rejsent = NO;
	}
	rejcond = 0;
	for(tmp = (axp->vr + 1) & MMASK;tmp != old_vr;tmp = (tmp + 1) & MMASK)
		if(axp->reseq[tmp].bp != NULL){
			rejcond = 1;
			break;
		}

	if(rejcond && !axp->flags.rejsent){
		axp->flags.rejsent = YES;
		sendctl(axp,LAPB_RESPONSE,REJ|pf);
	} else {
		if(poll){
			tmp = busy(axp) ? RNR : RR;
			sendctl(axp,LAPB_RESPONSE,tmp|pf);
		} else
			start_timer(&axp->t2);
	}

	while((bp = axp->reseq[old_vr].bp) != NULL){
		axp->reseq[old_vr].bp = NULL;
		old_vr = (old_vr + 1) & MMASK;
		if(axp->peer)
			send_ax25(axp->peer,&bp,-1);
		else
			procdata(axp,&bp);
	}
}

/* Process a valid incoming I frame */
static void
procdata(
struct ax25_cb *axp,
struct mbuf **bpp
){
	int pid;
	int seq;

	/* Extract level 3 PID */
	if((pid = PULLCHAR(bpp)) == -1)
		return; /* No PID */

	if(axp->segremain != 0){
		/* Reassembly in progress; continue */
		seq = PULLCHAR(bpp);
		if(pid == PID_SEGMENT
		 && (seq & SEG_REM) == axp->segremain - 1){
			/* Correct, in-order segment */
			append(&axp->rxasm,bpp);
			if((axp->segremain = (seq & SEG_REM)) == 0){
				/* Done; kick it upstairs */
				*bpp = axp->rxasm;
				axp->rxasm = NULL;
				pid = PULLCHAR(bpp);
				handleit(axp,pid,bpp);
			}
		} else {
			/* Error! */
			free_p(&axp->rxasm);
			axp->rxasm = NULL;
			axp->segremain = 0;
			free_p(bpp);
		}
	} else {
		/* No reassembly in progress */
		if(pid == PID_SEGMENT){
			/* Start reassembly */
			seq = PULLCHAR(bpp);
			if(!(seq & SEG_FIRST)){
				free_p(bpp);    /* not first seg - error! */
			} else {
				/* Put first segment on list */
				axp->segremain = seq & SEG_REM;
				axp->rxasm = (*bpp);
				*bpp = NULL;
			}
		} else {
			/* Normal frame; send upstairs */
			handleit(axp,pid,bpp);
		}
	}
}
/* New-style frame segmenter. Returns queue of segmented fragments, or
 * original packet if small enough
 */
struct mbuf *
segmenter(
struct mbuf **bpp,      /* Complete packet */
uint ssize              /* Max size of frame segments */
){
	struct mbuf *result = NULL;
	struct mbuf *bptmp;
	uint len,offset;
	int segments;

	/* Is it small enough to go in one frame?  len counts the PID that the
	 * caller pushed on a moment ago, and the PID is not part of the
	 * information field that paclen bounds - it sits in front of it, in
	 * the header.  So a datagram of exactly paclen bytes still fits, and
	 * the comparison has to be against paclen+1.  This is bookkeeping,
	 * not slack: the frame that goes out has an information field of at
	 * most paclen bytes either way.
	 */
	len = len_p(*bpp);
	if(len <= ssize+1){
		result = *bpp;
		*bpp = NULL;
		return result;  /* Too small to segment */
	}
	/* Below paclen 2 there is nothing left once the counter has its byte,
	 * and ssize would be zero for the division just below.  "ax25 paclen
	 * 1" is accepted by dopaclen() and used to kill the node with SIGFPE
	 * right here.
	 */
	if(ssize < 2){
		free_p(bpp);
		return NULL;
	}
	/* The segment counter is the first byte OF the information field, so
	 * it comes out of paclen.  The PID_SEGMENT byte does not - that one
	 * is header again.  Hence one, not two.
	 */
	ssize -= 1;
	segments = 1 + (len - 1) / ssize;       /* # segments  */
	/* Bit 7 of that counter is SEG_FIRST, so it can count no further than
	 * SEG_REM.  Sending more anyway sets SEG_FIRST on every segment whose
	 * count still has bit 7, and the far end then reassembles a truncated
	 * datagram from the first few and discards all the rest, one by one,
	 * as "not first segment".  Refuse instead: the datagram cannot be
	 * expressed in this protocol at this paclen.
	 *
	 * The largest datagram that still fits is 128 * (paclen-1) - 1, so at
	 * paclen 256 it would take 32640 bytes to get here and the case is
	 * out of reach.  At paclen 12 or less an ordinary MTU of 1500 walks
	 * straight into it.
	 */
	if(segments - 1 > SEG_REM){
		free_p(bpp);
		return NULL;
	}
	offset = 0;

	while(segments != 0){
		offset += dup_p(&bptmp,*bpp,offset,ssize);
		if(bptmp == NULL){
			free_q(&result);
			break;
		}
		/* Make room for segmentation header */
		pushdown(&bptmp,NULL,2);
		bptmp->data[0] = PID_SEGMENT;
		bptmp->data[1] = --segments;
		if(offset == ssize)
			bptmp->data[1] |= SEG_FIRST;
		enqueue(&result,&bptmp);
	}
	free_p(bpp);
	return result;
}

static void
handleit(
struct ax25_cb *axp,
int pid,
struct mbuf **bpp
){
	struct axlink *ipp;
	struct axservice *sp;

	/* A consumer for this protocol id first: one that is already attached,
	 * or one the configuration says to start.  Only then the node's own
	 * protocols, so that a service configured for a callsign takes what
	 * arrives there without taking the protocol away from the node.
	 */
	if((sp = find_axservice(axp,pid)) == NULL)
		sp = axserv_start(axp,pid);
	if(sp != NULL){
		/* One frame stays one entry on the queue.  Kernel AX.25 was
		 * SOCK_SEQPACKET and the protocols that ride on a connection
		 * rely on it: FBB's compressed forwarding reads the end of an
		 * uncompressed block off the frame boundary.  append() would
		 * melt two frames into one chain and there is no recovering
		 * that afterwards.
		 */
		enqueue(&sp->rxq,bpp);
		if(sp->r_upcall != NULL)
			(*sp->r_upcall)(sp,(int) len_qbytes(sp->rxq));
		return;
	}

	for(ipp = Axlink;ipp->funct != NULL;ipp++){
		if(ipp->pid == pid)
			break;
	}
	if(ipp->funct != NULL){
		(*ipp->funct)(axp->iface,axp,axp->hdr.dest,axp->hdr.source,bpp,0);
	}
	else
		free_p(bpp);
}

int
busy(
struct ax25_cb *axp)
{
	int held;
	struct axservice *sp;

	if(axp->peer)
		return space_ax25(axp->peer) <= 0;
	/* One window for the whole link: what any consumer has not taken
	 * counts against it.  AX.25 cannot say "not ready for this pid".
	 */
	for(sp = axp->services, held = 0; sp != NULL; sp = sp->next)
		held += (int) len_qbytes(sp->rxq);
	return held >= (int) axp->window;
}

void
ax_t2_timeout(
void *p)
{
	struct ax25_cb *axp;
	int i;

	axp = (struct ax25_cb *)p;
	if (!axp->flags.rejsent) {
		for (i = 0; i < 8; i++) {
			if (axp->reseq[i].bp) {
				axp->flags.rejsent = YES;
				sendctl(axp, LAPB_RESPONSE, REJ);
				return;
			}
		}
	}
	sendctl(axp, LAPB_RESPONSE, busy(axp) ? RNR : RR);
}

void
ax_t5_timeout(
void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;
	if(axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY){
		free_q(&axp->txq);
		axp->retries = 0;
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		stop_timer(&axp->t3);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
	}
}

void
build_path(
struct ax25_cb *axp,
struct iface *ifp,
struct ax25 *hdr,
int reverse,
const struct ax25_opts *opts)
{
	int i;
	uint8 wanted[AXALEN];

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
		/* axroute() picks the interface and stamps its callsign over
		 * the source.  Where the caller has chosen either of those,
		 * put its choice back afterwards rather than teaching the
		 * router about it - the routing itself is unchanged.
		 */
		addrcp(wanted, hdr->source);
		axroute(hdr,&axp->iface);
		if (opts) {
			if (opts->iface) {
				axp->iface = opts->iface;
				/* axroute() stamps the source with the
				 * callsign of the interface it chose; a
				 * caller who named a port must get that
				 * port's callsign instead, not the node's.
				 */
				if (!opts->ownsource && axp->iface->hwaddr)
					addrcp(hdr->source,
					       axp->iface->hwaddr);
			}
			if (opts->ownsource)
				addrcp(hdr->source, wanted);
		}
		axp->hdr = *hdr;
	}
	axp->srt = 0;
	axp->mdev = (T1init * (1 + 2 * (axp->hdr.ndigis - axp->hdr.nextdigi)) + 2) / 4;
	set_timer(&axp->t1, 4 * axp->mdev);
}

