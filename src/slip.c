/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/slip.c,v 1.5 1991/02/24 20:17:38 deyke Exp $ */

/* SLIP (Serial Line IP) encapsulation and control routines.
 * Copyright 1991 Phil Karn
 * Van Jacobsen header compression hooks added by Katie Stevens, UC Davis
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "slip.h"
#include "asy.h"
#include "trace.h"
#include "config.h"
#include "internet.h"
#include "ip.h"
/* #include "slcompre.h" */

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
		type = sl_compress_tcp(&bp,sp->slcomp,1);
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
		free_p(bp);
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
		bp = sp->rbp;
		sp->rbp = NULLBUF;
		sp->rcnt = 0;
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

	char *cp,buf[4096];
	int cnt,xdev;
	struct mbuf *bp,*nbp;
	struct phdr phdr;
	register struct slip *sp;
#ifdef VJCOMPRESS
	int16 len;
#endif
	int cdev;

	xdev = iface->xdev;
	sp = &Slip[xdev];
	cdev = sp->iface->xdev;

	cnt = (*sp->get)(cdev,cp=buf,sizeof(buf));
	while(--cnt >= 0){
		if((bp = slip_decode(sp,*cp++)) == NULLBUF)
			continue;       /* More to come */

#ifdef VJCOMPRESS
		if (sp->iface->trace & IF_TRACE_RAW)
			raw_dump(sp->iface,IF_TRACE_IN,bp);
		if ((len = len_p(bp)) >= 3) {
			/* Got a packet at least minimum length
			 * of a compressed packet
			 */
			if ((sp->escaped & SLIP_VJCOMPR) &&
			    ((c = (bp->data[0] & 0xf0)) != (IPVERSION << 4))) {
				/* Got a compressed TCP packet */
				if (c & 0x80)
					c = SL_TYPE_COMPRESSED_TCP;
				else if (c == SL_TYPE_UNCOMPRESSED_TCP)
					bp->data[0] &= 0x4f;
				len = sl_uncompress_tcp(&bp, len,
					(int16)(c & 0x00ff),
					sp->slcomp);
				if (len <= 0) {
					free_p(bp);
					sp->errors++;
					continue;
				}
			}
		}
#endif

		if((nbp = pushdown(bp,sizeof(phdr))) == NULLBUF){
			free_p(bp);
			continue;
		}
		phdr.iface = sp->iface;
		phdr.type = sp->type;
		memcpy(&nbp->data[0],(char *)&phdr,sizeof(phdr));
		enqueue(&Hopper,nbp);
	}
}
#ifdef VJCOMPRESS
/* Show VJ stats if async interface is SLIP with VJ TCP compression */
void
doslstat(iface)
struct iface *iface;
{
	struct slip *sp;
	struct slcompress *slp;

	if (iface->xdev > SLIP_MAX)
		/* Must not be a SLIP device */
		return;

	sp = &Slip[iface->xdev];
	if (sp->iface != iface)
		/* Must not be a SLIP device */
		return;

	if ((sp->escaped & SLIP_VJCOMPR) == 0)
		/* SLIP device, but not doing VJ TCP header compression */
		return;

	slp = sp->slcomp;
	tprintf("  Link encap SLIP::VJ Compress\n");
	tprintf("  IN:  pkt %lu comp %lu",
		 iface->rawrecvcnt, slp->sls_compressedin);
	tprintf(" uncomp %lu err %lu toss %lu ip %lu\n",
		 slp->sls_uncompressedin, slp->sls_errorin,
		 slp->sls_tossed, iface->rawrecvcnt -
		 slp->sls_uncompressedin - slp->sls_compressedin);
	tprintf("  OUT: pkt %lu comp %lu uncomp %lu",
		 iface->rawsndcnt, slp->sls_compressed,
		 slp->sls_uncompressed);
	tprintf(" ip %lu search %lu miss %lu",
		 (slp->sls_nontcp + slp->sls_asistcp),
		 slp->sls_searches, slp->sls_misses);
	tprintf("\n");
}
#else
void
doslstat(iface)
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

	tprintf("  Link encap SLIP\n");
	tprintf("  IN:  pkt %lu", iface->rawrecvcnt);
	tprintf("  OUT: pkt %lu\n", iface->rawsndcnt);
}
#endif

