/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/slip.c,v 1.9 1992/05/14 13:20:28 deyke Exp $ */

/* SLIP (Serial Line IP) encapsulation and control routines.
 * Copyright 1991 Phil Karn
 *
 * Van Jacobsen header compression hooks added by Katie Stevens, UC Davis
 *
 *      - Feb 1991      Bill_Simpson@um.cc.umich.edu
 *                      reflect changes to header compression calls
 *                      revise status display
 */
#include <stdio.h>
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "slhc.h"
#include "asy.h"
#include "slip.h"
#include "trace.h"

static struct mbuf *slip_decode __ARGS((struct slip *sp,int c));
static struct mbuf *slip_encode __ARGS((struct mbuf *bp));

/* Slip level control structure */
struct slip Slip[ASY_MAX];

/* Send routine for point-to-point slip */
int
slip_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;        /* Buffer to send */
struct iface *iface;    /* Pointer to interface control block */
int32 gateway;          /* Ignored (SLIP is point-to-point) */
int prec;
int del;
int tput;
int rel;
{
#ifdef VJCOMPRESS
	register struct slip *sp;
	int type;
#endif
	if(iface == NULLIF){
		free_p(bp);
		return -1;
	}
#ifdef VJCOMPRESS
	sp = &Slip[iface->xdev];
	if (sp->escaped & SLIP_VJCOMPR) {
		/* Attempt IP/ICP header compression */
		type = slhc_compress(sp->slcomp,&bp,TRUE);
		bp->data[0] |= type;
	}
#endif
	return (*iface->raw)(iface,bp);
}
/* Send a raw slip frame */
int
slip_raw(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct mbuf *bp1;

	dump(iface,IF_TRACE_OUT,Slip[iface->xdev].type,bp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	if((bp1 = slip_encode(bp)) == NULLBUF){
		return -1;
	}
	if (iface->trace & IF_TRACE_RAW)
		raw_dump(iface,-1,bp1);
	return Slip[iface->xdev].send(iface->dev,bp1);
}
/* Encode a packet in SLIP format */
static
struct mbuf *
slip_encode(bp)
struct mbuf *bp;
{
	struct mbuf *lbp;       /* Mbuf containing line-ready packet */
	register char *cp;
	int c;

	/* Allocate output mbuf that's twice as long as the packet.
	 * This is a worst-case guess (consider a packet full of FR_ENDs!)
	 */
	lbp = alloc_mbuf((int16)(2*len_p(bp) + 2));
	if(lbp == NULLBUF){
		/* No space; drop */
		free_p(bp);
		return NULLBUF;
	}
	cp = lbp->data;

	/* Flush out any line garbage */
	*cp++ = FR_END;

	/* Copy input to output, escaping special characters */
	while((c = PULLCHAR(&bp)) != -1){
		switch(c){
		case FR_ESC:
			*cp++ = FR_ESC;
			*cp++ = T_FR_ESC;
			break;
		case FR_END:
			*cp++ = FR_ESC;
			*cp++ = T_FR_END;
			break;
		default:
			*cp++ = c;
		}
	}
	*cp++ = FR_END;
	lbp->cnt = cp - lbp->data;
	return lbp;
}
/* Process incoming bytes in SLIP format
 * When a buffer is complete, return it; otherwise NULLBUF
 */
static
struct mbuf *
slip_decode(sp,c)
register struct slip *sp;
char c;         /* Incoming character */
{
	struct mbuf *bp;

	switch(uchar(c)){
	case FR_END:
		bp = sp->rbp_head;
		sp->rbp_head = NULLBUF;
		return bp;      /* Will be NULLBUF if empty frame */
	case FR_ESC:
		sp->escaped |= SLIP_FLAG;
		return NULLBUF;
	}
	if(sp->escaped & SLIP_FLAG){
		/* Translate 2-char escape sequence back to original char */
		sp->escaped &= ~SLIP_FLAG;
		switch(uchar(c)){
		case T_FR_ESC:
			c = FR_ESC;
			break;
		case T_FR_END:
			c = FR_END;
			break;
		default:
			sp->errors++;
			break;
		}
	}
	/* We reach here with a character for the buffer;
	 * make sure there's space for it
	 */
	if(sp->rbp_head == NULLBUF){
		/* Allocate first mbuf for new packet */
		if((sp->rbp_tail = sp->rbp_head = alloc_mbuf(SLIP_ALLOC)) == NULLBUF)
			return NULLBUF; /* No memory, drop */
		sp->rcp = sp->rbp_head->data;
	} else if(sp->rbp_tail->cnt == SLIP_ALLOC){
		/* Current mbuf is full; link in another */
		if((sp->rbp_tail->next = alloc_mbuf(SLIP_ALLOC)) == NULLBUF){
			/* No memory, drop whole thing */
			free_p(sp->rbp_head);
			sp->rbp_head = NULLBUF;
			return NULLBUF;
		}
		sp->rbp_tail = sp->rbp_tail->next;
		sp->rcp = sp->rbp_tail->data;
	}
	/* Store the character, increment fragment and total
	 * byte counts
	 */
	*sp->rcp++ = c;
	sp->rbp_tail->cnt++;
	return NULLBUF;
}
/* Process SLIP line input */
void
asy_rx(iface)
struct iface *iface;
{

	char *cp,buf[4096];
	int cnt,xdev;
	struct mbuf *bp;
	register struct slip *sp;
	int cdev;

	xdev = iface->xdev;
	sp = &Slip[xdev];
	cdev = sp->iface->dev;

	cnt = (*sp->get)(cdev,cp=buf,sizeof(buf));
	while(--cnt >= 0){
		if((bp = slip_decode(sp,*cp++)) == NULLBUF)
			continue;       /* More to come */

#ifdef VJCOMPRESS
		if (sp->iface->trace & IF_TRACE_RAW)
			raw_dump(sp->iface,IF_TRACE_IN,bp);

		if (sp->escaped & SLIP_VJCOMPR) {
			if ((c = bp->data[0]) & SL_TYPE_COMPRESSED_TCP) {
				if ( slhc_uncompress(sp->slcomp, &bp) <= 0 ) {
					free_p(bp);
					sp->errors++;
					continue;
				}
			} else if (c >= SL_TYPE_UNCOMPRESSED_TCP) {
				bp->data[0] &= 0x4f;
				if ( slhc_remember(sp->slcomp, &bp) <= 0 ) {
					free_p(bp);
					sp->errors++;
					continue;
				}
			}
		}
#endif
		if ( net_route( sp->iface, sp->type, bp ) != 0 ) {
			free_p(bp);
		}
	}
/*      free_p(bp); */
}

/* Show serial line status */
void
slip_status(iface)
struct iface *iface;
{
	struct slip *sp;

	if (iface->xdev > SLIP_MAX)
		/* Must not be a SLIP device */
		return;

	sp = &Slip[iface->xdev];
	if (sp->iface != iface)
		/* Must not be a SLIP device */
		return;

	printf("  IN:\t%lu pkts\n", iface->rawrecvcnt);
#ifdef VJCOMPRESS
	slhc_i_status(sp->slcomp);
#endif
	printf("  OUT:\t%lu pkts\n", iface->rawsndcnt);
#ifdef VJCOMPRESS
	slhc_o_status(sp->slcomp);
#endif
}

