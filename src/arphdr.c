/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arphdr.c,v 1.5 1994/10/06 16:15:19 deyke Exp $ */

/* ARP header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "arp.h"

/* Copy a host format arp structure into mbuf for transmission */
struct mbuf *
htonarp(
register struct arp *arp)
{
	struct mbuf *bp;
	register char *buf;

	if(arp == (struct arp *)NULL)
		return NULLBUF;

	if((bp = alloc_mbuf(ARPLEN + 2 * uchar(arp->hwalen))) == NULLBUF)
		return NULLBUF;

	buf = bp->data;

	buf = put16(buf,arp->hardware);
	buf = put16(buf,arp->protocol);
	*buf++ = arp->hwalen;
	*buf++ = arp->pralen;
	buf = put16(buf,arp->opcode);
	memcpy(buf,arp->shwaddr,(uint16)uchar(arp->hwalen));
	buf += arp->hwalen;
	buf = put32(buf,arp->sprotaddr);
	memcpy(buf,arp->thwaddr,(uint16)uchar(arp->hwalen));
	buf += arp->hwalen;
	buf = put32(buf,arp->tprotaddr);

	bp->cnt = buf - bp->data;
	return bp;
}
/* Convert an incoming ARP packet into a host-format structure */
int
ntoharp(
register struct arp *arp,
struct mbuf **bpp)
{
	if(arp == (struct arp *)NULL || bpp == NULLBUFP)
		return -1;

	arp->hardware = pull16(bpp);
	arp->protocol = pull16(bpp);
	arp->hwalen = PULLCHAR(bpp);
	arp->pralen = PULLCHAR(bpp);
	arp->opcode = pull16(bpp);
	pullup(bpp,arp->shwaddr,(uint16)uchar(arp->hwalen));
	arp->sprotaddr = pull32(bpp);
	pullup(bpp,arp->thwaddr,(uint16)uchar(arp->hwalen));
	arp->tprotaddr = pull32(bpp);

	/* Get rid of anything left over */
	free_p(*bpp);
	*bpp = NULLBUF;
	return 0;
}

