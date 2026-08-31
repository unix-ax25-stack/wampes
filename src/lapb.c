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
#include "dama.h"
#include "ip.h"
#include "slhc.h"
#include "pidfilter.h"

static void handleit(struct ax25_cb *axp,int pid,struct mbuf **bp);
static void procdata(struct ax25_cb *axp,struct mbuf **bp);
static int ackours(struct ax25_cb *axp,uint n,int rex_all);
static void clr_ex(struct ax25_cb *axp);
static void enq_resp(struct ax25_cb *axp);
static void inv_rex(struct ax25_cb *axp);
static void resequence(struct ax25_cb *axp,struct mbuf **bpp,int ns,int pf,int poll);
static int eax25_wanted(struct iface *ifp,uint8 *dest,struct ax25_cb *from);
static void eax25_mark(struct ax25_cb *axp);
static void eax25_hint(struct iface *ifp,struct ax25 *hdr);

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
	int controlx = -1;      /* Second control octet, modulo-128 only */
	int class;              /* General class (I/S/U) of frame */
	uint type;              /* Specific type (I/RR/RNR/etc) of frame */
	char pf;                /* extracted poll/final bit */
	char poll = 0;
	char final = 0;
	uint nr = 0;            /* ACK number of incoming frame */
	uint ns = 0;            /* Seq number of incoming frame */
	uint tmp;
	int digipeat;

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
	/* Are we the addressee, or are we being asked to relay?  Besides the
	 * interfaces' own callsigns this has to count the ones our links use
	 * and the ones we listen for - see ax_answers_to().  Widened, never
	 * narrowed: a frame for another interface's callsign counts as ours
	 * here exactly as it always did.
	 */
	digipeat = (ismyax25addr(hdr->dest) == NULL
		    && !ax_answers_to(iface,hdr->dest));
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
		/* THE PAIR, exactly as the digipeat branch above has always
		 * done it, only the other way round: the frame is addressed
		 * to us, so hdr->dest is the callsign of ours it was sent to
		 * and hdr->source is his.  Looking him up alone found the
		 * first link to him under ANY of our callsigns - and a SABM
		 * landing on that link is answered with UA and resets its
		 * counters, so a second session to a different service of
		 * ours tore down the first instead of opening beside it.
		 */
		axp = find_ax25(hdr->dest,hdr->source);
	if(axp == NULL){
		/* No address yet: build_path() fills the header in, and the
		 * VJ setting with it, from the frame.
		 */
		axp = cr_ax25(NULL);
		build_path(axp,iface,hdr,1,0);
		if(digipeat){
			axp->peer = cr_ax25(NULL);
			axp->peer->peer = axp;
			build_path(axp->peer,NULL,hdr,0,0);
		}
	}

	if(cmdrsp == LAPB_UNKNOWN)
		axp->proto = V1;        /* Old protocol in use */

	/* A SABM or SABME does not merely open a link, it states which modulus
	 * the link runs on, and either may arrive at any time - including on
	 * one that is already up.  Taking what the frame says, here and once,
	 * is what AX.25 2.2 asks for and what the Linux kernel does in
	 * ax25_std_state3_machine().  Deciding it separately in every state is
	 * how the two drifted apart in the patch this was built from.
	 */
	/* A port set to "eax25 off" refuses modulo-128 from the other side too.
	 * DM is the right answer and not silence: it tells him at once that we
	 * are here and that this is not the way, so his own fallback starts
	 * now rather than after his retries run out.
	 */
	/* EIN BANN NACH ZU VIELEN DAMA-VERSTOESSEN, und dieselbe Antwort wie
	 * gleich darunter: DM sagt ihm sofort, dass wir da sind und dass es so
	 * nicht geht.  Wer mit gesetztem DAMA-Bit anklopft, kommt herein - das
	 * prueft dama_connect_refused() selbst.
	 */
	if((type == SABM || type == SABME) && dama_connect_refused(iface,hdr)){
		sendctl(axp,LAPB_RESPONSE,DM | (control & PF));
		free_p(bpp);
		return 0;
	}

	if(type == SABME && iface != NULL && iface->eax25 == EAX25_OFF){
		eax25_hint(iface,hdr);
		sendctl(axp,LAPB_RESPONSE,DM | (control & PF));
		free_p(bpp);
		return 0;
	}

	if(type == SABM || type == SABME){
		axp->mmask = (type == SABME) ? EMMASK : MMASK;
		if(type == SABME){
			axp->hdr.ext |= SSID_EAX25;
			/* He is calling US with it, so he can do it - the one
			 * piece of evidence that needs no probe, and the one
			 * that undoes an earlier "cannot" the moment his end
			 * is fixed.
			 */
			eax25_remember(hdr->source,AXR_EAX25_YES);

			eax25_hint(iface,hdr);
		} else
			axp->hdr.ext &= ~SSID_EAX25;
	}

	/* Now that the link is known, the rest of the control field can be
	 * read: modulo-128 carries the sequence numbers and the poll/final bit
	 * in a SECOND octet, and only for I and S frames.  A U frame has no
	 * sequence number to widen and keeps its single octet in both moduli -
	 * which is what makes SABME readable at all before anything has been
	 * agreed.
	 */
	if(class != U && axp->mmask == EMMASK){
		if((controlx = PULLCHAR(bpp)) == -1){
			free_p(bpp);
			return -1;
		}
		pf = (controlx & PF_EAX25);
	} else
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

	/* Extract sequence numbers, if present */
	switch(class){
	case I:
	case I+2:
		ns = (control >> 1) & axp->mmask;
	case S: /* Note fall-thru */
		nr = (axp->mmask == EMMASK) ? ((controlx >> 1) & EMMASK)
					    : ((control >> 5) & MMASK);
		break;
	}

	/* DAMA.  The MASTER's SSID octet carries the bit, so on a connection
	 * the user placed it arrives in the UA, not in the SABM - the SABM is
	 * ours.  Any marked frame feeds the watchdog and puts this port into
	 * DAMA mode; from then on a command with the poll bit is a poll,
	 * whether or not that particular frame is marked as well.
	 *
	 * Deliberately the liberal reading, and it is not what TNN does: TNN
	 * wraps its poll test in "if (rxfDA)" and so needs the bit on every
	 * polling frame, while Linux latches the mode at connect and then
	 * looks only at command+P.  The paper allows either - "it would be
	 * sufficient to tell the user to switch to DAMA mode only once, at
	 * connect time" - so a master may legitimately mark nothing after the
	 * UA, and requiring the bit per frame would leave us silent for ever
	 * against such a master.  Accepting both costs nothing: the watchdog
	 * already handles a master that stops speaking DAMA at all.
	 */
	if(hdr->ext & SSID_DAMA){
		dama_heard_frame(iface,hdr);
		axp->dama_link = 1;
	}
	dama_poll_begin(iface,poll,hdr);
	/* Und die andere Rolle: auf einem Master-Port endet hier der Zug des
	 * Gepollten (F-Bit), und hier wird gezaehlt, wer selbst pollt.
	 */
	dama_master_input(iface,axp,hdr,class == U,class == I,poll,
			  (cmdrsp == LAPB_RESPONSE) && (control & PF));

	/* This section follows the SDL diagrams by K3NA fairly closely */
	switch(axp->state){
	case LAPB_DISCONNECTED:
		switch(type){
		case SABM:      /* Initialize or reset link */
		case SABME:     /* the same, modulo-128 */
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
					axp->peer->mmask =
					 eax25_wanted(axp->peer->iface,
						      axp->peer->hdr.dest,axp);
					eax25_mark(axp->peer);
					axp->peer->eax25_probes = 0;
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
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:        /* Connection accepted */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_YES);
			else if(axp->eax25_tried){
				/* He ignored the SABMEs and answered the SABM.
				 * Once that read like proof; it is not.  The
				 * same picture arises when HIS UA to a SABME
				 * was lost and reaches us after we have
				 * fallen back - and then he is on modulo 128
				 * while we are on 8.  Measured against TNN at
				 * 20 % loss: the very station whose other
				 * link was running modulo 128 got marked as
				 * incapable.  So it is counted, not believed.
				 */
				eax25_failed(axp->hdr.dest);
				axp->eax25_tried = 0;
			}
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
			if(axp->mmask == EMMASK){
				/* Not a refusal - he simply does not speak
				 * modulo-128 and says so.  Ask again plainly;
				 * a DM to THAT one means there really is no
				 * service.
				 */
				eax25_fallback(axp);
				free_p(bpp);
				return 0;
			}
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
		case SABME:
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
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			/* free_q(&axp->txq); */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			for(tmp = 0; tmp <= EMMASK; tmp++){
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
			/* He accepted the SABME and then could not follow.
			 * Remember "cannot" rather than clearing what we knew:
			 * unknown would mean probing him again on the very next
			 * connect, which is the cost we became three-valued to
			 * avoid.  An incoming SABME from him undoes it.
			 */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_NO);
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
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
			ackours(axp,nr,0);
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
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
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			for(tmp = 0; tmp <= EMMASK; tmp++){
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
			/* He accepted the SABME and then could not follow.
			 * Remember "cannot" rather than clearing what we knew:
			 * unknown would mean probing him again on the very next
			 * connect, which is the cost we became three-valued to
			 * avoid.  An incoming SABME from him undoes it.
			 */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_NO);
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
				if(poll){
					if(dama_master_holds(axp))
						dama_master_owe(axp);
					else
						enq_resp(axp);
				}
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
				if(poll){
					if(dama_master_holds(axp))
						dama_master_owe(axp);
					else
						enq_resp(axp);
				}
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
	    (axp->flags.remotebusy && TDIFF(msclock(), axp->flags.remotebusy) > 900000L))
	   ){
		/* A DAMA slave waits for the poll before it disconnects too -
		 * the paper is explicit about that one ("the user ... will
		 * wait to send his DISC-frame until polled").  Nothing is lost
		 * by waiting: the wish to close is the state, and the state
		 * outlives the gate being shut.
		 */
		if(dama_holds(axp)){
			dama_wait(axp);
		} else {
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
		}
	}
	dama_serve_others(iface,axp);
	dama_poll_end(iface);
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
	oldest = (axp->vs - axp->unack) & axp->mmask;
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
				rtt = TDIFF(msclock(), axp->rtt_time);
				abserr = (rtt > axp->srt) ? rtt - axp->srt :
				 axp->srt - rtt;

				/* Run SRT and mdev integrators */
				axp->srt = ((axp->srt * 7) + rtt + 4) >> 3;
				axp->mdev = ((axp->mdev*3) + abserr + 2) >> 2;
				/* Update timeout */
				tmp = 4*axp->mdev+axp->srt;
				set_timer(&axp->t1,max(tmp,500));
				if(axp->maxframe < ax25_maxframe(axp))
					axp->maxframe++;
			}
			axp->flags.retrans = 0;
		}
		axp->retries = 0;
		oldest = (oldest + 1) & axp->mmask;
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
		axp->vs &= axp->mmask;
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

