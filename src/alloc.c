/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/alloc.c,v 1.20 1994/01/26 11:11:30 deyke Exp $ */

/* memory allocation routines
 */

#define malloc  Xmalloc
#define free    Xfree
#define realloc Xrealloc
#define calloc  Xcalloc

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#undef malloc
#undef free
#undef realloc
#undef calloc

#ifndef PURIFY

#include "global.h"
#include "mbuf.h"

#define DEBUGLEVEL      1

#define ALLOCSIZE       0x8000
#define FREEPATTERN     0xbb
#define FREETABLESIZE   2048
#define MEMFULL         ((struct block *) (-1))
#define MINSIZE         16
#define SBRK(i)         ((struct block *) sbrk((int) (i)))
#define USEDPATTERN     0xdd

struct block {
  struct block *next;
};

static struct block Freetable[FREETABLESIZE];
static unsigned long Memfail;   /* Count of allocation failures */
static unsigned long Allocs;    /* Total allocations */
static unsigned long Frees;     /* Total frees */
static unsigned long Invalid;   /* Total calls to free with garbage arg */
static unsigned long Heapsize;
static unsigned long Inuse;
static unsigned long Morecores;

static void giveup(const char *mesg);

/*---------------------------------------------------------------------------*/

static void giveup(mesg)
const char *mesg;
{
  fprintf(stderr, mesg);
  abort();
}

/*---------------------------------------------------------------------------*/

/* Allocate block of 'size' bytes */
void *
malloc(size)
unsigned size;
{

  static struct block *freespace;
  static unsigned int freesize;

  int align_error;
  register struct block *p, *tp;

  size = (size + sizeof(struct block *) + MINSIZE - 1) & ~(MINSIZE - 1);
  if ((tp = Freetable + size / MINSIZE) >= Freetable + FREETABLESIZE)
    giveup("malloc: size too large\n");
  if (p = tp->next)
    tp->next = p->next;
  else {
    if (size > freesize) {
      if (freesize) {
	struct block *tpf = Freetable + freesize / MINSIZE;
	freespace->next = tpf->next;
	tpf->next = freespace;
	freesize = 0;
      }
      if ((freespace = SBRK(ALLOCSIZE)) == MEMFULL) {
	Memfail++;
	return 0;
      }
      Morecores++;
      Heapsize += (freesize = ALLOCSIZE);
      if (align_error = (MINSIZE - 1) & -(((int) freespace) + sizeof(struct block *))) {
	freespace = (struct block *) (align_error + (int) freespace);
	freesize -= MINSIZE;
      }
    }
    p = freespace;
    freespace += (size / sizeof(struct block *));
    freesize -= size;
  }
#if DEBUGLEVEL >= 2
  memset(p, USEDPATTERN, size);
#endif
  p->next = tp;
  Allocs++;
  Inuse += size;
  return (void *) (p + 1);
}

/*---------------------------------------------------------------------------*/

/* Put memory block back on heap */
void
free(pp)
void *pp;
{
  register struct block *p, *tp;

  if (p = (struct block *) pp) {
    if ((MINSIZE - 1) & (int) p) {
#if DEBUGLEVEL >= 1
      giveup("free: bad alignment\n");
#endif
      Invalid++;
      return;
    }
    p--;
    tp = p->next;
    if (tp < Freetable || tp >= Freetable + FREETABLESIZE) {
#if DEBUGLEVEL >= 1
      giveup("free: bad free table pointer\n");
#endif
      Invalid++;
      return;
    }
#if DEBUGLEVEL >= 2
    memset(p, FREEPATTERN, (tp - Freetable) * MINSIZE);
#endif
    Frees++;
    Inuse -= (tp - Freetable) * MINSIZE;
    p->next = tp->next;
    tp->next = p;
  }
}

/*---------------------------------------------------------------------------*/

/* Move existing block to new area */
void *
realloc(p,size)
void *p;
unsigned size;
{

  struct block *tp;
  unsigned osize;
  void *q;

  if (!p)
    return malloc(size);

  if (!size) {
    free(p);
    return 0;
  }

  tp = p;
  tp--;
  osize = (tp->next - Freetable) * MINSIZE - sizeof(struct block *);
  if (size <= osize) return p;
  if (q = malloc(size)) {
    memcpy(q, p, osize);
    free(p);
  }
  return q;
}

/*---------------------------------------------------------------------------*/

/* Allocate block of cleared memory */
void *
calloc(nelem,size)
unsigned nelem; /* Number of elements */
unsigned size;  /* Size of each element */
{

	register unsigned i;
	register char *cp;

	i = nelem * size;
	if((cp = malloc(i)) != NULL)
		memset(cp,0,i);
	return cp;
}

#endif

/*---------------------------------------------------------------------------*/

void *mallocw(size)
unsigned int size;
{
  return malloc(size);
}

/*---------------------------------------------------------------------------*/

void *callocw(nelem, elsize)
unsigned int nelem, elsize;
{
  return calloc(nelem, elsize);
}

/*---------------------------------------------------------------------------*/

/* Return 0 if at least Memthresh memory is available. Return 1 if
 * less than Memthresh but more than Memthresh/2 is available; i.e.,
 * if a yellow garbage collection should be performed. Return 2 if
 * less than Memthresh/2 is available, i.e., a red collection should
 * be performed.
 */
int
availmem()
{
		return 0;       /* We're clearly OK */
}

/*---------------------------------------------------------------------------*/

/* Print heap stats */
int domem(argc,argv,envp)
int argc;
char *argv[];
void *envp;
{
#ifndef PURIFY
	printf("heap size %lu avail %lu (%lu%%) morecores %lu\n",
	 Heapsize,Heapsize-Inuse,100L*(Heapsize-Inuse)/Heapsize,
	 Morecores);
	printf("allocs %lu frees %lu (diff %lu) alloc fails %lu invalid frees %lu\n",
		Allocs,Frees,Allocs-Frees,Memfail,Invalid);
	printf("pushdown calls %lu pushdown calls to malloc %lu\n",
		Pushdowns,Pushalloc);
#endif
	return 0;
}
