/* @(#) $Id: axhc.c,v 1.2 2002/01/12 15:55:53 dl9sau Exp $ */

/*
 * axhc.c written by Matthias Welwarsky, DG2FEF.
 * 
 * Routines to compress TCP/IP packets according to RFC 1146 and to
 * suppress redundant retransmissions on a reliable virtual circuit.
 * Largely based on code by Van Jacobson, Phil Karn et.al. Directly
 * derived from WAMPES' slhc.c written by Dieter Deyke, DK5SG. 
 */

#ifdef	AX25_VJCOMP
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "ip.h"
#include "tcp.h"
#include "slhc.h"

static uint8 *encode(uint8 *cp,uint n);
static long decode(struct mbuf **bpp);

/* Encode a number */
static uint8 *
encode(
    uint8 *cp,
    uint n)
{
    if(n >= 256 || n == 0){
	*cp++ = 0;
	cp = put16(cp,n);
    } else {
	*cp++ = n;
    }
    return cp;
}

/* Decode a number */
static long
decode(
    struct mbuf **bpp)
{
    int x;

    x = PULLCHAR(bpp);
    if(x == 0){
	return pull16(bpp);     /* pull16 returns -1 on error */
    } else {
	return (long)x;         /* -1 if PULLCHAR returned error */
    }
}

/* Dear hacker. I assume that you have read and understood RFC 1146
 * and the original slhc_compress() procedure before tinkering with
 * this code.
 *
 * procedure is as follows:
 * 1. check if packet is TCP. return SL_TYPE_IP if not.
 * 2. check if SYN, FIN, MSS, WSCALE, TSTAMP or RST is set, or if ACK is not
 *    set. deny compression for these packets (do_compression = 0).
 * 3. try to find the appopriate slot, reuse an old one if no match is found
 * 4. attempt to compress the packet and check the following rules:
 *    - if the packet contains an old (outdated) seq and no new ack or
 *      window or urgent data, drop it (return SL_TYPE_ERROR).
 *    - if nothing changed since the last frame sent (no new seq, ack,
 *      window, urgent data, or changing TCP flags), drop it.
 *    - before dropping a packet, check if any packet made it through the
 *      filter within the last 120sec. If not, assume a packet loss and
 *      transmit the packet.
 * 5. transmit a compressed, uncompressed or regular packet, depending
 *    on do_compression and cs->deny_compression.  
 */
 