/* Remember what modulo-128 actually did with this station.  Our own traffic
 * is the only thing that teaches this - what a station claims in the EAX bit
 * of its SSID says it is willing, not that it works.
 */

void eax25_remember(uint8 *call, int verdict)
{
	struct ax_route *rp;

	if((rp = ax_routeptr(call,1)) == NULL)
		return;
	rp->eax25 = verdict;
	/* Ein Erfolg loescht die Fehlschlaege - sie waren dann Zufall, und der
	 * Zaehler soll nur zaehlen, was IN FOLGE misslingt.
	 */
	if(verdict == AXR_EAX25_YES)
		rp->eax25_fails = 0;
	if(verdict == AXR_EAX25_NO)
		rp->eax25_skips = 0;
}

/* Ein Rueckfall auf SABM, und der sagt fuer sich genommen NICHTS: er kann
 * heissen, dass die Station kein Modulo 128 kann - oder dass ein SABME
 * verlorenging, oder ihr UA darauf.  Deshalb wird er gezaehlt und nicht
 * geglaubt; erst EAX25_MAXFAILS Rueckfaelle in Folge sind eine Aussage.
 *
 * Ein FRMR geht weiter direkt an eax25_remember(): dort HAT sie das SABME
 * angenommen und konnte dann nicht folgen, das ist der eine Beweis, den
 * kein verlorener Rahmen vortaeuschen kann.
 */

