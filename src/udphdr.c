/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/udphdr.c,v 1.2 1991/02/24 20:18:02 deyke Exp $ */

/* UDP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "internet.h"
#include "udp.h"

/* Convert UDP header in internal format to an mbuf in external format */
struct mbuf *
htonudp(udp,data,ph)
struct udp *udp;
struct mbuf *data;
struct pseudo_header *ph;
{
	struct mbuf *bp;
	register char *cp;
	int16 checksum;

	/* Allocate UDP protocol header and fill it in */
	if((bp = pushdown(data,UDPHDR)) == NULLBUF)
		return NULLBUF;

	cp = bp->data;
	cp = put16(cp,udp->source);     /* Source port */
	cp = put16(cp,udp->dest);       /* Destination port */
	cp = put16(cp,udp->length);     /* Length */
	*cp++ = 0;                      /* Clear checksum */
	*cp-- = 0;

	/* All zeros and all ones is equivalent in one's complement arithmetic;
	 * the spec requires us to change zeros into ones to distinguish an
	 * all-zero checksum from no checksum at all
	 */
	if((checksum = cksum(ph,bp,ph->length)) == 0)
		checksum = 0xffffffff;
	put16(cp,checksum);
	return bp;
}
/* Convert UDP header in mbuf to internal structure */
int
ntohudp(udp,bpp)
struct udp *udp;
struct mbuf **bpp;
{
	char udpbuf[UDPHDR];

	if(pullup(bpp,udpbuf,UDPHDR) != UDPHDR)
		return -1;
	udp->source = get16(&udpbuf[0]);
	udp->dest = get16(&udpbuf[2]);
	udp->length = get16(&udpbuf[4]);
	udp->checksum = get16(&udpbuf[6]);
	return 0;
}

