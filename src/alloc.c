/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/alloc.c,v 1.10 1991/10/11 18:56:07 deyke Exp $ */

/* memory allocation routines
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "global.h"

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
static unsigned int blksize __ARGS((void *p));

/*---------------------------------------------------------------------------*/

static void giveup(mesg)
char  *mesg;
{
  fprintf(stderr, mesg);
  abort();
}

/*---------------------------------------------------------------------------*/

void *malloc(size)
register unsigned int  size;
{

  static struct block *freespace;
  static unsigned int  freesize;

  int  align_error;
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

void *mallocw(size)
unsigned int  size;
{
  return malloc(size);
}

/*---------------------------------------------------------------------------*/

void *_malloc(size)
unsigned int  size;
{
  return malloc(size);
}

/*---------------------------------------------------------------------------*/

void free(pp)
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

/* Return size of allocated buffer, in bytes */

static unsigned int  blksize(p)
void *p;
{
  register struct block *tp;

  tp = (struct block *) p;
  tp--;
  tp = tp->next;
  return (tp - Freetable) * MINSIZE - sizeof(struct block *);
}

/*---------------------------------------------------------------------------*/

void *realloc(p, size)
void *p;
unsigned int  size;
{

  unsigned int  oldsize;
  void *q;

  oldsize = blksize(p);
  if (size <= oldsize) return p;
  if (q = malloc(size)) {
    memcpy(q, p, oldsize);
    free(p);
  }
  return q;
}

/*---------------------------------------------------------------------------*/

void *_realloc(p, size)
void *p;
unsigned int  size;
{
  return realloc(p, size);
}

/*---------------------------------------------------------------------------*/

void *calloc(nelem, elsize)
unsigned int  nelem, elsize;
{

  register char  *q;
  register unsigned int  size;
  register void *p;

  if (p = malloc(size = nelem * elsize))
    for (q = (char *) p;  size--; *q++ = '\0') ;
  return p;
}

/*---------------------------------------------------------------------------*/

void *callocw(nelem, elsize)
unsigned int  nelem, elsize;
{
  return calloc(nelem, elsize);
}

/*---------------------------------------------------------------------------*/

void *_calloc(nelem, elsize)
unsigned int  nelem, elsize;
{
  return calloc(nelem, elsize);
}

/*---------------------------------------------------------------------------*/

unsigned long  availmem()
{
  return 0x7fffffff;
}

/*---------------------------------------------------------------------------*/

int  domem(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  tprintf("heap size %lu avail %lu (%lu%%) morecores %lu\n",
   Heapsize, Heapsize - Inuse, 100L * (Heapsize - Inuse) / Heapsize, Morecores);
  tprintf("allocs %lu frees %lu (diff %lu) alloc fails %lu invalid frees %lu\n",
   Allocs, Frees, Allocs - Frees, Memfail, Invalid);
  return 0;
}

