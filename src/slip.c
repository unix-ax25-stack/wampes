/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/slip.c,v 1.3 1990/09/11 13:46:21 deyke Exp $ */

/* Send and receive IP datagrams on serial lines. Compatible with SLIP
 * under Berkeley Unix.
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "slip.h"
#include "asy.h"
#include "trace.h"

int Nslip;

static struct mbuf *slip_decode __ARGS((int dev,int c));
static struct mbuf *slip_encode __ARGS((struct mbuf *bp));

/* Slip level control structure */
struct slip Slip[ASY_MAX];

/* Send routine for point-to-point slip
 * This is a trivial function since there is no slip link-level header
 */
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
	if(iface == NULLIF){
		free_p(bp);
		return -1;
	}
	iface->rawsndcnt++;
	return (*iface->raw)(iface,bp);
}
/* Send a raw slip frame -- also trivial */
int
slip_raw(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct mbuf *bp1;

	dump(iface,IF_TRACE_OUT,Slip[iface->xdev].type,bp);
	if((bp1 = slip_encode(bp)) == NULLBUF){
		free_p(bp);
		return -1;
	}
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
slip_decode(dev,c)
int dev;        /* Slip unit number */
char c;         /* Incoming character */
{
	struct mbuf *bp;
	register struct slip *sp;

	sp = &Slip[dev];
	switch(uchar(c)){
	case FR_END:
		bp = sp->rbp;
		sp->rbp = NULLBUF;
		sp->rcnt = 0;
		return bp;      /* Will be NULLBUF if empty frame */
	case FR_ESC:
		sp->escaped = 1;
		return NULLBUF;
	}
	if(sp->escaped){
		/* Translate 2-char escape sequence back to original char */
		sp->escaped = 0;
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
	if(sp->rbp == NULLBUF){
		/* Allocate first mbuf for new packet */
		if((sp->rbp1 = sp->rbp = alloc_mbuf(SLIP_ALLOC)) == NULLBUF)
			return NULLBUF; /* No memory, drop */
		sp->rcp = sp->rbp->data;
	} else if(sp->rbp1->cnt == SLIP_ALLOC){
		/* Current mbuf is full; link in another */
		if((sp->rbp1->next = alloc_mbuf(SLIP_ALLOC)) == NULLBUF){
			/* No memory, drop whole thing */
			free_p(sp->rbp);
			sp->rbp = NULLBUF;
			sp->rcnt = 0;
			return NULLBUF;
		}
		sp->rbp1 = sp->rbp1->next;
		sp->rcp = sp->rbp1->data;
	}
	/* Store the character, increment fragment and total
	 * byte counts
	 */
	*sp->rcp++ = c;
	sp->rbp1->cnt++;
	sp->rcnt++;
	return NULLBUF;
}
/* Process SLIP line input */
void
asy_rx(iface)
struct iface *iface;
{

	char *cp,buf[1024];
	int cnt,dev;
	struct mbuf *bp,*nbp;
	struct phdr *phdr;
	struct slip *sp;

	dev = iface->xdev;
	sp = Slip+dev;

	cnt = (*sp->get)(iface->dev,cp=buf,sizeof(buf));
	while(--cnt >= 0){
		if((bp = slip_decode(dev,*cp++)) == NULLBUF)
			continue;       /* More to come */

		if((nbp = pushdown(bp,sizeof(struct phdr))) == NULLBUF){
			free_p(bp);
			continue;
		}
		phdr = (struct phdr *)nbp->data;
		phdr->iface = iface;
		phdr->type = sp->type;
		enqueue(&Hopper,nbp);
	}
}
