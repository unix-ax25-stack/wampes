/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcphdr.c,v 1.1 1990/10/12 19:26:44 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "tcp.h"
#include "ip.h"
#include "internet.h"

/* Convert TCP header in host format into mbuf ready for transmission,
 * link in data (if any), and compute checksum
 */
struct mbuf *
htontcp(tcph,data,ph)
register struct tcp *tcph;
struct mbuf *data;
struct pseudo_header *ph;
{
	int16 hdrlen;
	struct mbuf *bp;
	register char *cp;
	int16 csum;

	hdrlen = (tcph->mss != 0) ? TCPLEN + MSS_LENGTH : TCPLEN;

	if((bp = pushdown(data,hdrlen)) == NULLBUF){
		free_p(data);
		return NULLBUF;
	}
	cp = bp->data;
	cp = put16(cp,tcph->source);
	cp = put16(cp,tcph->dest);
	cp = put32(cp,tcph->seq);
	cp = put32(cp,tcph->ack);
	*cp++ = hdrlen << 2;    /* Offset field */
	*cp = 0;
	if(tcph->flags.urg)
		*cp |= 32;
	if(tcph->flags.ack)
		*cp |= 16;
	if(tcph->flags.psh)
		*cp |= 8;
	if(tcph->flags.rst)
		*cp |= 4;
	if(tcph->flags.syn)
		*cp |= 2;
	if(tcph->flags.fin)
		*cp |= 1;
	cp++;
	cp = put16(cp,tcph->wnd);
	*cp++ = 0;      /* Zero out checksum field */
	*cp++ = 0;
	cp = put16(cp,tcph->up);

	if(tcph->mss != 0){
		*cp++ = MSS_KIND;
		*cp++ = MSS_LENGTH;
		cp = put16(cp,tcph->mss);
	}
	csum = cksum(ph,bp,ph->length);
	/* Fill checksum field */
	put16(&bp->data[16],csum);

	return bp;
}
/* Pull TCP header off mbuf */
int
ntohtcp(tcph,bpp)
register struct tcp *tcph;
struct mbuf **bpp;
{
	int16 hdrlen;
	int16 i,optlen;
	register int flags;
	char hdrbuf[TCPLEN];

	i = pullup(bpp,hdrbuf,TCPLEN);
	/* Note that the results will be garbage if the header is too short.
	 * We don't check for this because returned ICMP messages will be
	 * truncated, and we at least want to get the port numbers.
	 */
	tcph->source = get16(&hdrbuf[0]);
	tcph->dest = get16(&hdrbuf[2]);
	tcph->seq = get32(&hdrbuf[4]);
	tcph->ack = get32(&hdrbuf[8]);
	hdrlen = (hdrbuf[12] & 0xf0) >> 2;
	flags = hdrbuf[13];
	tcph->flags.urg = flags & 32;
	tcph->flags.ack = flags & 16;
	tcph->flags.psh = flags & 8;
	tcph->flags.rst = flags & 4;
	tcph->flags.syn = flags & 2;
	tcph->flags.fin = flags & 1;
	tcph->wnd = get16(&hdrbuf[14]);
	tcph->up = get16(&hdrbuf[18]);
	tcph->mss = 0;

	/* Check for option field. Only space for one is allowed, but
	 * since there's only one TCP option (MSS) this isn't a problem
	 */
	if(i < TCPLEN || hdrlen < TCPLEN)
		return -1;      /* Header smaller than legal minimum */
	if(hdrlen == TCPLEN)
		return (int)hdrlen;     /* No options, all done */

	if(hdrlen > len_p(*bpp) + TCPLEN){
		/* Remainder too short for options length specified */
		return -1;
	}
	/* Process options */
	for(i=TCPLEN; i < hdrlen;){
		switch(PULLCHAR(bpp)){
		case EOL_KIND:
			i++;
			goto eol;       /* End of options list */
		case NOOP_KIND:
			i++;
			break;
		case MSS_KIND:
			optlen = PULLCHAR(bpp);
			if(optlen == MSS_LENGTH)
				tcph->mss = pull16(bpp);
			i += optlen;
			break;
		}
	}
eol:
	/* Get rid of any padding */
	if(i < hdrlen)
		pullup(bpp,NULLCHAR,(int16)(hdrlen - i));
	return (int)hdrlen;
}
