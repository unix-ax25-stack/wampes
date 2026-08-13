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

/* Open an AX.25 connection */
struct ax25_cb *
open_ax25(
struct ax25 *hdr,
int mode,               /* active/passive/server */
const struct ax25_opts *opts    /* per-connection choices, 0 for the usual */
){
	struct ax25_cb *axp;

	axp = find_ax25(hdr->dest);
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
		if((axp = cr_ax25(hdr->dest)) == NULL){
			Net_error = NO_MEM;
			return NULL;
		}
		build_path(axp,NULL,hdr,0,opts);
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
	if(pid != -1){
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
		bp = sp->rxq;
		sp->rxq = NULL;
	} else {
		bp = ambufw(cnt);
		bp->cnt = pullup(&sp->rxq,bp->data,cnt);
	}
	/* If this has un-busied the link, reopen the window */
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
		held += len_p(sp->rxq);
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
	if(axp->services == NULL)
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
