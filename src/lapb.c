/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapb.c,v 1.25 1993/01/29 06:48:27 deyke Exp $ */

/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "ip.h"
#include "netrom.h"

extern struct ax25_cb *Netrom_server_axcb;

struct ax25_cb *Axcb_server;    /* Server control block */

static int ackours __ARGS((struct ax25_cb *axp, int n));
static void reset_t1 __ARGS((struct ax25_cb *axp));
static int put_reseq __ARGS((struct ax25_cb *axp, struct mbuf *bp, int ns));

#define next_seq(n)  (((n) + 1) & MMASK)

/* Process incoming frames */
int
lapb_input(iface,hdr,bp)
struct iface *iface;
struct ax25 *hdr;
struct mbuf *bp;                /* Rest of frame, starting with ctl */
{

  int cmdrsp;
  int control;
  int digipeat;
  int nr;
  int ns;
  int pid;
  int type;
  struct ax25_cb *axp;
  struct ax25_cb *axpp;

  if ((control = PULLCHAR(&bp)) == -1) return (-1);
  type = ftype(control);
  digipeat = (ismyax25addr(hdr->dest) == NULLIF);

  if (bp) {
    pid = uchar(*bp->data);
    if (pid == PID_FLEXNET) is_flexnet(hdr->source, 1);
  } else
    pid = -1;

  switch (hdr->cmdrsp) {
  case LAPB_COMMAND:
    cmdrsp = DST_C | (control & PF);
    break;
  case LAPB_RESPONSE:
    cmdrsp = SRC_C | (control & PF);
    break;
  default:
    cmdrsp = VERS1;
    break;
  }

  if (digipeat) {
    struct ax25_cb *axlast = NULLAX25;
    for (axp = Ax25_cb; axp; axlast = axp, axp = axp->next)
      if (addreq(hdr->source, axp->hdr.dest) && addreq(hdr->dest, axp->hdr.source)) {
	if (axlast != NULLAX25) {
	  axlast->next = axp->next;
	  axp->next = Ax25_cb;
	  Ax25_cb = axp;
	}
	break;
      }
  } else {
    axp = find_ax25(hdr->source);
  }

  if (!axp) {
    if (!digipeat && Netrom_server_axcb && isnetrom(hdr->source))
      axp = cr_ax25(Netrom_server_axcb);
    else if (!digipeat && Axcb_server)
      axp = cr_ax25(Axcb_server);
    else
      axp = cr_ax25(NULLAX25);
    build_path(axp, iface, hdr, 1);
    if (digipeat) {
      axp->peer = axpp = cr_ax25(NULLAX25);
      axpp->peer = axp;
      build_path(axpp, NULLIF, hdr, 0);
    } else
      axpp = NULLAX25;
  } else
    axpp = axp->peer;

  if (type == SABM) {
    int i;
    if (axp->unack)
      start_timer(&axp->t1);
    else
      stop_timer(&axp->t1);
    stop_timer(&axp->t2);
    stop_timer(&axp->t4);
    axp->flags.polling = 0;
    axp->flags.rnrsent = 0;
    axp->flags.rejsent = 0;
    axp->flags.remotebusy = 0;
    axp->vr = 0;
    axp->vs = axp->unack;
    axp->retries = 0;
    for (i = 0; i < 8; i++)
      if (axp->reseq[i].bp) {
	free_p(axp->reseq[i].bp);
	axp->reseq[i].bp = 0;
      }
  }

  if (axp->mode == STREAM && type == I && pid != PID_NO_L3) {
    axp->mode = DGRAM;
    if (axpp) axpp->mode = DGRAM;
  }

  set_timer(&axp->t3, T3init);
  start_timer(&axp->t3);