void eax25_failed(uint8 *call)
{
	struct ax_route *rp;

	if((rp = ax_routeptr(call,1)) == NULL)
		return;
	if(rp->eax25 == AXR_EAX25_NO)
		return;
	if(++rp->eax25_fails >= EAX25_MAXFAILS){
		rp->eax25 = AXR_EAX25_NO;
		rp->eax25_skips = 0;
	}
}

/* The first modulo-128 call heard on a port that does not ask for it - once
 * per port and per run, whoever it came from.  The default is not to ask
 * anyone (see doc/EAX25.md), and an operator who never reads the
 * documentation would otherwise not learn that this port has partners who
 * could do better.  Reported on "off" as well: he said no once, and what he
 * hears now may be a partner who has since been replaced.
 *
 * The packet header goes with it, so it says WHO was heard and over which
 * path, instead of only that somebody was.
 *
 * Console AND log, because neither reaches everyone on its own: logmsg()
 * returns at once when no "log" file is configured, and the console is
 * nothing on a node started without one.  What survives in either case is
 * the E in "ax25 route list".
 */

static void eax25_hint(struct iface *ifp,struct ax25 *hdr)
{
	char msg[256];

	if(ifp == NULL || ifp->eax25_hinted)
		return;
	if(ifp->eax25 != EAX25_ACCEPT && ifp->eax25 != EAX25_OFF)
		return;
	ifp->eax25_hinted = 1;
	sprintf(msg,"EAX25 heard on %s. Consider \"ifconfig %s eax25 caller\"."
	 "  Packet: %s",ifp->name,ifp->name,ax25hdr_to_string(hdr));
	printf("%s\n",msg);
	logmsg(NULL,"%s",msg);
}

