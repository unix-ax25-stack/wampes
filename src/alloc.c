/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/alloc.c,v 1.13 1992/06/01 10:34:07 deyke Exp $ */

/* memory allocation routines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"

#define DEBUG           1

#define ALLOCSIZE       0x8000
#define FREETABLESIZE   2048
#define MEMFULL         ((struct block *) (-1))
#define MINSIZE         16
#define SBRK(i)         ((struct block *) sbrk((int) (i)))

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

static void giveup __ARGS((char *mesg));

/*---------------------------------------------------------------------------*/

static void giveup(mesg)
char *mesg;
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
    giveup("malloc: requested block too large\n");
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
  p->next = tp;
  Allocs++;
  Inuse += size;
  return (void *) (p + 1);
}

/*---------------------------------------------------------------------------*/

void *_malloc(size)
unsigned int size;
{
  return malloc(size);
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
#if DEBUG
      giveup("free: bad alignment\n");
#else
      Invalid++;
      return;
#endif
    }
    p--;
    tp = p->next;
    if (tp < Freetable || tp >= Freetable + FREETABLESIZE) {
#if DEBUG
      giveup("free: bad free table pointer\n");
#else
      Invalid++;
      return;
#endif
    }
    Frees++;
    Inuse -= (tp - Freetable) * MINSIZE;
    p->next = tp->next;
    tp->next = p;
  }
}

/*---------------------------------------------------------------------------*/

void _free(pp)
void *pp;
{
  free(pp);
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

void *_realloc(p, size)
void *p;
unsigned int size;
{
  return realloc(p, size);
}

/*---------------------------------------------------------------------------*/

/* Allocate block of cleared memory */
void *
calloc(nelem,size)
unsigned nelem; /* Number of elements */
unsigned size;  /* Size of each element */
{

	register unsigned i;
	register void *cp;

	if ((cp = malloc(i = nelem * size)) != NULL)
		memset(cp,0,i);
	return cp;
}

/*---------------------------------------------------------------------------*/

void *_calloc(nelem, elsize)
unsigned int nelem, elsize;
{
  return calloc(nelem, elsize);
}

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
	printf("heap size %lu avail %lu (%lu%%) morecores %lu\n",
	 Heapsize,Heapsize-Inuse,100L*(Heapsize-Inuse)/Heapsize,
	 Morecores);
	printf("allocs %lu frees %lu (diff %lu) alloc fails %lu invalid frees %lu\n",
		Allocs,Frees,Allocs-Frees,Memfail,Invalid);
	printf("pushdown calls %lu pushdown calls to malloc %lu\n",
		Pushdowns,Pushalloc);
	return 0;
}

