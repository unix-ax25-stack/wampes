/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arphdr.c,v 1.1 1990/08/23 17:32:27 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "arp.h"

/* Copy a host format arp structure into mbuf for transmission */
struct mbuf *
htonarp(arp)
register struct arp *arp;
{
	struct mbuf *bp;
	register char *buf;

	if(arp == (struct arp *)NULL)
		return NULLBUF;
	if((bp = alloc_mbuf(sizeof(struct arp))) == NULLBUF)
		return NULLBUF;

	buf = bp->data;

	buf = put16(buf,arp->hardware);
	buf = put16(buf,arp->protocol);
	*buf++ = arp->hwalen;
	*buf++ = arp->pralen;
	buf = put16(buf,arp->opcode);
	memcpy(buf,arp->shwaddr,(int16)uchar(arp->hwalen));
	buf += arp->hwalen;
	buf = put32(buf,arp->sprotaddr);
	memcpy(buf,arp->thwaddr,(int16)uchar(arp->hwalen));
	buf += arp->hwalen;
	buf = put32(buf,arp->tprotaddr);

	bp->cnt = buf - bp->data;
	return bp;
}
/* Convert an incoming ARP packet into a host-format structure */
int
ntoharp(arp,bpp)
register struct arp *arp;
struct mbuf **bpp;
{
	if(arp == (struct arp *)NULL || bpp == NULLBUFP)
		return -1;

	arp->hardware = pull16(bpp);
	arp->protocol = pull16(bpp);
	arp->hwalen = pullchar(bpp);
	arp->pralen = pullchar(bpp);
	arp->opcode = pull16(bpp);
	pullup(bpp,arp->shwaddr,(int16)uchar(arp->hwalen));
	arp->sprotaddr = pull32(bpp);
	pullup(bpp,arp->thwaddr,(int16)uchar(arp->hwalen));

	arp->tprotaddr = pull32(bpp);

	/* Get rid of anything left over */
	free_p(*bpp);
	*bpp = NULLBUF;
	return 0;
}

