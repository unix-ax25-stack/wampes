/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mbuf.c,v 1.3 1990/08/23 17:33:35 deyke Exp $ */

/* Primitive mbuf allocate/free routines */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"

/* Allocate mbuf with associated buffer of 'size' bytes. If interrupts
 * are enabled, use the regular heap. If they're off, use the special
 * interrupt buffer pool.
 */
struct mbuf *
alloc_mbuf(size)
register int16 size;
{
	register struct mbuf *bp;

	bp = (struct mbuf *)malloc((unsigned)(size + sizeof(struct mbuf)));

	if(bp == NULLBUF)
		return NULLBUF;
	bp->next = bp->anext = NULLBUF;
	if((bp->size = size) != 0){
		bp->data = (char *)(bp + 1);
	} else {
		bp->data = NULLCHAR;
	}
	bp->refcnt = 1;
	bp->dup = NULLBUF;
	bp->cnt = 0;
	return bp;
}

/* Decrement the reference pointer in an mbuf. If it goes to zero,
 * free all resources associated with mbuf.
 * Return pointer to next mbuf in packet chain
 */
struct mbuf *
free_mbuf(bp)
register struct mbuf *bp;
{
	register struct mbuf *bp1 = NULLBUF;

	if(bp != NULLBUF){
		bp1 = bp->next;
		/* Follow indirection, if any */
		free_mbuf(bp->dup);

		if(--bp->refcnt == 0){
			free((char *)bp);
		}
	}
	return bp1;
}

/* Free packet (a chain of mbufs). Return pointer to next packet on queue,
 * if any
 */
struct mbuf *
free_p(bp)
register struct mbuf *bp;
{
	register struct mbuf *abp;

	if(bp == NULLBUF)
		return NULLBUF;
	abp = bp->anext;
	while(bp != NULLBUF)
		bp = free_mbuf(bp);
	return abp;
}
/* Free entire queue of packets (of mbufs) */
void
free_q(q)
struct mbuf **q;
{
	register struct mbuf *bp;

	while((bp = dequeue(q)) != NULLBUF)
		free_p(bp);
}

/* Count up the total number of bytes in a packet */
int16
len_p(bp)
register struct mbuf *bp;
{
	register int16 cnt = 0;

	while(bp != NULLBUF){
		cnt += bp->cnt;
		bp = bp->next;
	}
	return cnt;
}
/* Count up the number of packets in a queue */
int16
len_q(bp)
register struct mbuf *bp;
{
	register int16 cnt;

	for(cnt=0;bp != NULLBUF;cnt++,bp = bp->anext)
		;
	return cnt;
}
/* Trim mbuf to specified length by lopping off end */
void
trim_mbuf(bpp,length)
struct mbuf **bpp;
int16 length;
{
	register int16 tot = 0;
	register struct mbuf *bp;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return; /* Nothing to trim */

	if(length == 0){
		/* Toss the whole thing */
		free_p(*bpp);
		*bpp = NULLBUF;
		return;
	}
	/* Find the point at which to trim. If length is greater than
	 * the packet, we'll just fall through without doing anything
	 */
	for( bp = *bpp; bp != NULLBUF; bp = bp->next){
		if(tot + bp->cnt < length){
			tot += bp->cnt;
		} else {
			/* Cut here */
			bp->cnt = length - tot;
			free_p(bp->next);
			bp->next = NULLBUF;
			break;
		}
	}
}
/* Duplicate/enqueue/dequeue operations based on mbufs */

/* Duplicate first 'cnt' bytes of packet starting at 'offset'.
 * This is done without copying data; only the headers are duplicated,
 * but without data segments of their own. The pointers are set up to
 * share the data segments of the original copy. The return pointer is
 * passed back through the first argument, and the return value is the
 * number of bytes actually duplicated.
 */