int axhc_compress(
    struct slcompress *comp,
    struct mbuf **bpp,
    int do_compression)
{
    struct cstate *ocs = &(comp->tstate[comp->xmit_oldest]);
    struct cstate *lcs = ocs;
    struct cstate *cs = lcs->next;
    uint hlen,iplen;
    struct tcp *oth;
    unsigned long deltaS, deltaA;
    uint changes = 0;
    uint8 new_seq[16];
    uint8 *cp = new_seq;
    struct tcp th;
    struct ip iph;
    struct mbuf *copy;

    /* Copy TCP/IP header, allowing for worst-case options in both
     * Using dup_p seemed to result in unexplained
     * memory leaks -- but only some of the time. Must find out why.
     */
/*      dup_p(&copy,*bpp,0,IPLEN+IP_MAXOPT+TCPLEN+TCP_MAXOPT); */
    copy = copy_p(*bpp,IPLEN+IP_MAXOPT+TCPLEN+TCP_MAXOPT);

    /* Peek at IP header */
    iplen = hlen = ntohip(&iph,&copy);

    /* Bail if this packet isn't TCP, or is an IP fragment */
    if(iph.protocol != TCP_PTCL || iph.offset != 0 || iph.flags.mf){
	/* Send as regular IP */
	if(iph.protocol != TCP_PTCL)
	    comp->sls_o_nontcp++;
	else
	    comp->sls_o_tcp++;
	free_p(&copy);
/*	puts("axhc.c: non-TCP or IP fragment");*/
	return SL_TYPE_IP;
    }
    /* Extract TCP header */
    hlen += ntohtcp(&th,&copy);
    free_p(&copy);  /* Done with copy */

    /*
     * check if packet may be compressed.
     */
    if(th.flags.syn || th.flags.fin || th.flags.rst || !th.flags.ack
       || th.flags.mss || th.flags.wscale || th.flags.tstamp){
	comp->sls_o_tcp++;
	do_compression = 0;
/*	puts("axhc.c: TCP connection stuff");*/
    }

    /* 
     * locate the connection state slot
     */
    for ( ; ; ) {
	if( iph.source == cs->cs_ip.source
	    && iph.dest == cs->cs_ip.dest
	    && th.source == cs->cs_tcp.source
	    && th.dest == cs->cs_tcp.dest)
	    goto found;

	/* if current equal oldest, at end of list */
	if ( cs == ocs )
	    break;
	lcs = cs;
	cs = cs->next;
	comp->sls_o_searches++;
    };
    /*
     * Didn't find it -- re-use oldest cstate.  Send an
     * uncompressed packet that tells the other side what
     * connection number we're using for this conversation.
     *
     * Note that since the state list is circular, the oldest
     * state points to the newest and we only need to set
     * xmit_oldest to update the lru linkage.
     */
    comp->sls_o_misses++;
    comp->xmit_oldest = lcs->this;
    cs->deny_compression = 0;
/*    printf("axhc.c: new slot %d\n", cs->this);*/
    goto uncompressed;

 found:
    /*
     * Found it -- move to the front on the connection list.
     */
    if(lcs == ocs) {
	/* found at most recently used */
    } else if (cs == ocs) {
	/* found at least recently used */
	comp->xmit_oldest = lcs->this;
    } else {
	/* more than 2 elements */
	lcs->next = cs->next;
	cs->next = ocs->next;
	ocs->next = cs;
    }
/*    printf("axhc.c: found slot %d\n", cs->this); */
	
    /*
     * Make sure that only what we expect to change changed.
     * Check the following:
     * IP protocol version, header length & type of service.
     * The "Don't fragment" bit.
     * The time-to-live field.
     * The TCP header length.
     * IP options, if any.
     * TCP options, if any.
     * If any of these things are different between the previous &
     * current datagram, we send the current datagram `uncompressed'.
     */
    oth = &cs->cs_tcp;

    if (iph.version != cs->cs_ip.version || iph.optlen != cs->cs_ip.optlen
	|| iph.tos != cs->cs_ip.tos
	|| iph.flags.df != cs->cs_ip.flags.df
	|| iph.ttl != cs->cs_ip.ttl
	|| (iph.optlen > 0 && memcmp(iph.options,cs->cs_ip.options,iph.optlen) != 0)){
/*	puts("axhc.c: uncompressible due to unexpected changes");*/
	goto uncompressed;
    }
    /*
     * Figure out which of the changing fields changed.  The
     * receiver expects changes in the order: urgent, window,
     * ack, seq (the order minimizes the number of temporaries
     * needed in this section of code).
     */
    if(th.flags.urg) {
	deltaS = th.up;
	cp = encode(cp,(uint)deltaS);
	changes |= NEW_U;
    } else if(th.up != oth->up){
	/* argh! URG not set but urp changed -- a sensible
	 * implementation should never do this but RFC793
	 * doesn't prohibit the change so we have to deal
	 * with it. */
	goto uncompressed;
    }
    if((deltaS = th.wnd - oth->wnd) != 0){
	cp = encode(cp,(uint)deltaS);
	changes |= NEW_W;
/*	puts("axhc.c: window changed");*/
    }
    if((deltaA = th.ack - oth->ack) != 0L){
	if(deltaA > 0x0000ffff)
	    goto uncompressed;
	cp = encode(cp,(uint)deltaA);
	changes |= NEW_A;
/*	puts("axhc.c: ack changed");*/
    }
    if((deltaS = th.seq - oth->seq) != 0L){
	if(deltaS > 0x0000ffff) {

	    /* 
	     * - if the packet contains an old (outdated) seq and no
	     *    new ack or window or urgent data, drop it (return
	     *    SL_TYPE_ERROR).
	     */
	    if (seq_lt(th.seq, oth->seq) && !changes) {
		if (cs->lastdropped != 0) {
		    if (secclock() - cs->lastdropped > /*120*/ 1) {
/*			puts("axhc.c: timeout, possible packet loss");*/
			goto uncompressed;
		    }
		} else {
		    cs->lastdropped = secclock();
		}
/*		puts("axhc.c: dropping old data packet");*/
		return SL_TYPE_ERROR;
	    }
	    goto uncompressed;
	}
	cp = encode(cp,(uint)deltaS);
	changes |= NEW_S;
/*	puts("axhc.c: seq changed");*/
    }
	
    switch (changes) {
    case 0: /* Nothing changed. If this packet contains data and the
	     * last one didn't, this is probably a data packet following
	     * an ack (normal on an interactive connection) and we send
	     * it compressed.  Otherwise it's probably a retransmit,
	     * retransmitted ack or window probe.  Send it uncompressed
	     * in case the other side missed the compressed version.  */
	if(iph.length != cs->cs_ip.length && cs->cs_ip.length == hlen) {
/*	    puts("axhc.c: data following ack");*/
	    break;
	}

	/*
	 * MW: drop retransmitted packet. seq and ack did not change,
	 * check if flags have changed. 
	 */
	if (memcmp(&th.flags, &(oth->flags), sizeof(th.flags))) {
/*	    puts("axhc.c: tcp flags changed");*/
	    goto uncompressed;
	}
/*	puts("axhc.c: no changes detected");*/
	if (cs->lastdropped != 0) {
	    if (secclock() - cs->lastdropped > /*120*/ 1) {
/*		puts("axhc.c: timeout, possible packet loss");*/
		goto uncompressed;
	    }
	} else {
	    cs->lastdropped = secclock();
	}
/*	puts("axhc.c: dropping retransmitted packet");*/
	return SL_TYPE_ERROR;

    case SPECIAL_I:
    case SPECIAL_D:
	/* actual changes match one of our special case encodings --
	 * send packet uncompressed.
	 */
	goto uncompressed;
    case NEW_S|NEW_A:
	if(deltaS == deltaA &&
	   deltaS == cs->cs_ip.length - hlen){
	    /* special case for echoed terminal traffic */
	    changes = SPECIAL_I;
	    cp = new_seq;
	}
	break;
    case NEW_S:
	if(deltaS == cs->cs_ip.length - hlen){
	    /* special case for data xfer */
	    changes = SPECIAL_D;
	    cp = new_seq;
	}
	break;
    }

    /*
     * The Packet contains new information, it has not been dropped
     * until here. But compression has been denied, so we transmit an
     * uncompressed packet instead.
     */
    if (cs->deny_compression) {
/*	puts("axhc.c: compression denied by rule");*/
	goto uncompressed;
    }

    deltaS = iph.id - cs->cs_ip.id;
    if(deltaS != 1){
	cp = encode(cp,(uint)deltaS);
	changes |= NEW_I;
    }
    if(th.flags.psh)
	changes |= TCP_PUSH_BIT;
    /* Grab the cksum before we overwrite it below.  Then update our
     * state with this packet's header.
     */
    deltaA = th.checksum;
    ASSIGN(cs->cs_ip,iph);
    ASSIGN(cs->cs_tcp,th);
    cs->lastdropped = 0;

    /*
     * MW: We don't actually perform the compression if we run on an
     * uncompressible stream. 
     */
    if (!do_compression) {
/*	puts("axhc.c: sending regular IP");*/
	cs->deny_compression = 1;
	return SL_TYPE_IP;
    }

    /* We want to use the original packet as our compressed packet.
     * (cp - new_seq) is the number of bytes we need for compressed
     * sequence numbers.  In addition we need one byte for the change
     * mask, one for the connection id and two for the tcp checksum.
     * So, (cp - new_seq) + 4 bytes of header are needed.
     */
    deltaS = cp - new_seq;
    pullup(bpp,NULL,hlen);          /* Strip TCP/IP headers */
    if(comp->xmit_current != cs->this){
	pushdown(bpp,NULL,(uint)(deltaS + 4));
	cp = (*bpp)->data;
	*cp++ = changes | NEW_C;
	*cp++ = cs->this;
	comp->xmit_current = cs->this;
    } else {
	pushdown(bpp,NULL,(uint)(deltaS + 3));
	cp = (*bpp)->data;
	*cp++ = changes;
    }
    cp = put16(cp,(uint)deltaA);    /* Write TCP checksum */
    memcpy(cp,new_seq,(uint)deltaS);        /* Write list of deltas */
    comp->sls_o_compressed++;
/*    puts("axhc.c: sending compressed packet");*/
    return SL_TYPE_COMPRESSED_TCP;

    /* Update connection state cs & send uncompressed packet (i.e.,
     * a regular ip/tcp packet but with the 'conversation id' we hope
     * to use on future compressed packets in the protocol field).
     */
 uncompressed:
    iph.protocol = cs->this;
    ASSIGN(cs->cs_ip,iph);
    ASSIGN(cs->cs_tcp,th);
    comp->xmit_current = cs->this;
    cs->lastdropped = 0;

    if (!do_compression) {
/*	puts("axhc.c: sending regular IP");*/
	cs->deny_compression = 1;
	return SL_TYPE_IP;
    }
    
    cs->deny_compression = 0;
    comp->sls_o_uncompressed++;
    pullup(bpp,NULL,iplen); /* Strip old IP header */
    htonip(&iph,bpp,IP_CS_OLD);     /* replace with new one */
/*    puts("axhc.c: sending uncompressed packet");*/
    return SL_TYPE_UNCOMPRESSED_TCP;
}

#endif
