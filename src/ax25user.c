/* @(#) $Id: ax25user.c,v 1.13 1999/02/01 22:24:25 deyke Exp $ */

/* User interface subroutines for AX.25
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "lapb.h"
#include <stdlib.h>

#include "netuser.h"
#include "ax25.h"
#include "lapb.h"
#include "pidfilter.h"

/* Bring the called side up on the next turn of the event loop rather than
 * from inside the caller's own call - see the note at the use.  One
 * millisecond, which is the shortest a timer can say "not now".
 */

static void loop_up_timeout(void *p)
{
	struct ax25_cb *peer = (struct ax25_cb *) p;    /* the called side */
	struct ax25_cb *caller = peer->loop;

	if(caller == NULL || caller->state != LAPB_DISCONNECTED)
		return;
	/* The caller first, so its own service hears "connected" before
	 * anything arrives.  THIS UPCALL IS WHY BOTH HALVES WAIT FOR THE
	 * TIMER: on the service socket it is where a "handover" gives the
	 * connection away, and it may close it again on the spot - all of it
	 * inside the caller's open_axservice(), which had not even returned
	 * yet, so connect_command() went on to use what was already gone.
	 */
	lapbstate(caller,LAPB_CONNECTED);
	/* And it may have taken us with it, so ask before touching either. */
	if(!ax25_alive(peer) || peer->loop == NULL)
		return;
	lapb_loop_up(peer);
}

static void peer_up_soon(struct ax25_cb *peer)
{
	peer->t1.func = loop_up_timeout;
	peer->t1.arg = peer;
	set_timer(&peer->t1,1);
	start_timer(&peer->t1);
}

/*---------------------------------------------------------------------------*/

/* Both ends of a link that stays in the node.  The caller gets the block it
 * asked for; the second one is the called side, with the addresses the other
 * way round, and the two point at each other.
 *
 * NEITHER IS BROUGHT UP HERE, and that is deliberate: whatever is listening
 * on the called side may greet as soon as its link stands - a mailbox always
 * does - and there would be nobody on this side to hear it yet.  The caller
 * opens its service next, and open_axservice() starts the pair then.
 */

static struct ax25_cb *open_ax25_loop(struct ax25 *hdr)
{
	struct ax25_cb *axp;
	struct ax25_cb *peer;

	if((axp = find_ax25(hdr->source,hdr->dest)) != NULL){
		Net_error = CON_EXISTS;
		return NULL;
	}
	if((axp = cr_ax25(hdr->dest)) == NULL){
		Net_error = NO_MEM;
		return NULL;
	}
	if((peer = cr_ax25(hdr->source)) == NULL){
		del_ax25(axp);
		Net_error = NO_MEM;
		return NULL;
	}
	axp->hdr = *hdr;
	peer->hdr = *hdr;
	addrcp(peer->hdr.dest,hdr->source);
	addrcp(peer->hdr.source,hdr->dest);
	peer->hdr.ndigis = 0;
	peer->hdr.nextdigi = 0;
	axp->loop = peer;
	peer->loop = axp;
	/* Room for more than one frame at a time.  The window of one is what a
	 * link starts with until the other end agrees to more, and here there
	 * is no other end to ask - every frame arrives, in order, at once.
	 * With one, a writer that asks space_ax25() first would go a frame at
	 * a time through the timer, which is slow for no reason.
	 */
	axp->maxframe = peer->maxframe = 7;
	return axp;
}

/*---------------------------------------------------------------------------*/

