/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25user.c,v 1.1 1993/01/29 06:51:57 deyke Exp $ */

/* User interface subroutines for AX.25
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "lapb.h"
#include "ax25.h"
#include "lapb.h"
#include <ctype.h>

/* Open an AX.25 connection */
struct ax25_cb *
open_ax25(hdr,mode,r_upcall,t_upcall,s_upcall,user)
struct ax25 *hdr;
int mode;               /* active/passive/server */
void (*r_upcall) __ARGS((struct ax25_cb *,int));
void (*t_upcall) __ARGS((struct ax25_cb *,int));
void (*s_upcall) __ARGS((struct ax25_cb *,int,int));
char *user;             /* User linkage area */
{
	struct ax25_cb *axp;

	switch(mode){
	case AX_ACTIVE:
		if((axp = find_ax25(hdr->dest)) != NULLAX25)
			return NULLAX25;        /* Only one to a customer */
		if(axp == NULLAX25 && (axp = cr_ax25(NULLAX25)) == NULLAX25)
			return NULLAX25;
		build_path(axp,NULLIF,hdr,0);
		axp->r_upcall = r_upcall;
		axp->t_upcall = t_upcall;
		axp->s_upcall = s_upcall;
		axp->user = user;
		lapbstate(axp,LAPB_SETUP);
		return axp;
	case AX_SERVER:
		if((axp = cr_ax25(NULLAX25)) == NULLAX25)
			return NULLAX25;
		axp->state = LAPB_LISTEN;
		axp->r_upcall = r_upcall;
		axp->t_upcall = t_upcall;
		axp->s_upcall = s_upcall;
		axp->user = user;
		return axp;
	default:
		return NULLAX25;
	}
}

int
send_ax25(axp,bp)
struct ax25_cb *axp;
struct mbuf *bp;
{

	if(axp == NULLAX25 || bp == NULLBUF){
		free_p(bp);
		return -1;
	}
	if (axp->mode == STREAM)
		append(&axp->sndq,bp);
	else
		enqueue(&axp->sndq,bp);
	axp->sndqtime = msclock();
	lapb_output(axp,0);
	return 1;
}

/* Receive incoming data on an AX.25 connection */
struct mbuf *
recv_ax25(axp,cnt)
struct ax25_cb *axp;
int16 cnt;
{
	struct mbuf *bp;

	if(axp->rxq == NULLBUF)
		return NULLBUF;

	if(cnt == 0 || axp->mode == DGRAM || axp->rcvcnt <= cnt){
		/* This means we want it all */
		bp = dequeue(&axp->rxq);
	} else {
		bp = ambufw(cnt);
		bp->cnt = pullup(&axp->rxq,bp->data,cnt);
	}
	axp->rcvcnt -= len_p(bp);
	/* If this has un-busied us, send a RR to reopen the window */
	if (axp->flags.rnrsent && !busy(axp))
		send_ack(axp,RESP);

	return bp;
}

/* Close an AX.25 connection */
int
disc_ax25(axp)
struct ax25_cb *axp;
{
	if(axp == NULLAX25)
		return -1;
	if(axp->flags.closed)
		return 0;
	axp->flags.closed = 1;
	switch(axp->state){
	case LAPB_DISCONNECTED:
	case LAPB_DISCPENDING:
		break;          /* Ignored */
	case LAPB_LISTEN:
		del_ax25(axp);
		break;
	case LAPB_SETUP:
		lapbstate(axp,LAPB_DISCONNECTED);
		break;
	case LAPB_CONNECTED:
		if (!axp->sndq && !axp->unack)
			lapbstate(axp,LAPB_DISCPENDING);
		break;
	}
	return 0;
}

/* Verify that axp points to a valid ax25 control block */
int
ax25val(axp)
struct ax25_cb *axp;
{
	register struct ax25_cb *axp1;

	if(axp == NULLAX25)
		return 0;       /* Null pointer can't be valid */
	for(axp1 = Ax25_cb;axp1 != NULLAX25; axp1 = axp1->next)
		if(axp1 == axp)
			return 1;
	return 0;
}

/* Force a retransmission */
int
kick_ax25(axp)
struct ax25_cb *axp;
{
	if(!ax25val(axp))
		return -1;
	recover(axp);
	return 0;
}

/* Abruptly terminate an AX.25 connection */
int
reset_ax25(axp)
struct ax25_cb *axp;
{
	if(axp == NULLAX25)
		return -1;
	if(axp == Axcb_server){
		del_ax25(axp);
	} else {
		axp->reason = LB_NORMAL;
		lapbstate(axp,LAPB_DISCONNECTED);
	}
	return 0;
}

int
space_ax25(axp)
struct ax25_cb *axp;
{
	int cnt;

	if(axp == NULLAX25)
		return -1;
	if(axp->state == LAPB_SETUP || axp->state == LAPB_CONNECTED)
		if (!axp->flags.closed) {
			cnt = (axp->cwind - axp->unack) * Paclen - len_p(axp->sndq);
			return (cnt > 0) ? cnt : 0;
		}
	return -1;
}