int16
dup_p(hp,bp,offset,cnt)
struct mbuf **hp;
register struct mbuf *bp;
register int16 offset;
register int16 cnt;
{
	register struct mbuf *cp;
	int16 tot;

	if(cnt == 0 || bp == NULLBUF || hp == NULLBUFP){
		if(hp != NULLBUFP)
			*hp = NULLBUF;
		return 0;
	}
	if((*hp = cp = alloc_mbuf(0)) == NULLBUF){
		return 0;
	}
	/* Skip over leading mbufs that are smaller than the offset */
	while(bp != NULLBUF && bp->cnt <= offset){
		offset -= bp->cnt;
		bp = bp->next;
	}
	if(bp == NULLBUF){
		free_mbuf(cp);
		*hp = NULLBUF;
		return 0;       /* Offset was too big */
	}
	tot = 0;
	for(;;){
		/* Make sure we get the original, "real" buffer (i.e. handle the
		 * case of duping a dupe)
		 */
		if(bp->dup != NULLBUF)
			cp->dup = bp->dup;
		else
			cp->dup = bp;

		/* Increment the duplicated buffer's reference count */
		cp->dup->refcnt++;

		cp->data = bp->data + offset;
		cp->cnt = min(cnt,bp->cnt - offset);
		offset = 0;
		cnt -= cp->cnt;
		tot += cp->cnt;
		bp = bp->next;
		if(cnt == 0 || bp == NULLBUF || (cp->next = alloc_mbuf(0)) == NULLBUF)
			break;
		cp = cp->next;
	}
	return tot;
}
/* Copy first 'cnt' bytes of packet into a new, single mbuf */
struct mbuf *
copy_p(bp,cnt)
register struct mbuf *bp;
register int16 cnt;
{
	register struct mbuf *cp;
	register char *wp;
	register int16 n;

	if(bp == NULLBUF || cnt == 0 || (cp = alloc_mbuf(cnt)) == NULLBUF)
		return NULLBUF;
	wp = cp->data;
	while(cnt != 0 && bp != NULLBUF){
		n = min(cnt,bp->cnt);
		memcpy(wp,bp->data,n);
		wp += n;
		cp->cnt += n;
		cnt -= n;
		bp = bp->next;
	}
	return cp;
}
/* Copy and delete "cnt" bytes from beginning of packet. Return number of
 * bytes actually pulled off
 */
int16
pullup(bph,buf,cnt)
struct mbuf **bph;
char *buf;
int16 cnt;
{
	register struct mbuf *bp;
	int16 n,tot;

	tot = 0;
	if(bph == NULLBUFP)
		return 0;
	while(*bph != NULLBUF && cnt != 0){
		bp = *bph;
		n = min(cnt,bp->cnt);
		if(buf != NULLCHAR){
			if(n == 1)      /* Common case optimization */
				*buf = *bp->data;
			else if(n > 1)
				memcpy(buf,bp->data,n);
			buf += n;
		}
		tot += n;
		cnt -= n;
		bp->data += n;
		bp->cnt -= n;
		if(bp->cnt == 0){
			/* If this is the last mbuf of a packet but there
			 * are others on the queue, return a pointer to
			 * the next on the queue. This allows pullups to
			 * to work on a packet queue
			 */
			if(bp->next == NULLBUF && bp->anext != NULLBUF){
				*bph = bp->anext;
				free_mbuf(bp);
			} else
				*bph = free_mbuf(bp);
		}
	}
	return tot;
}
/* Append mbuf to end of mbuf chain */
void
append(bph,bp)
struct mbuf **bph;
struct mbuf *bp;
{
	register struct mbuf *p;

	if(bph == NULLBUFP || bp == NULLBUF)
		return;
	if(*bph == NULLBUF){
		/* First one on chain */
		*bph = bp;
	} else {
		for(p = *bph ; p->next != NULLBUF ; p = p->next)
			;
		p->next = bp;
	}
}
/* Insert specified amount of contiguous new space at the beginning of an
 * mbuf chain. If enough space is available in the first mbuf, no new space
 * is allocated. Otherwise a mbuf of the appropriate size is allocated and
 * tacked on the front of the chain.
 *
 * This operation is the logical inverse of pullup(), hence the name.
 */
