/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25hdr.c,v 1.1 1990/09/11 13:45:01 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "ax25.h"

/* Convert a host-format AX.25 header into a mbuf ready for transmission */
struct mbuf *
htonax25(hdr,data)
register struct ax25 *hdr;
struct mbuf *data;
{
	struct mbuf *bp;
	register char *cp;
	register int16 i;

	if(hdr == (struct ax25 *)NULL || hdr->ndigis > MAXDIGIS)
		return NULLBUF;

	/* Allocate space for return buffer */
	i = AXALEN * (2 + hdr->ndigis);
	if((bp = pushdown(data,i)) == NULLBUF)
		return NULLBUF;

	/* Now convert */
	cp = bp->data;          /* cp -> dest field */

	hdr->dest.ssid &= ~E;   /* Dest E-bit is always off */
	/* Encode command/response in C bits */
	switch(hdr->cmdrsp){
	case LAPB_COMMAND:
		hdr->dest.ssid |= C;
		hdr->source.ssid &= ~C;
		break;
	case LAPB_RESPONSE:
		hdr->dest.ssid &= ~C;
		hdr->source.ssid |= C;
		break;
	default:
		hdr->dest.ssid &= ~C;
		hdr->source.ssid &= ~C;
		break;
	}
	cp = putaxaddr(cp,&hdr->dest);

	/* Set E bit on source address if no digis */
	if(hdr->ndigis == 0){
		hdr->source.ssid |= E;
		putaxaddr(cp,&hdr->source);
		return bp;
	}
	hdr->source.ssid &= ~E;
	cp = putaxaddr(cp,&hdr->source);

	/* All but last digi get copied with E bit off */
	for(i=0; i < hdr->ndigis - 1; i++){
		hdr->digis[i].ssid &= ~E;
		cp = putaxaddr(cp,&hdr->digis[i]);
	}
	hdr->digis[i].ssid |= E;
	cp = putaxaddr(cp,&hdr->digis[i]);
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
	register struct ax25_addr *axp;
	char *getaxaddr();
	char buf[AXALEN];

	if(pullup(bpp,buf,AXALEN) < AXALEN)
		return -1;
	getaxaddr(&hdr->dest,buf);

	if(pullup(bpp,buf,AXALEN) < AXALEN)
		return -1;
	getaxaddr(&hdr->source,buf);

	/* Process C bits to get command/response indication */
	if((hdr->source.ssid & C) == (hdr->dest.ssid & C))
		hdr->cmdrsp = LAPB_UNKNOWN;
	else if(hdr->source.ssid & C)
		hdr->cmdrsp = LAPB_RESPONSE;
	else
		hdr->cmdrsp = LAPB_COMMAND;

	hdr->ndigis = 0;
	if(hdr->source.ssid & E)
		return 2;       /* No digis */

	/* Process digipeaters */
	for(axp = hdr->digis;axp < &hdr->digis[MAXDIGIS]; axp++){
		if(pullup(bpp,buf,AXALEN) < AXALEN)
			return -1;
		getaxaddr(axp,buf);
		if(axp->ssid & E){      /* Last one */
			hdr->ndigis = axp - hdr->digis + 1;
			return hdr->ndigis + 2;
		}
	}
	return -1;      /* Too many digis */
}

