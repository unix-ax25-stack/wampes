/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iphdr.c,v 1.1 1990/09/11 13:45:43 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "ip.h"
#include "internet.h"

/* Convert IP header in host format to network mbuf */
struct mbuf *
htonip(ip,data)
struct ip *ip;
struct mbuf *data;
{
	int16 hdr_len;
	struct mbuf *bp;
	register char *cp;
	int16 checksum;
	int16 fl_offs;

	hdr_len = IPLEN + ip->optlen;
	if((bp = pushdown(data,hdr_len)) == NULLBUF){
		free_p(data);
		return NULLBUF;
	}
	cp = bp->data;

	*cp++ = (IPVERSION << 4) | (hdr_len >> 2);
	*cp++ = ip->tos;
	cp = put16(cp,ip->length);
	cp = put16(cp,ip->id);
	fl_offs = ip->offset >> 3;
	if(ip->flags.df)
		fl_offs |= 0x4000;
	if(ip->flags.mf)
		fl_offs |= 0x2000;

	cp = put16(cp,fl_offs);
	*cp++ = ip->ttl;
	*cp++ = ip->protocol;
	cp = put16(cp,0);       /* Clear checksum */
	cp = put32(cp,ip->source);
	cp = put32(cp,ip->dest);
	if(ip->optlen != 0)
		memcpy(cp,ip->options,ip->optlen);

	/* Compute checksum and insert into header */
	checksum = cksum(NULLHEADER,bp,hdr_len);
	put16(&bp->data[10],checksum);

	return bp;
}
/* Extract an IP header from mbuf */
int
ntohip(ip,bpp)
struct ip *ip;
struct mbuf **bpp;
{
	int16 ihl;
	int16 fl_offs;
	char ipbuf[IPLEN];

	if(pullup(bpp,ipbuf,IPLEN) != IPLEN)
		return -1;

	ip->version = (ipbuf[0] >> 4) & 0xf;
	ip->tos = ipbuf[1];
	ip->length = get16(&ipbuf[2]);
	ip->id = get16(&ipbuf[4]);
	fl_offs = get16(&ipbuf[6]);
	ip->offset = (fl_offs & 0x1fff) << 3;
	ip->flags.mf = (fl_offs & 0x2000) ? 1 : 0;
	ip->flags.df = (fl_offs & 0x4000) ? 1 : 0;
	ip->ttl = ipbuf[8];
	ip->protocol = ipbuf[9];
	ip->source = get32(&ipbuf[12]);
	ip->dest = get32(&ipbuf[16]);

	ihl = (ipbuf[0] & 0xf) << 2;
	if(ihl < IPLEN){
		/* Bogus packet; header is too short */
		return -1;
	}
	ip->optlen = ihl - IPLEN;
	if(ip->optlen != 0)
		pullup(bpp,ip->options,ip->optlen);

	return ip->optlen + IPLEN;
}
/* Perform end-around-carry adjustment */
int16
eac(sum)
register int32 sum;     /* Carries in high order 16 bits */
{
	register int16 csum;

	while((csum = sum >> 16) != 0)
		sum = csum + (sum & 0xffffL);
	return (int16) (sum & 0xffffl); /* Chops to 16 bits */
}
/* Checksum a mbuf chain, with optional pseudo-header */
int16
cksum(ph,m,len)
struct pseudo_header *ph;
register struct mbuf *m;
int16 len;
{
	register int16 cnt, total;
	register int32 sum, csum;
	register char *up;
	int16 csum1;
	int swap = 0;

	sum = 0l;

	/* Sum pseudo-header, if present */
	if(ph != NULLHEADER){
		sum = hiword(ph->source);
		sum += loword(ph->source);
		sum += hiword(ph->dest);
		sum += loword(ph->dest);
		sum += uchar(ph->protocol);
		sum += ph->length;
	}
	/* Now do each mbuf on the chain */
	for(total = 0; m != NULLBUF && total < len; m = m->next) {
		cnt = min(m->cnt, len - total);
		up = (char *)m->data;
		csum = 0;

		if(((long)up) & 1){
			/* Handle odd leading byte */
			if(swap)
				csum = uchar(*up++);
			else
				csum = (int16)(uchar(*up++) << 8);
			cnt--;
			swap = !swap;
		}
		if(cnt > 1){
			/* Have the primitive checksumming routine do most of
			 * the work. At this point, up is guaranteed to be on
			 * a short boundary
			 */
			csum1 = lcsum((unsigned short *)up, (int16)(cnt >> 1));
			if(swap)
				csum1 = (csum1 << 8) | (csum1 >> 8);
			csum += csum1;
		}
		/* Handle odd trailing byte */
		if(cnt & 1){
			if(swap)
				csum += uchar(up[--cnt]);
			else
				csum += (int16)(uchar(up[--cnt]) << 8);
			swap = !swap;
		}
		sum += csum;
		total += m->cnt;
	}
	/* Do final end-around carry, complement and return */
	return (int16)(~eac(sum) & 0xffff);
}

