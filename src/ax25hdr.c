/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25hdr.c,v 1.4 1993/04/20 09:27:06 deyke Exp $ */

/* AX25 header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"

/* Convert a host-format AX.25 header into a mbuf ready for transmission */
struct mbuf *
htonax25(hdr,bp)
register struct ax25 *hdr;
struct mbuf *bp;
{
	register char *cp;
	register int16 i;

	if(hdr == (struct ax25 *)NULL || hdr->ndigis > MAXDIGIS)
		return NULLBUF;

	/* Allocate space for return buffer */
	i = AXALEN * (2 + hdr->ndigis);
	bp = pushdown(bp,i);

	/* Now convert */
	cp = bp->data;          /* cp -> dest field */

	/* Generate destination field */
	memcpy(cp,hdr->dest,AXALEN);
	if(hdr->cmdrsp == LAPB_COMMAND)
		cp[ALEN] |= C;  /* Command frame sets C bit in dest */
	else
		cp[ALEN] &= ~C;
	cp[ALEN] &= ~E; /* Dest E-bit is always off */

	cp += AXALEN;           /* cp -> source field */

	/* Generate source field */
	memcpy(cp,hdr->source,AXALEN);
	if(hdr->cmdrsp == LAPB_RESPONSE)
		cp[ALEN] |= C;
	else
		cp[ALEN] &= ~C;
	/* Set E bit on source address if no digis */
	if(hdr->ndigis == 0){
		cp[ALEN] |= E;
		return bp;
	} else
		cp[ALEN] &= ~E;

	cp += AXALEN;           /* cp -> first digi field */

	/* All but last digi get copied with E bit off */
	for(i=0; i < hdr->ndigis; i++){
		memcpy(cp,hdr->digis[i],AXALEN);
		if(i < hdr->ndigis - 1)
			cp[ALEN] &= ~E;
		else
			cp[ALEN] |= E;  /* Last digipeater has E bit set */
		if(i < hdr->nextdigi)
			cp[ALEN] |= REPEATED;
		else
			cp[ALEN] &= ~REPEATED;
		cp += AXALEN;           /* cp -> next digi field */
	}
	return bp;
}
/* Convert a network-format AX.25 header into a host format structure
 * Return -1 if error, number of addresses if OK
 */
int
ntohax25(hdr,bpp)
register struct ax25 *hdr;      /* Output structure */
struct mbuf **bpp;
{
	register char *axp;

	if(pullup(bpp,hdr->dest,AXALEN) < AXALEN)
		return -1;

	/* Decompress Flexnet header */
	if (hdr->dest[1] & 1) {
		hdr->ndigis = hdr->nextdigi = 0;
		hdr->cmdrsp = (hdr->dest[1] & 2) ? LAPB_COMMAND : LAPB_RESPONSE;
		hdr->qso_num = ((hdr->dest[0] << 6) & 0x3fc0) | ((hdr->dest[1] >> 2) & 0x3f);
		hdr->dest[0] = (((hdr->dest[2] >> 1) & 0x7e)                               ) + 0x40;
		hdr->dest[1] = (((hdr->dest[2] << 5) & 0x60) | ((hdr->dest[3] >> 3) & 0x1e)) + 0x40;
		hdr->dest[2] = (((hdr->dest[3] << 3) & 0x78) | ((hdr->dest[4] >> 5) & 0x06)) + 0x40;
		hdr->dest[3] = (((hdr->dest[4] << 1) & 0x7e)                               ) + 0x40;
		hdr->dest[4] = (((hdr->dest[5] >> 1) & 0x7e)                               ) + 0x40;
		hdr->dest[5] = (((hdr->dest[5] << 5) & 0x60) | ((hdr->dest[6] >> 3) & 0x1e)) + 0x40;
		hdr->dest[6] = ((hdr->dest[6] << 1) & SSID) | 0x60;
		hdr->source[0] = 0x7e;
		hdr->source[1] = 0x40;
		hdr->source[2] = 0x40;
		hdr->source[3] = 0x40;
		hdr->source[4] = 0x40;
		hdr->source[5] = 0x40;
		hdr->source[6] = 0x60;
		return 2;
	}
	hdr->qso_num = -1;

	if(pullup(bpp,hdr->source,AXALEN) < AXALEN)
		return -1;

	/* Process C bits to get command/response indication */
	if((hdr->source[ALEN] & C) == (hdr->dest[ALEN] & C))
		hdr->cmdrsp = LAPB_UNKNOWN;
	else if(hdr->source[ALEN] & C)
		hdr->cmdrsp = LAPB_RESPONSE;
	else
		hdr->cmdrsp = LAPB_COMMAND;

	hdr->ndigis = 0;
	hdr->nextdigi = 0;
	if(hdr->source[ALEN] & E)
		return 2;       /* No digis */

	/* Count and process the digipeaters */
	axp = hdr->digis[0];
	while(hdr->ndigis < MAXDIGIS && pullup(bpp,axp,AXALEN) == AXALEN){
		hdr->ndigis++;
		if(axp[ALEN] & REPEATED)
			hdr->nextdigi++;
		if(axp[ALEN] & E)       /* Last one */
			return hdr->ndigis + 2;
		axp += AXALEN;
	}
	return -1;      /* Too many digis */
}