/* Open an AX.25 connection */
struct ax25_cb *
open_ax25(
struct ax25 *hdr,
int mode,               /* active/passive/server */
const struct ax25_opts *opts    /* per-connection choices, 0 for the usual */
){
	struct ax25_cb *axp;
	struct iface *ifp;

	/* THE PATH FIRST, THE SEARCH AFTER IT, and that order is the whole
	 * point.  A link is named by both addresses, and OURS is not settled
	 * until axroute() has stamped it into the source - so before this
	 * runs, there is nothing to search for.  Asking after the destination
	 * alone is what refused a second connect to a node we already had a
	 * link to under another callsign: the FlexNet partner DK0CCC-10 ->
	 * DB0FHN made DL9SAU-5 -> DB0FHN "busy", though the two share nothing
	 * but the far end.
	 */
	/* A CALLSIGN WE ANSWER TO OURSELVES: the session has no business on the
	 * air.  Sending a SABM for our own callsign only works where something
	 * out there routes it back - db0fhn-12 reached through the xnet next
	 * door and came home as an incoming call - and where nothing does, the
	 * caller waits for an answer that cannot come.  Which of the two you
	 * got depended on whether a route happened to exist, so the same
	 * connect said "no route to host" on one port and nothing at all on
	 * another.
	 *
	 * A path given by hand is left alone: "connect db0fhn-8 via db0fhn" is
	 * somebody asking for the long way round on purpose, and that is a
	 * legitimate way to test the real one.
	 */
	if(hdr->ndigis == 0 && mode == AX_ACTIVE
	   && (ismyax25addr(hdr->dest) != NULL || axlisten_active(hdr->dest)))
		return open_ax25_loop(hdr);

	ax25_resolve_path(hdr,&ifp,opts);
	axp = find_ax25(hdr->source,hdr->dest);
	if(axp != NULL && axp->services != NULL){
		/* Only one to a customer.  Say which refusal this is: a caller
		 * that hears "busy" can try again under another SSID, one that
		 * hears "invalid" can only give up - and until now it heard
		 * whatever the last operation had left behind.
		 */
		Net_error = CON_EXISTS;
		return NULL;
	}
	if(axp == NULL){
		/* No port to send on: axroute() knew no route and the caller
		 * named none.  Without this the link sits in SETUP forever
		 * with no interface - no SABM ever leaves, so nothing times
		 * out either, and the caller waits for an answer that cannot
		 * come.  A refusal with a reason is the whole point.
		 */
		if(ifp == NULL){
			Net_error = NO_ROUTE;
			return NULL;
		}
		if((axp = cr_ax25(hdr->dest)) == NULL){
			Net_error = NO_MEM;
			return NULL;
		}
		ax25_adopt_path(axp,ifp,hdr);
	}

	switch(mode){
	case AX_SERVER:
		axp->flags.clone = 1;
	case AX_PASSIVE:        /* Note fall-thru */
		axp->state = LAPB_LISTEN;
		return axp;
	case AX_ACTIVE:
		break;
	}
	switch(axp->state){
	case LAPB_DISCONNECTED:
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
		break;
	default:
		break;
	}
	return axp;
}

/* Send data on an AX.25 connection. Caller provides optional PID. If
 * a PID is provided, then operate in stream mode, i.e., a large packet
 * is automatically packetized into a series of paclen-sized data fields.
 *
 * If pid == -1, it is assumed the packet (which may actually be a queue
 * of distinct packets) already has a PID on the front and it is passed
 * through directly even if it is very large.
 */
int
send_ax25(
struct ax25_cb *axp,
struct mbuf **bpp,
int pid
){
	struct mbuf *bp1;
	uint offset,len,size;

	if(axp == NULL || bpp == NULL || *bpp == NULL || axp->flags.closed){
		free_p(bpp);
		return -1;
	}
	/* Nothing below this is about a link that never leaves the node: no
	 * segmenting, no queue, no window - what is written here is received
	 * on the other side, and that is the whole of it.
	 */
	if(axp->loop != NULL)
		return lapb_loop_send(axp,bpp,pid);
	if(pid != -1){
		/* Ahead of the segmenter on purpose: what leaves here as
		 * PID_SEGMENT says nothing about what is inside, so this is the
		 * last place where the protocol is still known by name.
		 */
		if(pid_blocked(axp->iface,PF_OUT,pid)){
			free_p(bpp);
			return -1;
		}
		offset = 0;
		len = len_p(*bpp);
		/* It is important that all the pushdowns be done before
		 * any part of the original packet is freed.
		 * Otherwise the pushdown might erroneously overwrite
		 * a part of the packet that had been duped and freed.
		 */
		while(len != 0){
			size = min(len,axp->paclen);
			dup_p(&bp1,*bpp,offset,size);
			len -= size;
			offset += size;
			pushdown(&bp1,NULL,1);
			bp1->data[0] = pid;
			enqueue(&axp->txq,&bp1);
		}
		free_p(bpp);
	} else if(axp->iface != NULL && axp->iface->pidblocked[PF_OUT]){
		/* The protocol id is already on the front of each frame here -
		 * NET/ROM pushes it on itself, and so does a relayed leg - so
		 * the gate has to read it out of the data instead of taking it
		 * as an argument.  Frame by frame, because this may be a chain
		 * and the frames in it need not carry the same protocol.
		 */
		struct mbuf *bp;
		struct mbuf *next;

		for(bp = *bpp;bp != NULL;bp = next){
			next = bp->anext;
			bp->anext = NULL;
			if(bp->cnt > 0 && pid_blocked(axp->iface,PF_OUT,bp->data[0]))
				free_p(&bp);
			else
				enqueue(&axp->txq,&bp);
		}
		*bpp = NULL;
	} else {
		enqueue(&axp->txq,bpp);
	}
	return lapb_output(axp);
}

/*---------------------------------------------------------------------------*/

/* Consumers.  One per protocol id on a link, each with a receive queue of its
 * own - see the note in lapb.h for why they cannot share one.
 */