struct mbuf *
pushdown(bp,size)
register struct mbuf *bp;
int16 size;
{
	register struct mbuf *nbp;

	/* Check that bp is real, that it hasn't been duplicated, and
	 * that it itself isn't a duplicate before checking to see if
	 * there's enough space at its front.
	 */
	if(bp != NULLBUF && bp->refcnt == 1 && bp->size != 0
	 && bp->data - (char *)(bp+1) >= size){
		/* No need to alloc new mbuf, just adjust this one */
		bp->data -= size;
		bp->cnt += size;
	} else {
		if((nbp = alloc_mbuf(size)) != NULLBUF){
			nbp->next = bp;
			nbp->cnt = size;
			bp = nbp;
		} else {
			bp = NULLBUF;
		}
	}
	return bp;
}
/* Append packet to end of packet queue */
void
enqueue(q,bp)
struct mbuf **q;
struct mbuf *bp;
{
	register struct mbuf *p;

	if(q == NULLBUFP || bp == NULLBUF)
		return;
	if(*q == NULLBUF){
		/* List is empty, stick at front */
		*q = bp;
	} else {
		for(p = *q ; p->anext != NULLBUF ; p = p->anext)
			;
		p->anext = bp;
	}
}
/* Unlink a packet from the head of the queue */
struct mbuf *
dequeue(q)
register struct mbuf **q;
{
	register struct mbuf *bp;

	if(q == NULLBUFP)
		return NULLBUF;
	if((bp = *q) != NULLBUF){
		*q = bp->anext;
		bp->anext = NULLBUF;
	}
	return bp;
}

/* Copy user data into an mbuf */
struct mbuf *
qdata(data,cnt)
char *data;
int16 cnt;
{
	register struct mbuf *bp;

	if((bp = alloc_mbuf(cnt)) == NULLBUF)
		return NULLBUF;
	memcpy(bp->data,data,cnt);
	bp->cnt = cnt;
	return bp;
}
/* Copy mbuf data into user buffer */
int16
dqdata(bp,buf,cnt)
struct mbuf *bp;
char *buf;
unsigned cnt;
{
	int16 tot;
	unsigned n;
	struct mbuf *bp1;

	if(buf == NULLCHAR)
		return 0;

	tot = 0;
	for(bp1 = bp;bp1 != NULLBUF; bp1 = bp1->next){
		n = min(bp1->cnt,cnt);
		memcpy(buf,bp1->data,n);
		cnt -= n;
		buf += n;
		tot += n;
	}
	free_p(bp);
	return tot;
}
/* Pull a 32-bit integer in host order from buffer in network byte order */
int32
pull32(bpp)
struct mbuf **bpp;
{
	char buf[4];

	if(pullup(bpp,buf,4) != 4){
		/* Return zero if insufficient buffer */
		return 0;
	}
	return get32(buf);
}
/* Pull a 16-bit integer in host order from buffer in network byte order */
int16
pull16(bpp)
struct mbuf **bpp;
{
	char buf[2];

	if(pullup(bpp,buf,2) != 2){
		/* Return zero if insufficient buffer */
		return 0;
	}
	return get16(buf);
}
/* Pull single character from mbuf */
char
pullchar(bpp)
struct mbuf **bpp;
{
	char c;

	if(pullup(bpp,&c,1) != 1)
		/* Return zero if nothing left */
		c = 0;
	return c;
}
int
write_p(fp,bp)
FILE *fp;
struct mbuf *bp;
{
	while(bp != NULLBUF){
		if(fwrite(bp->data,1,bp->cnt,fp) != bp->cnt)
			return -1;
		bp = bp->next;
	}
	return 0;
}