/* Which modulus a link we are about to open should ask for.
 *
 * "from" is the other half when we are relaying and NULL when the connect
 * starts here.  Under EAX25_CALLER that is the whole rule: a user who asked
 * for plain AX.25 is carried onward as plain AX.25, because if the upper leg
 * then misbehaves he is the one who can do nothing about it - he already used
 * the most conservative thing he has.  EAX25_ALWAYS overrules him, and that
 * is meant for an interlink whose partner the operator knows.
 */

static int eax25_wanted(struct iface *ifp, uint8 *dest, struct ax25_cb *from)
{
	struct ax_route *rp;

	/* No port yet, or a port that only answers: do not ask. */
	if(ifp == NULL || ifp->eax25 == EAX25_OFF || ifp->eax25 == EAX25_ACCEPT)
		return MMASK;
	/* "Kann nicht" gilt nicht fuer immer.  Nach EAX25_RETRY einfachen
	 * Verbindungen wird wieder gefragt, denn der Vermerk kann aus
	 * verlorenen Rahmen stammen (siehe eax25_failed).  Auf einer Strecke,
	 * die oft neu aufbaut, kommt der naechste Versuch damit frueher - und
	 * genau dort war die Fehldiagnose auch wahrscheinlicher.
	 */
	if((rp = ax_routeptr(dest,0)) != NULL && rp->eax25 == AXR_EAX25_NO){
		if(++rp->eax25_skips < EAX25_RETRY)
			return MMASK;
		rp->eax25 = AXR_EAX25_UNKNOWN;
		rp->eax25_fails = 0;
		rp->eax25_skips = 0;
	}
	if(ifp->eax25 == EAX25_ALWAYS)
		return EMMASK;
	if(from != NULL)
		return from->mmask;
	return EMMASK;
}

/* Say on the wire which modulus this link runs on.  The bit lives in the
 * source SSID, active low, and htonax25() puts it there; every monitor reads
 * it - ours in ax25dump.c decodes the two-octet control field only when it is
 * set - and TNN's "EAXMODE 1, by MHEARD" is the same bit seen from outside.
 *
 * It used to be set in one place only, when we ACCEPTED a SABME.  A link we
 * opened ourselves therefore ran modulo-128 while its frames said "cannot",
 * so our own trace could not decode our own traffic and no neighbour could
 * learn from us that we speak it.  mmask is the truth; this follows it.
 */

static void eax25_mark(struct ax25_cb *axp)
{
	if(axp->mmask == EMMASK)
		axp->hdr.ext |= SSID_EAX25;
	else
		axp->hdr.ext &= ~SSID_EAX25;
}

/* Give up on modulo-128 for this attempt and ask again the plain way.  The
 * link stays in setup: to the caller above us nothing has happened yet, and
 * that is the point - he asked for a connection, not for a modulus.
 */

void eax25_fallback(struct ax25_cb *axp)
{
	axp->mmask = MMASK;
	eax25_mark(axp);
	axp->eax25_probes = 0;
	axp->eax25_tried = 1;

	/* Start the retransmission timer over.  What the probes proved is that
	 * he does not speak modulo-128; they said nothing about how long the
	 * path is, so carrying their stretched T1 into the plain attempt would
	 * punish the connection for the wrong reason.
	 *
	 * The retry COUNT is deliberately not reset.  Left alone, the probes
	 * are paid out of the tries the caller was already willing to spend:
	 * three SABMEs and the seven SABMs that remain come to about 94 s,
	 * where ten SABMs alone take 166 s.  Falling back therefore costs him
	 * nothing - it ends sooner than not trying at all.
	 */
	set_timer(&axp->t1,4 * axp->mdev);
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	start_timer(&axp->t1);
}

