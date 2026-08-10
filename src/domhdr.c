/* @(#) $Id: domhdr.c,v 1.12 1996/08/19 16:30:14 deyke Exp $ */

/* Domain header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "domain.h"

#define dn_expand Xdn_expand    /* Resolve name conflict */

#define DOMHDRLEN       12      /* Fixed part of a domain message header */
#define NAMELEN         512     /* Scratch buffer for a decoded domain name */

static int dn_expand(uint8 *msg,uint8 *eom,uint8 *compressed,char *full,
	int fullen);
static uint8 *getq(struct rr **rrpp,uint8 *msg,uint8 *eom,uint8 *cp);
static uint8 *ntohrr(struct rr **rrpp,uint8 *msg,uint8 *eom,uint8 *cp);

static uint8 *putstring(uint8 *cp, uint8 *end, const char *str);
static uint8 *putname(uint8 *buffer, uint8 *cp, uint8 *end, const char *name);
static uint8 *putq(uint8 *buffer, uint8 *cp, uint8 *end, const struct rr *rrp,
	int *count, int *trunc);
static uint8 *putrr(uint8 *buffer, uint8 *cp, uint8 *end, const struct rr *rrp,
	int *count, int *trunc);

int
ntohdomain(struct dhdr *dhdr,struct mbuf **bpp)
{
	uint tmp,len;
	uint i;
	uint8 *msg,*cp,*eom;
	struct rr **rrpp;

	memset(dhdr,0,sizeof(struct dhdr));

	/* A message shorter than the fixed 12-byte header is not one.  The old
	 * code read msg[0..11] regardless of how much had actually arrived.
	 */
	len = len_p(*bpp);
	if(len < DOMHDRLEN)
		return -1;
	msg = (uint8 *) mallocw(len);
	pullup(bpp,msg,len);
	eom = msg + len;

	dhdr->id = get16(&msg[0]);
	tmp = get16(&msg[2]);
	if(tmp & 0x8000)
		dhdr->qr = 1;
	dhdr->opcode = (tmp >> 11) & 0xf;
	if(tmp & 0x0400)
		dhdr->aa = 1;
	if(tmp & 0x0200)
		dhdr->tc = 1;
	if(tmp & 0x0100)
		dhdr->rd = 1;
	if(tmp & 0x0080)
		dhdr->ra = 1;
	dhdr->rcode = tmp & 0xf;
	dhdr->qdcount = get16(&msg[4]);
	dhdr->ancount = get16(&msg[6]);
	dhdr->nscount = get16(&msg[8]);
	dhdr->arcount = get16(&msg[10]);

	/* Now parse the variable length sections.  The counts above are taken
	 * from the wire and are not to be trusted: the loops below stop as soon
	 * as a record does not fit, so a small datagram claiming 65535 records
	 * costs one failed parse rather than 65535 allocations.
	 */
	cp = msg + DOMHDRLEN;

	/* Question section */
	rrpp = &dhdr->questions;
	for(i=0;i<dhdr->qdcount;i++){
		if((cp = getq(rrpp,msg,eom,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_QUESTION;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Answer section */
	rrpp = &dhdr->answers;
	for(i=0;i<dhdr->ancount;i++){
		if((cp = ntohrr(rrpp,msg,eom,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_ANSWER;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Name server (authority) section */
	rrpp = &dhdr->authority;
	for(i=0;i<dhdr->nscount;i++){
		if((cp = ntohrr(rrpp,msg,eom,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_AUTHORITY;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;

	/* Additional section */
	rrpp = &dhdr->additional;
	for(i=0;i<dhdr->arcount;i++){
		if((cp = ntohrr(rrpp,msg,eom,cp)) == NULL){
			free(msg);
			return -1;
		}
		(*rrpp)->source = RR_ADDITIONAL;
		rrpp = &(*rrpp)->next;
	}
	*rrpp = NULL;
	free(msg);
	return 0;
}
static uint8 *
getq(struct rr **rrpp,uint8 *msg,uint8 *eom,uint8 *cp)
{
	struct rr *rrp;
	int len;
	char *name;

	*rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
	name = (char *) mallocw(NAMELEN);
	len = dn_expand(msg,eom,cp,name,NAMELEN);
	if(len == -1){
		free(name);
		return NULL;
	}
	cp += len;
	if(cp + 4 > eom){       /* type and class */
		free(name);
		return NULL;
	}
	rrp->name = strdup(name);
	rrp->type = get16(cp);
	cp += 2;
	rrp->class = get16(cp);
	cp += 2;
	rrp->ttl = 0;
	rrp->rdlength = 0;
	free(name);
	return cp;
}
/* Read a resource record from a domain message into a host structure */
static uint8 *
ntohrr(
struct rr **rrpp, /* Where to allocate resource record structure */
uint8 *msg,     /* Pointer to beginning of domain message */
uint8 *eom,     /* First byte past the end of the message */
uint8 *cp)      /* Pointer to start of encoded RR record */
{
	struct rr *rrp;
	int len;
	char *name;
	uint8 *rdend;   /* First byte past the rdata field */

	*rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
	name = (char *) mallocw(NAMELEN);
	if((len = dn_expand(msg,eom,cp,name,NAMELEN)) == -1)
		goto Bad;
	cp += len;
	if(cp + 10 > eom)       /* type, class, ttl, rdlength */
		goto Bad;
	rrp->name = strdup(name);
	rrp->type = get16(cp);
	cp += 2;
	rrp->class = get16(cp);
	cp+= 2;
	rrp->ttl = get32(cp);
	cp += 4;
	rrp->rdlength = get16(cp);
	cp += 2;

	/* Everything below has to stay inside the rdata field.  Remember where
	 * it ends, bound the individual reads by it, and resynchronise on it
	 * afterwards - that way a record whose contents disagree with its own
	 * rdlength cannot desynchronise the rest of the message.
	 */
	rdend = cp + rrp->rdlength;
	if(rdend > eom)
		goto Bad;

	switch(rrp->type){
	case TYPE_A:
		/* Just read the address directly into the structure */
		if(cp + 4 > rdend)
			goto Bad;
		rrp->rdata.addr = get32(cp);
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
		/* These types all consist of a single domain name;
		 * convert it to ascii format
		 */
		len = dn_expand(msg,eom,cp,name,NAMELEN);
		if(len == -1 || cp + len > rdend)
			goto Bad;
		rrp->rdata.name = strdup(name);
		rrp->rdlength = strlen(name);
		break;
	case TYPE_HINFO:
		if(cp >= rdend)
			goto Bad;
		len = *cp++;
		if(cp + len > rdend)
			goto Bad;
		rrp->rdata.hinfo.cpu = (char *) mallocw(len+1);
		memcpy( rrp->rdata.hinfo.cpu, cp, len );
		rrp->rdata.hinfo.cpu[len] = '\0';
		cp += len;

		if(cp >= rdend)
			goto Bad;
		len = *cp++;
		if(cp + len > rdend)
			goto Bad;
		rrp->rdata.hinfo.os = (char *) mallocw(len+1);
		memcpy( rrp->rdata.hinfo.os, cp, len );
		rrp->rdata.hinfo.os[len] = '\0';
		break;
	case TYPE_MX:
		if(cp + 2 > rdend)
			goto Bad;
		rrp->rdata.mx.pref = get16(cp);
		cp += 2;
		/* Get domain name of exchanger */
		len = dn_expand(msg,eom,cp,name,NAMELEN);
		if(len == -1 || cp + len > rdend)
			goto Bad;
		rrp->rdata.mx.exch = strdup(name);
		break;
	case TYPE_SOA:
		/* Get domain name of name server */
		len = dn_expand(msg,eom,cp,name,NAMELEN);
		if(len == -1 || cp + len > rdend)
			goto Bad;
		rrp->rdata.soa.mname = strdup(name);
		cp += len;

		/* Get domain name of responsible person */
		len = dn_expand(msg,eom,cp,name,NAMELEN);
		if(len == -1 || cp + len > rdend)
			goto Bad;
		rrp->rdata.soa.rname = strdup(name);
		cp += len;

		if(cp + 20 > rdend)
			goto Bad;
		rrp->rdata.soa.serial = get32(cp);
		cp += 4;
		rrp->rdata.soa.refresh = get32(cp);
		cp += 4;
		rrp->rdata.soa.retry = get32(cp);
		cp += 4;
		rrp->rdata.soa.expire = get32(cp);
		cp += 4;
		rrp->rdata.soa.minimum = get32(cp);
		break;
	case TYPE_TXT:
		if(cp >= rdend)
			goto Bad;
		len = *cp++;
		if(cp + len > rdend)
			goto Bad;
		rrp->rdata.name = (char *) mallocw(len+1);
		memcpy(rrp->rdata.name,cp,len);
		rrp->rdata.name[len] = '\0';
		break;
	default:
		/* Ignore */
		break;
	}
	free(name);
	/* rdend, not rdata + rrp->rdlength: the CNAME/PTR case above replaces
	 * rdlength with the length of the decoded name. */
	return rdend;

Bad:
	free(name);
	return NULL;
}

/* Convert a compressed domain name to the human-readable form */
static int
dn_expand(
uint8 *msg,             /* Complete domain message */
uint8 *eom,
uint8 *compressed,      /* Pointer to compressed name */
char *full,             /* Pointer to result buffer */
int fullen)             /* Length of same */
{
	unsigned int slen;      /* Length of current segment */
	uint8 *cp;
	int clen = 0;   /* Total length of compressed name */
	int indirect = 0;       /* Set if indirection encountered */
	int nseg = 0;           /* Total number of segments in name */
	int njumps = 0;         /* Compression pointers followed */

	/* The eom argument used to be passed as NULL by every caller and was
	 * never looked at, so nothing here was bounded: a compression pointer
	 * can name any offset up to 16383 and would be followed straight out of
	 * the message buffer, copying whatever was there into the result - and
	 * the result is handed back to the peer or put into the cache.  A cycle
	 * of pointers had nothing to stop it either.
	 */
	if(msg == NULL || eom == NULL || compressed == NULL
	 || full == NULL || fullen <= 0)
		return -1;

	cp = compressed;
	for(;;){
		if(cp < msg || cp >= eom)
			return -1;
		slen = *cp++;   /* Length of this segment */
		if(!indirect)
			clen++;
		while((slen & 0xc0) == 0xc0){
			if(cp >= eom)
				return -1;
			if(!indirect)
				clen++;         /* the pointer costs two bytes here */
			indirect = 1;
			/* Follow indirection.  A pointer may name any offset in the
			 * message, including one that leads back here, so cap the
			 * number of jumps rather than trusting the encoding.
			 */
			if(++njumps > 128)
				return -1;
			cp = &msg[((slen & 0x3f)<<8) + *cp];
			if(cp < msg || cp >= eom)
				return -1;
			slen = *cp++;
		}
		if(slen == 0)   /* zero length == all done */
			break;
		if(slen > 63)   /* not a legal label length */
			return -1;
		if(cp + slen > eom)
			return -1;
		fullen -= slen + 1;
		if(fullen < 0)
			return -1;
		if(!indirect)
			clen += slen;
		while(slen-- != 0)
			*full++ = (char)*cp++;
		*full++ = '.';
		nseg++;
	}
	if(nseg == 0){
		/* Root name; represent as single dot */
		if(--fullen < 0)
			return -1;
		*full++ = '.';
	}
	if(--fullen < 0)
		return -1;
	*full++ = '\0';
	return clen;    /* Length of compressed message */
}

/*---------------------------------------------------------------------------*/

/* Largest message htondomain() will build.  512 is the RFC 1035 limit for
 * UDP; the TCP path in domain.c only prepends a length word, so the same
 * bound is safe there.  Whatever does not fit is left out and the TC bit is
 * set, which is what a resolver expects.
 */
#define DOMAIN_MAXMSG   512

static struct compress_table {
  const char *name;
  int offset;
} Compress_table[128];

#define NCOMPRESS       ((int) (sizeof Compress_table / sizeof Compress_table[0]))

/*---------------------------------------------------------------------------*/

/* Every put* routine below takes the end of the output buffer and returns
 * NULL when the item would not fit.  putq() and putrr() turn that into a
 * rollback to the last record boundary, so a truncated message is still well
 * formed.  Before this, the whole RR list was serialized into a fixed buffer
 * with no bound at all: a query carrying enough questions was all it took to
 * write past the mbuf.
 */

static uint8 *putstring(
uint8 *cp,
uint8 *end,
const char *str)
{
  size_t len;

  len = strlen(str);
  if (len > 255) len = 255;             /* DNS character-string limit */
  if (cp + 1 + len > end) return NULL;
  *cp++ = (uint8) len;
  memcpy(cp, str, len);
  return cp + len;
}

/*---------------------------------------------------------------------------*/

static uint8 *putname(
uint8 *buffer,
uint8 *cp,
uint8 *end,
const char *name)
{

  const char *cp1;
  int i;
  int len;

  for (; ; ) {
    for (i = 0; i < NCOMPRESS && Compress_table[i].name; i++)
      if (!strcmp(Compress_table[i].name, name)) {
	if (cp + 2 > end) return NULL;
	return put16(cp, 0xc000 | Compress_table[i].offset);
      }
    for (cp1 = name; *cp1 && *cp1 != '.'; cp1++) ;
    len = cp1 - name;
    if (len > 63) return NULL;          /* longer than a DNS label may be */
    if (cp + 1 + len > end) return NULL;
    if (!(*cp++ = len)) break;          /* root label terminates the name */
    /* Remember this suffix for compression - but only while there is room
     * left in the table and the offset still fits the 14 bits of a pointer.
     * The old code let i run past the end of Compress_table.
     */
    if (i < NCOMPRESS - 1 && cp - buffer - 1 < 0x4000) {
      Compress_table[i].name = name;
      Compress_table[i].offset = cp - buffer - 1;
      Compress_table[i+1].name = 0;
    }
    while (name < cp1) *cp++ = *name++;
    if (*name) name++;
  }
  return cp;
}

/*---------------------------------------------------------------------------*/

static uint8 *putq(
uint8 *buffer,
uint8 *cp,
uint8 *end,
const struct rr *rrp,
int *count,
int *trunc)
{
  uint8 *mark;

  *count = 0;
  for (; rrp; rrp = rrp->next) {
    mark = cp;
    if (!(cp = putname(buffer, cp, end, rrp->name)) || cp + 4 > end) {
      *trunc = 1;
      return mark;
    }
    cp = put16(cp, rrp->type);
    cp = put16(cp, rrp->class);
    (*count)++;
  }
  return cp;
}

/*---------------------------------------------------------------------------*/

static uint8 *putrr(
uint8 *buffer,
uint8 *cp,
uint8 *end,
const struct rr *rrp,
int *count,
int *trunc)
{
  uint8 *cp1;
  uint8 *mark;

  *count = 0;
  for (; rrp; rrp = rrp->next) {
    mark = cp;
    /* name + type + class + ttl + the rdlength word */
    if (!(cp = putname(buffer, cp, end, rrp->name)) || cp + 10 > end)
      goto Truncated;
    cp = put16(cp, rrp->type);
    cp = put16(cp, rrp->class);
    cp1 = put32(cp, rrp->ttl);
    cp = cp1 + 2;
    switch (rrp->type) {
    case TYPE_A:
      if (cp + 4 > end) goto Truncated;
      cp = put32(cp, rrp->rdata.addr);
      break;
    case TYPE_CNAME:
    case TYPE_MB:
    case TYPE_MG:
    case TYPE_MR:
    case TYPE_NS:
    case TYPE_PTR:
      if (!(cp = putname(buffer, cp, end, rrp->rdata.name))) goto Truncated;
      break;
    case TYPE_HINFO:
      if (!(cp = putstring(cp, end, rrp->rdata.hinfo.cpu)) ||
	  !(cp = putstring(cp, end, rrp->rdata.hinfo.os))) goto Truncated;
      break;
    case TYPE_MX:
      if (cp + 2 > end) goto Truncated;
      cp = put16(cp, rrp->rdata.mx.pref);
      if (!(cp = putname(buffer, cp, end, rrp->rdata.mx.exch))) goto Truncated;
      break;
    case TYPE_SOA:
      if (!(cp = putname(buffer, cp, end, rrp->rdata.soa.mname)) ||
	  !(cp = putname(buffer, cp, end, rrp->rdata.soa.rname)) ||
	  cp + 20 > end) goto Truncated;
      cp = put32(cp, rrp->rdata.soa.serial);
      cp = put32(cp, rrp->rdata.soa.refresh);
      cp = put32(cp, rrp->rdata.soa.retry);
      cp = put32(cp, rrp->rdata.soa.expire);
      cp = put32(cp, rrp->rdata.soa.minimum);
      break;
    case TYPE_TXT:
      if (!(cp = putstring(cp, end, rrp->rdata.name))) goto Truncated;
      break;
    default:
      break;
    }
    put16(cp1, cp - cp1 - 2);
    (*count)++;
    continue;

Truncated:
    /* This record does not fit.  Roll back to the record boundary and stop;
     * the caller sets TC and reports the count that actually went out.
     */
    *trunc = 1;
    return mark;
  }
  return cp;
}

/*---------------------------------------------------------------------------*/

struct mbuf *htondomain(
const struct dhdr *dhp)
{

  int tmp;
  int trunc = 0;
  int nqd = 0, nan = 0, nns = 0, nar = 0;
  struct mbuf *bp;
  uint8 *cp;
  uint8 *end;

  Compress_table[0].name = 0;
  bp = alloc_mbuf(DOMAIN_MAXMSG);
  if (!bp) return 0;
  end = bp->data + DOMAIN_MAXMSG;

  /* Body first, header afterwards: the counts in the header have to be the
   * number of records that actually fit, not the number we were handed.
   * Once a section truncates, stop - the compression table would otherwise
   * hand out offsets into a rolled-back part of the buffer.
   */
  cp = bp->data + 12;
  cp = putq(bp->data, cp, end, dhp->questions, &nqd, &trunc);
  if (!trunc) cp = putrr(bp->data, cp, end, dhp->answers,    &nan, &trunc);
  if (!trunc) cp = putrr(bp->data, cp, end, dhp->authority,  &nns, &trunc);
  if (!trunc) cp = putrr(bp->data, cp, end, dhp->additional, &nar, &trunc);

  tmp = 0;
  if (dhp->qr) tmp |= 0x8000;
  tmp |= (dhp->opcode & 0xf) << 11;
  if (dhp->aa) tmp |= 0x0400;
  if (dhp->tc || trunc) tmp |= 0x0200;
  if (dhp->rd) tmp |= 0x0100;
  if (dhp->ra) tmp |= 0x0080;
  tmp |= (dhp->rcode & 0xf);

  put16(bp->data,      dhp->id);
  put16(bp->data + 2,  tmp);
  put16(bp->data + 4,  nqd);
  put16(bp->data + 6,  nan);
  put16(bp->data + 8,  nns);
  put16(bp->data + 10, nar);

  bp->cnt = cp - bp->data;
  return bp;
}

