/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25user.c,v 1.3 1993/02/26 10:17:43 deyke Exp $ */

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
void (*r_upcall) __ARGS((struct ax25_cb *,int));        /* Receiver upcall handler */
void (*t_upcall) __ARGS((struct ax25_cb *,int));        /* Transmitter upcall handler */
void (*s_upcall) __ARGS((struct ax25_cb *,int,int));    /* State-change upcall handler */
char *user;             /* User linkage area */
{
	struct ax25_cb *axp;

	axp = find_ax25(hdr->dest);
	if(axp && axp->s_upcall != NULLVFP && s_upcall != NULLVFP)
		return NULLAX25;        /* Only one to a customer */
	if(axp == NULLAX25){
		if((axp = cr_ax25(hdr->dest)) == NULLAX25)
			return NULLAX25;
		build_path(axp,NULLIF,hdr,0);
	}
	if(s_upcall != NULLVFP){
		axp->r_upcall = r_upcall;
		axp->t_upcall = t_upcall;
		axp->s_upcall = s_upcall;
		axp->user = user;
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
#if 0
	case LAPB_SETUP:
		free_q(&axp->txq);
		break;
	case LAPB_DISCPENDING:  /* Ignore */
		break;
	case LAPB_RECOVERY:
	case LAPB_CONNECTED:
		free_q(&axp->txq);
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
		break;
#endif
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
send_ax25(axp,bp,pid)
struct ax25_cb *axp;
struct mbuf *bp;
int pid;
{
	struct mbuf *bp1;
	int16 offset,len,size;

	if(axp == NULLAX25 || bp == NULLBUF || axp->flags.closed){
		free_p(bp);
		return -1;
	}
	if(pid != -1){
		offset = 0;
		len = len_p(bp);
		/* It is important that all the pushdowns be done before
		 * any part of the original packet is freed.
		 * Otherwise the pushdown might erroneously overwrite
		 * a part of the packet that had been duped and freed.
		 */
		while(len != 0){
			size = min(len,axp->paclen);
			dup_p(&bp1,bp,offset,size);
			len -= size;
			offset += size;
			bp1 = pushdown(bp1,1);
			bp1->data[0] = pid;
			enqueue(&axp->txq,bp1);
		}
		free_p(bp);
	} else {
		enqueue(&axp->txq,bp);
	}
	return lapb_output(axp);
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

	if(cnt == 0){
		/* This means we want it all */
		bp = axp->rxq;
		axp->rxq = NULLBUF;
	} else {
		bp = ambufw(cnt);
		bp->cnt = pullup(&axp->rxq,bp->data,cnt);
	}
	/* If this has un-busied us, send a RR to reopen the window */
	if (axp->flags.rnrsent && !busy(axp))
		sendctl(axp,LAPB_RESPONSE,RR);

	return bp;
}

/* Close an AX.25 connection */
int
disc_ax25(axp)
struct ax25_cb *axp;
{
	if(axp == NULLAX25)
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
	void (*upcall)();

	if(axp == NULLAX25)
		return -1;
	upcall = axp->s_upcall;
	lapbstate(axp,LAPB_DISCONNECTED);
#if 0
	/* Clean up if the standard upcall isn't in use */
	if(upcall != s_ascall)
		del_ax25(axp);
#endif
	return 0;
}

int
space_ax25(axp)
struct ax25_cb *axp;
{
	int cnt;

	if(axp == NULLAX25)
		return -1;
	if((axp->state == LAPB_SETUP || axp->state == LAPB_CONNECTED ||
	    axp->state == LAPB_RECOVERY) && !axp->flags.closed) {
		cnt = (axp->maxframe - len_q(axp->txq)) * axp->paclen;
		return (cnt > 0) ? cnt : 0;
	}
	return -1;
}