/* Establish data link */
void
est_link(struct ax25_cb *axp)
{
	clr_ex(axp);
	axp->retries = 0;
	axp->eax25_probes = 0;
	axp->mmask = eax25_wanted(axp->iface,axp->hdr.dest,NULL);
	eax25_mark(axp);
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
	axp->vs &= axp->mmask;
	axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(
struct ax25_cb *axp,
enum lapb_cmdrsp cmdrsp,
int cmd
){
	int ctlx = -1;

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
	if((ftype(cmd & 0xff) & 0x3) == S){     /* Insert V(R) if S frame */
		if(axp->mmask == EMMASK){
			/* Modulo-128 splits an S frame over two octets: the type
			 * stays in the first, N(R) and the poll/final bit move to
			 * the second.
			 */
			ctlx = ((cmd & PF) ? PF_EAX25 : 0) | (axp->vr << 1);
			cmd &= 0x0f;
		} else
			cmd |= (axp->vr << 5);
	} else if(axp->mmask == EMMASK && (cmd & ~PF) == SABM){
		/* This is where the decision to speak modulo-128 leaves the
		 * node: est_link() always asks for a SABM, and whoever set
		 * mmask beforehand turns it into a SABME.  A U frame keeps its
		 * single octet in both moduli, so nothing else changes - and
		 * that is precisely why a SABME is readable by a peer that has
		 * agreed to nothing yet.
		 */
		cmd = SABME | (cmd & PF);
	}
	return sendframe(axp,cmdrsp,cmd,ctlx,NULL);
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(struct ax25_cb *axp)
{
	struct mbuf *bp;
	struct mbuf *tbp;
	int control;            /* int, not char: modulo-128 puts V(S) up to 127
				 * in bits 1..7, and 127<<1 does not fit a signed
				 * char.  The old value never exceeded 0xEE and
				 * survived by the mask below happening to cut the
				 * sign extension off again. */
	int sent = 0;
	int i;

	if(axp == NULL
	 || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED)
	 || axp->flags.remotebusy)
		return 0;

	/* A DAMA slave sends nothing of its own accord.  Nothing is lost by
	 * refusing here: the frames stay on axp->txq, and lapb_input() calls
	 * this again at the end of every received frame - so the next poll
	 * takes them out.  That call was already there for piggybacking an
	 * ack, which is why the slave needs no queue of its own.
	 */
	if(dama_holds(axp)){
		if(axp->txq != NULL)
			dama_wait(axp);
		return 0;
	}

	/* Polled, and T1 has meanwhile said that what we sent last time was
	 * never acknowledged.  In ordinary AX.25 T1 would have resent it long
	 * ago; under DAMA it may not transmit at all, so the retransmission
	 * happens here instead - in the window a poll opened, which is the
	 * only place a slave is allowed to put anything on the air.
	 *
	 * Without this a DAMA slave never retransmits anything.  On a clean
	 * channel that is invisible; measured on one with 30% loss, the node
	 * answered every poll with a bare RR and its data never moved again.
	 */
	if(axp->dama_rex){
		axp->dama_rex = 0;
		if(axp->unack)
			inv_rex(axp);
	}

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
		int controlx = -1;

		if(axp->mmask == EMMASK){
			control = I | (axp->vs++ << 1);
			controlx = (axp->vr << 1);      /* no poll on this one */
		} else
			control = I | (axp->vs++ << 1) | (axp->vr << 5);
		axp->vs &= axp->mmask;
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp == NULL)
			return sent;    /* Probably out of memory */
		sendframe(axp,LAPB_COMMAND,control,controlx,&tbp);
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
			axp->rtt_seq = (control >> 1) & axp->mmask;
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
int ctlx,                       /* Second control octet, or -1 for none */
struct mbuf **bpp
){
	struct iface *ifp;
	struct mbuf *bp;

	if(bpp == NULL){
		bp = NULL;
		bpp = &bp;
	}
	/* Pushed first so that it ends up behind ctl, which is pushed next */
	if(ctlx != -1){
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = ctlx;
	}
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;
	if ((ctl & 3) != U)
		stop_timer(&axp->t2);
	axp->hdr.cmdrsp = cmdrsp;
	dama_mark(axp);
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
	/* Auf einem Master-Port ist eine neue Verbindung der Anlass, die Runde
	 * anzustossen: vorher gibt es niemanden zu pollen, und sie ruht.
	 */
	if(s == LAPB_CONNECTED && oldstate != LAPB_CONNECTED)
		dama_master_kick(axp->iface);
	if(s == LAPB_DISCONNECTED){
		/* The link is over, so what was agreed for it is over too.  A
		 * control block can be reused for the next connection to the
		 * same station, and that one has to earn its DAMA discipline
		 * again rather than inherit it.
		 */
		axp->dama_link = 0;
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
	if(ns != ((axp->vr - 1) & axp->mmask) && rp->bp == NULL){
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
		axp->vr = (axp->vr + 1) & axp->mmask;
		axp->flags.rejsent = NO;
	}
	rejcond = 0;
	for(tmp = (axp->vr + 1) & axp->mmask;tmp != old_vr;tmp = (tmp + 1) & axp->mmask)
		if(axp->reseq[tmp].bp != NULL){
			rejcond = 1;
			break;
		}

	if(rejcond && !axp->flags.rejsent){
		axp->flags.rejsent = YES;
		sendctl(axp,LAPB_RESPONSE,REJ|pf);
	} else {
		if(poll){
			/* Auch hier: als Master nicht in einen fremden Zug
			 * hinein.  Der REJ-Zweig darueber bleibt sofort - ein
			 * REJ fordert eine Wiederholung an, und ihn
			 * zurueckzuhalten wuerde den Link anhalten, waehrend
			 * flags.rejsent schon gesetzt ist.
			 */
			if(dama_master_holds(axp)){
				dama_master_owe(axp);
			} else {
				tmp = busy(axp) ? RNR : RR;
				sendctl(axp,LAPB_RESPONSE,tmp|pf);
			}
		} else
			start_timer(&axp->t2);
	}

	while((bp = axp->reseq[old_vr].bp) != NULL){
		axp->reseq[old_vr].bp = NULL;
		old_vr = (old_vr + 1) & axp->mmask;
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

	/* Does this protocol get in on this port?  Ahead of everything, as in
	 * the UI path - see pidfilter.c.  A segmented datagram is reassembled
	 * first and only then dropped: the true protocol id is inside, and the
	 * segments carry PID_SEGMENT and say nothing about what they are.
	 */
	if(pid_blocked(axp->iface,PF_IN,pid)){
		free_p(bpp);
		return;
	}

	/* A consumer for this protocol id first: one that is already attached,
	 * or one the configuration says to start.  Only then the node's own
	 * protocols, so that a service configured for a callsign takes what
	 * arrives there without taking the protocol away from the node.
	 */
	if((sp = find_axservice(axp,pid)) == NULL){
		/* PLAIN TEXT ON A LINK WE OPENED FOR SOMETHING ELSE.  We
		 * called this station to speak FlexNet, NET/ROM or IP, and it
		 * greets us in text the way an XNET does.  Starting a login on
		 * that is wrong twice over: the greeting would talk to a shell
		 * that answers it with command errors until the far end gives
		 * up, and where a listener is configured but nobody answers,
		 * axserv_start() sends "*** ... is not answering" into the
		 * link and disconnects - taking the interlink with it.
		 *
		 * Every other protocol id rides on regardless, in both
		 * directions: carrying several at once is what the pid is for.
		 * Text is the exception because it is not a protocol - it has
		 * no frame in which "not for you" could be said.
		 */
		if(pid == PID_NO_L3 && axp->openpid != 0
		   && axp->openpid != PID_NO_L3){
			free_p(bpp);
			return;
		}
		sp = axserv_start(axp,pid);
	}
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

	/* The delayed acknowledgement, and for a DAMA slave it stays delayed:
	 * the ack rides out on the answer to the next poll instead.  This is
	 * the same shape as the kernel's AX25_COND_ACK_PENDING - resequence()
	 * already splits "polled, answer now" from "not polled, start T2", so
	 * all that was missing was for T2 to hold its tongue.
	 */
	if (dama_holds(axp)) {
		dama_wait(axp);
		return;
	}

	if (!axp->flags.rejsent) {
		for (i = 0; i <= EMMASK; i++) {
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

	/* The idle disconnect.  A DAMA slave may not send the DISC unbidden,
	 * so try again later rather than dropping it: the link is idle by
	 * definition, and one more idle period costs nothing.
	 */
	if(dama_holds(axp)){
		start_timer(&axp->t5);
		return;
	}

	if(axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY){
		free_q(&axp->txq);
		axp->retries = 0;
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		stop_timer(&axp->t3);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
	}
}

/* WHICH OF OUR CALLSIGNS, and over which port.  This is the first half of
 * build_path(), and it stands on its own because a link is named by BOTH
 * addresses: until axroute() has stamped the source, one half of the name
 * does not exist yet, so open_ax25() cannot even look the link up.  It works
 * on the caller's header and touches no control block.
 */
void
ax25_resolve_path(
struct ax25 *hdr,
struct iface **ifpp,
const struct ax25_opts *opts)
{
	uint8 wanted[AXALEN];
	struct iface *ifp;

	/* axroute() picks the interface and stamps its callsign over the
	 * source.  Where the caller has chosen either of those, put its choice
	 * back afterwards rather than teaching the router about it - the
	 * routing itself is unchanged.
	 */
	addrcp(wanted, hdr->source);
	axroute(hdr,&ifp);
	if (opts) {
		if (opts->iface) {
			ifp = opts->iface;
			/* axroute() stamps the source with the callsign of the
			 * interface it chose; a caller who named a port must
			 * get that port's callsign instead, not the node's.
			 */
			if (!opts->ownsource && ifp->hwaddr)
				addrcp(hdr->source, ifp->hwaddr);
		}
		if (opts->ownsource)
			addrcp(hdr->source, wanted);
	}
	*ifpp = ifp;
}

/* What follows from the path once it is settled, and the same for a link we
 * open and one we answer.
 */
static void
path_timing(struct ax25_cb *axp)
{
	axp->srt = 0;
	axp->mdev = (T1init * (1 + 2 * (axp->hdr.ndigis - axp->hdr.nextdigi)) + 2) / 4;
	set_timer(&axp->t1, 4 * axp->mdev);
	ax25_apply_iface_limits(axp);
}

/* Take a path that has ALREADY been resolved.  Not the same as calling
 * build_path() a second time: axroute() inserts digipeaters and stamps our
 * callsign over the source, so running it twice on one header is not running
 * it once.
 */
/* The called side of a link that never leaves the node.  This is the SABM
 * branch of lapb_input() with everything left out that only makes sense on a
 * channel: no UA to send, no timers to start, no sequence numbers to reset,
 * because nothing here can be lost or arrive twice.  What remains is the part
 * that matters - the state change, so the consumers hear it, and the same
 * question the incoming path asks: is a service configured for the callsign
 * that was called, and does it want to greet before a byte arrives?
 */

void lapb_loop_up(struct ax25_cb *axp)
{
	struct ax_route *axr;

	lapbstate(axp,LAPB_CONNECTED);
	if(axp->services != NULL)
		return;
	/* hdr.source is the callsign that was called - the header of the
	 * called side is already turned round, as after build_path(reverse).
	 */
	axr = ax_routeptr(axp->hdr.dest,0);
	if(axr && axr->jumpstart)
		axserv_start(axp,PID_NO_L3);
	axserv_connected(axp);
}

/*---------------------------------------------------------------------------*/

/* What the caller sends is what the called side receives, and handleit() is
 * where a received frame goes anyway - so the consumers, the listeners and
 * the handover to a client see no difference at all.
 *
 * No segmenting: paclen bounds what fits in a frame on the air, and there is
 * no frame and no air here.  With pid == -1 the protocol id is already on the
 * front of each buffer in the chain, which is how NET/ROM and a relayed leg
 * send, so it is read back off there and each one is delivered on its own.
 */

/* Hand ONE frame over on the next turn of the event loop, then let go of
 * everything.
 *
 * Delivering from inside send_ax25() looked obvious and was wrong: the first
 * frame is what starts the listener on the other side, a listener may refuse
 * - "is not answering" - and the refusal disconnects, which takes this side
 * down with it: control block, service and, for a console session, the
 * session itself.  send_ax25() would then return into a caller that goes on
 * using exactly those.  axclient_parse() does:
 *
 *      send_ax25(Current->cb.ax25, &bp, PID_NO_L3);
 *      if (Current->record) ...              <- Current is gone by now
 *
 * Nobody calling send_ax25() expects sending to tear the link down under
 * them, and a real link never does - what is sent goes on a queue and leaves
 * later.  So this one queues too.
 *
 * One frame per turn, and the timer for the next is started BEFORE the
 * delivery: after handleit() neither block may be touched again, because
 * either of them may be gone.  A block that dies takes its timers with it
 * (del_ax25 stops them), so the one just started dies with it.
 */

static void loop_flush_soon(struct ax25_cb *axp);

static void loop_flush(void *p)
{
	struct ax25_cb *axp = (struct ax25_cb *) p;
	struct ax25_cb *peer = axp->loop;
	struct axservice *sp;
	struct axservice *spnext;
	struct mbuf *bp;
	int pid;
	int room;

	if(peer == NULL){
		free_q(&axp->txq);
		return;
	}
	if((bp = dequeue(&axp->txq)) == NULL)
		return;
	if(axp->txq != NULL)
		loop_flush_soon(axp);
	if((pid = PULLCHAR(&bp)) == -1){
		free_p(&bp);
		return;
	}
	handleit(peer,pid,&bp);

	/* AND NOW SAY THERE IS ROOM AGAIN, which is what the far end's
	 * acknowledgement does on a link that goes out over the air.  Without
	 * it a writer that asks space_ax25() first stops for good: the window
	 * is one frame, so as soon as one sits in the queue there is no room,
	 * and nothing would ever tell it otherwise.  That is a session that
	 * greets and then falls silent.
	 *
	 * After the delivery, because that may have taken the whole link down
	 * - so ask whether this block is still there at all.
	 */
	if(!ax25_alive(axp))
		return;
	/* Everything handed over, and somebody asked to disconnect while it
	 * was still in hand - now it may happen.  disc_ax25() set the flag and
	 * left rather than throw the rest away.
	 */
	if(axp->txq == NULL && axp->flags.closed){
		disc_ax25(axp);
		return;
	}
	room = space_ax25(axp);
	if(room <= 0)
		return;
	for(sp = axp->services;sp != NULL;sp = spnext){
		spnext = sp->next;
		if(sp->t_upcall != NULL)
			(*sp->t_upcall)(sp,room);
	}
}

static void loop_flush_soon(struct ax25_cb *axp)
{
	axp->loop_timer.func = loop_flush;
	axp->loop_timer.arg = axp;
	set_timer(&axp->loop_timer,1);
	start_timer(&axp->loop_timer);
}

int lapb_loop_send(struct ax25_cb *axp,struct mbuf **bpp,int pid)
{
	int len;
	struct mbuf *bp;
	struct mbuf *next;

	if(axp->loop == NULL || bpp == NULL || *bpp == NULL){
		free_p(bpp);
		return -1;
	}
	len = (int) len_p(*bpp);
	/* The protocol id goes on the front, the way it does in a frame on the
	 * air, so the queue holds one kind of thing and the delivery reads it
	 * back off.  With pid == -1 it is already there.
	 */
	if(pid != -1){
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = (uint8) pid;
		enqueue(&axp->txq,bpp);
	} else {
		for(bp = *bpp;bp != NULL;bp = next){
			struct mbuf *one = bp;

			next = bp->anext;
			one->anext = NULL;
			enqueue(&axp->txq,&one);
		}
		*bpp = NULL;
	}
	loop_flush_soon(axp);
	return len;
}

/*---------------------------------------------------------------------------*/

void
ax25_adopt_path(
struct ax25_cb *axp,
struct iface *ifp,
const struct ax25 *hdr)
{
	axp->routing_changes++;
	axp->iface = ifp;
	axp->hdr = *hdr;
	path_timing(axp);
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
		struct iface *chosen;

		ax25_resolve_path(hdr,&chosen,opts);
		axp->iface = chosen;
		axp->hdr = *hdr;
	}
	path_timing(axp);
}

/* The window this link may grow to.  Per port if the port says so, else the
 * node's setting, and which of the two numbers applies is decided by the
 * modulus the link agreed on.
 */

int ax25_maxframe(struct ax25_cb *axp)
{
	struct iface *ifp = axp->iface;

	if(axp->mmask == EMMASK)
		return (ifp && ifp->emaxframe) ? ifp->emaxframe : EMaxframe;
	return (ifp && ifp->maxframe) ? ifp->maxframe : Maxframe;
}

/* Take the packet length from the port if it has one, and then let the
 * driver's hard limit override both.  Without that last step "ax25 paclen
 * 1024" silences a 6pack port - sixpack_encode() drops what it cannot hold -
 * while every other port keeps working, which is the worst way to find out.
 */

void ax25_apply_iface_limits(struct ax25_cb *axp)
{
	struct iface *ifp = axp->iface;
	int room;

	axp->paclen = (ifp && ifp->paclen) ? ifp->paclen : Paclen;
	if(ifp && ifp->framemax){
		room = ifp->framemax
		     - (2 + axp->hdr.ndigis) * AXALEN   /* addresses */
		     - 2                                /* control, modulo-128 */
		     - 1                                /* PID */
		     - 2;                               /* FCS */
		if(room > 0 && axp->paclen > room)
			axp->paclen = room;
	}
}