struct axservice *
find_axservice(
struct ax25_cb *axp,
int pid
){
	struct axservice *sp;

	for(sp = axp->services; sp != NULL; sp = sp->next)
		if(sp->pid == pid)
			return sp;
	return NULL;
}

struct axservice *
open_axservice(
struct ax25_cb *axp,
int pid,
void (*r_upcall)(struct axservice *,int),
void (*t_upcall)(struct axservice *,int),
void (*s_upcall)(struct axservice *,enum lapb_state,enum lapb_state),
void *user
){
	struct axservice *sp;

	if((sp = find_axservice(axp,pid)) != NULL)
		return sp;
	if((sp = (struct axservice *)calloc(1,sizeof(struct axservice))) == NULL)
		return NULL;
	sp->axp = axp;
	sp->pid = pid;
	sp->r_upcall = r_upcall;
	sp->t_upcall = t_upcall;
	sp->s_upcall = s_upcall;
	sp->user = user;
	sp->next = axp->services;
	axp->services = sp;
	/* The caller of a link that stays in the node now has somewhere to be
	 * answered, so the pair can come up: this side first, so its own
	 * service hears the state change, and then the called side, where a
	 * listener may greet at once.  The test says "caller": the called side
	 * gets its service from that very start, and by then this one has one.
	 */
	if(axp->loop != NULL && axp->state == LAPB_DISCONNECTED
	   && axp->loop->state == LAPB_DISCONNECTED
	   && axp->loop->services == NULL){
		/* BOTH HALVES COME UP AFTER WE HAVE RETURNED, and that is not
		 * tidiness but necessity.  Neither state change may happen
		 * inside this call:
		 *
		 * - "connected" on this side reaches our caller's own upcall,
		 *   and on the service socket that is where a "handover" hands
		 *   the connection to a program and may close it again at once
		 *   - while connect_command() has not yet been given what it
		 *   is about to use.
		 * - the far side may refuse: a "client" listener with nobody
		 *   holding the callsign says "is not answering" and hangs up,
		 *   taking this half with it - block, service and session.
		 *
		 * Both cost a segfault on db0fhn before they were understood.
		 * The far side's timer is free for it: a link that never leaves
		 * the node has nothing to retransmit.
		 */
		peer_up_soon(axp->loop);
	}
	return sp;
}

/* Take from this consumer's queue.  cnt == 0 means all of it, as before.
 * Whether the link is still busy is a question about every queue, so it is
 * asked after the change, not decided here.
 */

struct mbuf *
recv_axservice(
struct axservice *sp,
uint cnt
){
	struct mbuf *bp;

	if(sp->rxq == NULL)
		return NULL;
	if(cnt == 0){
		/* Everything there is, as one chain: the queue holds frames
		 * now, so flatten it rather than hand out a chain whose
		 * anext still points at the rest.  A caller that asks this
		 * way wants a byte stream and has said so.
		 */
		struct mbuf *next;

		bp = sp->rxq;
		sp->rxq = NULL;
		for(next = bp->anext, bp->anext = NULL; next != NULL;){
			struct mbuf *this = next;

			next = this->anext;
			this->anext = NULL;
			append(&bp,&this);
		}
	} else {
		/* Bytes, across as many frames as it takes.  pullup() empties
		 * one frame at a time, so walk on while there is room left.
		 */
		uint got = 0;

		bp = ambufw(cnt);
		while(got < cnt && sp->rxq != NULL){
			struct mbuf *this = dequeue(&sp->rxq);
			uint took = pullup(&this,bp->data + got,cnt - got);

			got += took;
			if(this != NULL){       /* frame not exhausted */
				this->anext = sp->rxq;
				sp->rxq = this;
				break;
			}
		}
		bp->cnt = got;
	}
	/* If this has un-busied the link, reopen the window */
	if(sp->axp->flags.rnrsent && !busy(sp->axp))
		sendctl(sp->axp,LAPB_RESPONSE,RR);
	return bp;
}

/* One frame, whole, or nothing.  What SOCK_SEQPACKET promises the consumer
 * and what recv_axservice() above cannot give it.
 */

struct mbuf *
recv_axservice_packet(struct axservice *sp)
{
	struct mbuf *bp;

	if((bp = dequeue(&sp->rxq)) == NULL)
		return NULL;
	if(sp->axp->flags.rnrsent && !busy(sp->axp))
		sendctl(sp->axp,LAPB_RESPONSE,RR);
	return bp;
}

/* What all consumers together still hold - what the one queue used to show */

int
axservice_pending(struct ax25_cb *axp)
{
	int held = 0;
	struct axservice *sp;

	for(sp = axp->services; sp != NULL; sp = sp->next)
		held += (int) len_qbytes(sp->rxq);
	return held;
}