  switch (axp->state) {

  case LAPB_DISCONNECTED:
    if (!digipeat) {
      if (type == SABM && cmdrsp != VERS1 && axp->r_upcall) {
	send_packet(axp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	lapbstate(axp, LAPB_CONNECTED);
      } else {
	if (cmdrsp != RESP && cmdrsp != FINAL)
	  send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	del_ax25(axp);
      }
    } else {
      if (type == SABM && cmdrsp != VERS1 && axpp->state == LAPB_DISCONNECTED) {
	lapbstate(axpp, LAPB_SETUP);
      } else if (type == SABM && cmdrsp != VERS1 && axpp->state == LAPB_SETUP) {
	if (axpp->routing_changes < 3) {
	  build_path(axpp, NULLIF, hdr, 0);
	  send_packet(axpp, SABM, POLL, NULLBUF);
	}
      } else {
	if (cmdrsp != RESP && cmdrsp != FINAL)
	  send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	if (axpp->state == LAPB_DISCONNECTED) {
	  del_ax25(axpp);
	  del_ax25(axp);
	}
      }
    }
    break;

  case LAPB_SETUP:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
      break;
    case SABM:
      if (cmdrsp != VERS1) {
	send_packet(axp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	lapbstate(axp, LAPB_CONNECTED);
      }
      break;
    case UA:
      if (cmdrsp != VERS1) {
	lapbstate(axp, LAPB_CONNECTED);
      } else {
	if (axpp && axpp->state == LAPB_DISCONNECTED)
	  send_packet(axpp, DM, FINAL, NULLBUF);
	axp->reason = LB_DM;
	lapbstate(axp, LAPB_DISCPENDING);
      }
      break;
    case DISC:
      send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
    case FRMR:
      if (axpp && axpp->state == LAPB_DISCONNECTED)
	send_packet(axpp, DM, FINAL, NULLBUF);
      axp->reason = LB_DM;
      lapbstate(axp, LAPB_DISCONNECTED);
      break;
    }
    break;

  case LAPB_CONNECTED:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
      nr = control >> 5;
      ackours(axp, nr);
      if (axpp && axpp->flags.rnrsent && !busy(axpp)) send_ack(axpp, RESP);
      if (type == I) {
	if (!digipeat &&
	    pid == PID_NETROM &&
	    axp->r_upcall != Netrom_server_axcb->r_upcall) {
	  new_neighbor(hdr->source);
	  lapbstate(axp, LAPB_DISCPENDING);
	  free_p(bp);
	  return 0;
	}
	ns = (control >> 1) & MMASK;
	if (!bp) bp = alloc_mbuf(0);
	if (put_reseq(axp, bp, ns))
	  while (bp = axp->reseq[axp->vr].bp) {
	    axp->reseq[axp->vr].bp = 0;
	    axp->vr = next_seq(axp->vr);
	    axp->flags.rejsent = 0;
	    if (axp->mode == STREAM) (void) PULLCHAR(&bp);
	    if (!digipeat) {
	      axp->rcvcnt += len_p(bp);
	      if (axp->mode == STREAM)
		append(&axp->rxq, bp);
	      else
		enqueue(&axp->rxq, bp);
	    } else
	      send_ax25(axpp, bp);
	  }
	if (cmdrsp == POLL)
	  send_ack(axp, FINAL);
	else {
	  set_timer(&axp->t2, T2init);
	  start_timer(&axp->t2);
	}
	if (axp->r_upcall && axp->rcvcnt) (*axp->r_upcall)(axp, axp->rcvcnt);
      } else {
	if (cmdrsp == POLL) send_ack(axp, FINAL);
	if (axp->flags.polling && cmdrsp == FINAL) axp->retries = axp->flags.polling = 0;
	if (type == RNR) {
	  if (!axp->flags.remotebusy) axp->flags.remotebusy = msclock();
	  set_timer(&axp->t4, T4init);
	  start_timer(&axp->t4);
	  axp->cwind = 1;
	} else {
	  axp->flags.remotebusy = 0;
	  stop_timer(&axp->t4);
	  if (axp->unack && type == REJ) {
	    int old_vs;
	    struct mbuf *bp1;
	    old_vs = axp->vs;
	    axp->vs = (axp->vs - axp->unack) & MMASK;
	    axp->flags.rtt_run = 0;
	    dup_p(&bp1, axp->txq, 0, MAXINT16);
	    send_packet(axp, I, CMD, bp1);
	    axp->vs = old_vs;
	    axp->cwind = 1;
	  } else if (axp->unack && cmdrsp == FINAL) {
	    struct mbuf *bp1, *qp;
	    axp->vs = (axp->vs - axp->unack) & MMASK;
	    for (qp = axp->txq; qp; qp = qp->anext) {
	      axp->flags.rtt_run = 0;
	      dup_p(&bp1, qp, 0, MAXINT16);
	      send_packet(axp, I, CMD, bp1);
	    }
	  }
	}
      }
      lapb_output(axp, 1);
      if (axp->flags.polling || axp->unack && !axp->flags.remotebusy)
	start_timer(&axp->t1);
      if (axp->flags.closed && !axp->sndq && !axp->unack ||
	  axp->flags.remotebusy && msclock() - axp->flags.remotebusy > 900000L)
	lapbstate(axp, LAPB_DISCPENDING);
      break;
    case SABM:
      send_packet(axp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
      lapb_output(axp, 1);
      break;
    case DISC:
      send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
      lapbstate(axp, LAPB_DISCONNECTED);
      break;
    case UA:
      axp->flags.remotebusy = 0;
      stop_timer(&axp->t4);
      if (axp->unack) start_timer(&axp->t1);
      lapb_output(axp, 1);
      break;
    case FRMR:
      lapbstate(axp, LAPB_DISCPENDING);
      break;
    }
    break;

  case LAPB_DISCPENDING:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
    case SABM:
      if (cmdrsp != RESP && cmdrsp != FINAL)
	send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
      break;
    case DISC:
      send_packet(axp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
    case UA:
    case FRMR:
      lapbstate(axp, LAPB_DISCONNECTED);
      break;
    }
    break;
  }

  free_p(bp);
  return 0;
}
/* Handle incoming acknowledgements for frames we've sent.
 * Free frames being acknowledged.
 * Return -1 to cause a frame reject if number is bad, 0 otherwise
 */
static int
ackours(axp,n)
struct ax25_cb *axp;
int16 n;
{
	struct mbuf *bp;
	int acked = 0;  /* Count of frames acked by this ACK */
	int16 oldest;   /* Seq number of oldest unacked I-frame */
	int32 rtt,abserr;

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
				reset_t1(axp);
				if (axp->cwind < Maxframe) {
					axp->mdev += ((axp->srt / axp->cwind + 2) >> 2);
					axp->cwind++;
				}
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
#if 0
	if(acked != 0){
		/* If user has set a transmit upcall, indicate how many frames
		 * may be queued
		 */
		if(axp->t_upcall != NULLVFP && Maxframe > axp->unack)
			(*axp->t_upcall)(axp,Paclen * (Maxframe - axp->unack));
	}
#endif
	return 0;
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(axp, fill_sndq)
struct ax25_cb *axp;
int fill_sndq;
{

	int cnt;
	struct mbuf *bp;

	stop_timer(&axp->t5);
	while (axp->unack < axp->cwind) {
		if (axp->state != LAPB_CONNECTED || axp->flags.remotebusy)
			return;
		if (fill_sndq && axp->t_upcall) {
			cnt = space_ax25(axp);
			if (cnt > 0) {
				(*axp->t_upcall)(axp, cnt);
				if (axp->unack >= axp->cwind)
					return;
			}
		}
		if (!axp->sndq)
			return;
		if (axp->mode == STREAM) {
			cnt = len_p(axp->sndq);
			if (cnt < Paclen) {
				if (axp->unack)
					return;
				if (!axp->peer && axp->sndqtime + T5init - msclock() > 0) {
					set_timer(&axp->t5, axp->sndqtime + T5init - msclock());
					start_timer(&axp->t5);
					return;
				}
			}
			if (cnt > Paclen)
				cnt = Paclen;
			if (!(bp = alloc_mbuf(cnt)))
				return;
			pullup(&axp->sndq, bp->data, bp->cnt = cnt);
		} else {
			bp = dequeue(&axp->sndq);
		}
		enqueue(&axp->txq, bp);
		axp->unack++;
		if (!axp->flags.rtt_run) {
			/* Start round trip timer */
			axp->rtt_seq = axp->vs;
			axp->rtt_time = msclock();
			axp->flags.rtt_run = 1;
		}
		dup_p(&bp, bp, 0, MAXINT16);
		send_packet(axp, I, CMD, bp);
	}
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
  axp->flags.polling = 0;
  axp->retries = 0;
  stop_timer(&axp->t1);
  stop_timer(&axp->t2);
  stop_timer(&axp->t4);
  stop_timer(&axp->t5);
  reset_t1(axp);
  switch (s) {
  case LAPB_DISCONNECTED:
    if (axp->peer) disc_ax25(axp->peer);
    if (axp->s_upcall)
      (*axp->s_upcall)(axp, oldstate, s);
    else if (axp->peer && axp->peer->state == LAPB_DISCONNECTED) {
      del_ax25(axp->peer);
      del_ax25(axp);
    }
    break;
  case LAPB_SETUP:
    if (axp->s_upcall) (*axp->s_upcall)(axp, oldstate, s);
    send_packet(axp, SABM, POLL, NULLBUF);
    break;
  case LAPB_CONNECTED:
    if (axp->peer && axp->peer->state == LAPB_DISCONNECTED) {
      send_packet(axp->peer, UA, FINAL, NULLBUF);
      lapbstate(axp->peer, LAPB_CONNECTED);
    }
    if (axp->s_upcall) (*axp->s_upcall)(axp, oldstate, s);
    lapb_output(axp, 1);
    break;
  case LAPB_DISCPENDING:
    if (axp->peer) disc_ax25(axp->peer);
    if (axp->s_upcall) (*axp->s_upcall)(axp, oldstate, s);
    send_packet(axp, DISC, POLL, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void
reset_t1(axp)
struct ax25_cb *axp;
{
  int32 tmp;

  tmp = axp->srt + 4 * axp->mdev;
  if (tmp < 500) tmp = 500;
  set_timer(&axp->t1, tmp);
}

/*---------------------------------------------------------------------------*/

void
send_packet(axp, type, cmdrsp, data)
struct ax25_cb *axp;
int type;
int cmdrsp;
struct mbuf *data;
{

  int control;
  struct iface *ifp;
  struct mbuf *bp;

  if (axp->mode == STREAM && (type == I || type == UI)) {
    if (!(bp = pushdown(data, 1))) {
      free_p(data);
      return;
    }
    *bp->data = PID_NO_L3;
    data = bp;
  }

  control = type;
  if (type == I) {
    control |= (axp->vs << 1);
    axp->vs = next_seq(axp->vs);
  }
  if ((type & 3) != U) {
    control |= (axp->vr << 5);
    stop_timer(&axp->t2);
  }
  if (cmdrsp & PF) control |= PF;
  if (!(bp = pushdown(data, 1))) {
    free_p(data);
    return;
  }
  *bp->data = control;
  data = bp;

  if (cmdrsp & DST_C)
    axp->hdr.cmdrsp = LAPB_COMMAND;
  else if (cmdrsp & SRC_C)
    axp->hdr.cmdrsp = LAPB_RESPONSE;
  else
    axp->hdr.cmdrsp = VERS1;
  if (!(bp = htonax25(&axp->hdr, data))) {
    free_p(data);
    return;
  }
  data = bp;

  if (type == RR || type == REJ || type == UA) axp->flags.rnrsent = 0;
  if (type == RNR) axp->flags.rnrsent = 1;
  if (type == REJ) axp->flags.rejsent = 1;
  if (cmdrsp == POLL) axp->flags.polling = 1;
  if (type == I || cmdrsp == POLL) start_timer(&axp->t1);

  if (ifp = axp->iface) {
    if (ifp->forw) ifp = ifp->forw;
    (*ifp->raw)(ifp, data);
  } else
    free_p(bp);
}

/*---------------------------------------------------------------------------*/

int
busy(axp)
struct ax25_cb *axp;
{
  return axp->peer ? space_ax25(axp->peer) <= 0 : axp->rcvcnt >= Axwindow;
}

/*---------------------------------------------------------------------------*/

void
send_ack(axp, cmdrsp)
struct ax25_cb *axp;
int cmdrsp;
{
  if (busy(axp))
    send_packet(axp, RNR, cmdrsp, NULLBUF);
  else if (!axp->flags.rejsent &&
	   (axp->reseq[0].bp || axp->reseq[1].bp ||
	    axp->reseq[2].bp || axp->reseq[3].bp ||
	    axp->reseq[4].bp || axp->reseq[5].bp ||
	    axp->reseq[6].bp || axp->reseq[7].bp))
    send_packet(axp, REJ, cmdrsp, NULLBUF);
  else
    send_packet(axp, RR, cmdrsp, NULLBUF);
}

/*---------------------------------------------------------------------------*/

void
t2_timeout(axp)
struct ax25_cb *axp;
{
  send_ack(axp, RESP);
}

/*---------------------------------------------------------------------------*/

/* Send a poll (S-frame command with the poll bit set) */
void
pollthem(p)
void *p;
{
	register struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;
	if (!run_timer(&axp->t1)) send_ack(axp, POLL);
}

/*---------------------------------------------------------------------------*/

void
t4_timeout(axp)
struct ax25_cb *axp;
{
  if (!axp->flags.polling) send_ack(axp, POLL);
}

/*---------------------------------------------------------------------------*/

void
t5_timeout(axp)
struct ax25_cb *axp;
{
  lapb_output(axp, 1);
}

/*---------------------------------------------------------------------------*/

void
build_path(axp, ifp, hdr, reverse)
struct ax25_cb *axp;
struct iface *ifp;
struct ax25 *hdr;
int reverse;
{
  int i;

  axp->routing_changes++;
  if (reverse) {
    addrcp(axp->hdr.dest, hdr->source);
    addrcp(axp->hdr.source, hdr->dest);
    for (i = 0; i < hdr->ndigis; i++)
      addrcp(axp->hdr.digis[i], hdr->digis[hdr->ndigis-1-i]);
    axp->hdr.ndigis = hdr->ndigis;
    axp->hdr.nextdigi = 0;
    for (i = axp->hdr.ndigis - 1; i >= 0; i--)
      if (ismyax25addr(axp->hdr.digis[i])) {
	axp->hdr.nextdigi = i + 1;
	break;
      }
    axp->iface = ifp;
  } else {
    axroute(hdr, &axp->iface);
    axp->hdr = *hdr;
  }
  axp->srt = (T1init * (1 + 2 * (axp->hdr.ndigis - axp->hdr.nextdigi))) / 2;
  axp->mdev = axp->srt / 4;
  reset_t1(axp);
}

/*---------------------------------------------------------------------------*/

static
int put_reseq(axp, bp, ns)
struct ax25_cb *axp;
struct mbuf *bp;
int ns;
{

  char *p;
  int cnt, sum;
  struct axreseq *rp;
  struct mbuf *tp;

  if (next_seq(ns) == axp->vr) return 0;
  rp = &axp->reseq[ns];
  if (rp->bp) return 0;
  for (sum = 0, tp = bp; tp; tp = tp->next) {
    cnt = tp->cnt;
    p = tp->data;
    while (cnt--) sum += uchar(*p++);
  }
  if (ns != axp->vr && sum == rp->sum) return 0;
  rp->bp = bp;
  rp->sum = sum;
  return 1;
}