int
space_axservice(struct axservice *sp)
{
	return space_ax25(sp->axp);
}

int
send_axservice(
struct axservice *sp,
struct mbuf **bpp
){
	return send_ax25(sp->axp,bpp,sp->pid);
}

/* Detach.  When the last consumer goes the link goes with it - asked by
 * walking the list, not counted: a number that some path forgets to decrement
 * keeps a link alive forever, and one decremented twice tears down a live
 * one.
 */

void
close_axservice(struct axservice *sp)
{
	struct ax25_cb *axp = sp->axp;
	struct axservice **pp;

	for(pp = &axp->services; *pp != NULL; pp = &(*pp)->next)
		if(*pp == sp){
			*pp = sp->next;
			break;
		}
	free_q(&sp->rxq);
	free(sp);
	if(axp->services != NULL)
		return;
	/* THE LAST CONSUMER IS GONE.  On a link that still stands, that is a
	 * reason to take it down, and disc_ax25() does it.  On one that is
	 * ALREADY down it is the moment the block becomes rubbish: nothing is
	 * left to answer a late frame for, and disc_ax25() would do nothing
	 * ("Ignored" below), so nobody would ever delete it.
	 *
	 * That is where the "Disconn" lines came from that outlive every timer
	 * and stand until the node is restarted: a connect that ran into its
	 * timeout reached LAPB_DISCONNECTED while the caller's service was
	 * still attached, so lapbstate() left the block - and when the caller
	 * then gave up, this was the path that had nothing more to say.
	 */
	if(axp->state == LAPB_DISCONNECTED)
		del_ax25(axp);
	else
		disc_ax25(axp);
}

/*---------------------------------------------------------------------------*/

/* Close an AX.25 connection */
int
disc_ax25(
struct ax25_cb *axp)
{
	if(axp == NULL)
		return -1;
	/* A link that stays in the node ends at once and takes the other half
	 * with it: there is no DISC to send, nothing in flight to wait for,
	 * and no timer that would ever come back to finish the job.  Unlinked
	 * first, because closing the far side comes back here.
	 */
	if(axp->loop != NULL){
		struct ax25_cb *peer = axp->loop;

		axp->loop = NULL;
		peer->loop = NULL;
		axp->reason = LB_NORMAL;
		peer->reason = LB_NORMAL;
		/* Both blocks are cleared up by the paths that clear up any
		 * other: lapbstate() deletes one that has no consumer left,
		 * and close_axservice() deletes the one whose last consumer
		 * goes here.  Not by hand - whoever is called below may free
		 * the block, and a pointer kept across that is a pointer into
		 * freed memory.
		 */
		lapbstate(peer,LAPB_DISCONNECTED);
		lapbstate(axp,LAPB_DISCONNECTED);
		return 0;
	}
	switch(axp->state){
	case LAPB_DISCONNECTED:
		break;          /* Ignored */
	case LAPB_LISTEN:
		del_ax25(axp);
		break;
	case LAPB_SETUP:
	case LAPB_DISCPENDING:
		lapbstate(axp,LAPB_DISCONNECTED);
		break;
	case LAPB_CONNECTED:
	case LAPB_RECOVERY:
		if(axp->txq && !axp->flags.closed){
			axp->flags.closed = 1;
			return 0;
		}
		free_q(&axp->txq);
		axp->retries = 0;
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		stop_timer(&axp->t3);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
		break;
	}
	return 0;
}

/* Verify that axp points to a valid ax25 control block */
int
ax25val(
struct ax25_cb *axp)
{
	struct ax25_cb *axp1;

	if(axp == NULL)
		return 0;       /* Null pointer can't be valid */
	for(axp1 = Ax25_cb;axp1 != NULL; axp1 = axp1->next)
		if(axp1 == axp)
			return 1;
	return 0;
}

/* Force a retransmission */
int
kick_ax25(
struct ax25_cb *axp)
{
	if(!ax25val(axp))
		return -1;
	recover(axp);
	return 0;
}

/* Abruptly terminate an AX.25 connection */
int
reset_ax25(
struct ax25_cb *axp)
{
	if(axp == NULL)
		return -1;
	lapbstate(axp,LAPB_DISCONNECTED);
	return 0;
}

int
space_ax25(
struct ax25_cb *axp)
{
	int cnt;

	if(axp == NULL)
		return -1;
	if((axp->state == LAPB_SETUP || axp->state == LAPB_CONNECTED ||
	    axp->state == LAPB_RECOVERY) && !axp->flags.closed) {
		cnt = (axp->maxframe - len_q(axp->txq)) * axp->paclen;
		return (cnt > 0) ? cnt : 0;
	}
	return -1;
}
