/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/alloc.c,v 1.21 1994/01/31 12:57:32 deyke Exp $ */

/* memory allocation routines
 */

#ifndef PURIFY

#define malloc  Xmalloc
#define free    Xfree
#define realloc Xrealloc
#define calloc  Xcalloc

#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PURIFY

#undef malloc
#undef free
#undef realloc
#undef calloc

#include "global.h"
#include "mbuf.h"
#include "cmdparse.h"

#define ALIGN           8

#define MIN_N           3       /* Min size is 8 bytes */
#define MAX_N           16      /* Max size is 65536 bytes */

struct block {
  struct block *next;
};

static const int Blocksize[] = {
  0x00000001, 0x00000002, 0x00000004, 0x00000008,
  0x00000010, 0x00000020, 0x00000040, 0x00000080,
  0x00000100, 0x00000200, 0x00000400, 0x00000800,
  0x00001000, 0x00002000, 0x00004000, 0x00008000,
  0x00010000, 0x00020000, 0x00040000, 0x00080000,
  0x00100000, 0x00200000, 0x00400000, 0x00800000,
  0x01000000, 0x02000000, 0x04000000, 0x08000000,
  0x10000000, 0x20000000, 0x40000000, 0x80000000
};

static struct block *Freetable[33];
static unsigned long Memfail;   /* Count of allocation failures */
static unsigned long Allocs;    /* Total allocations */
static unsigned long Frees;     /* Total frees */
static unsigned long Invalid;   /* Total calls to free with garbage arg */
static unsigned long Heapsize;
static unsigned long Inuse;
static unsigned long Morecores;
static int Memdebug;
static unsigned long Sizes[33];

#define FREEPATTERN     0xbb
#define USEDPATTERN     0xdd

static int domdebug(int argc,char *argv[],void *ptr);
static int dostat(int argc,char *argv[],void *p);
static int dosizes(int argc,char *argv[],void *p);

struct cmds Memcmds[] = {
	"debug",        domdebug,       0, 0, NULLCHAR,
	"sizes",        dosizes,        0, 0, NULLCHAR,
	"status",       dostat,         0, 0, NULLCHAR,
	NULLCHAR,
};

/*---------------------------------------------------------------------------*/

#ifdef RECOMBINE

static void putblock(struct block *p, int n)
{

  struct block **cpp;
  struct block *cp;
  struct block *np;
  struct block *pp;

Retry:
  if (n == MAX_N) {
    p->next = Freetable[MAX_N];
    Freetable[MAX_N] = p;
    return;
  }
  pp = (struct block *) (((char *) p) - Blocksize[n]);
  np = (struct block *) (((char *) p) + Blocksize[n]);
  for (cpp = Freetable + n; ; cpp = &cp->next) {
    cp = *cpp;
    if (cp == pp || cp == np) {
      *cpp = cp->next;
#if 0
      putblock(cp == pp ? pp : p, n + 1);
      return;
#else
      if (cp == pp) p = pp;
      n++;
      goto Retry;
#endif
    }
    if (!cp || cp > p) {
      p->next = cp;
      *cpp = p;
      return;
    }
  }
}

#endif

/*---------------------------------------------------------------------------*/

static struct block *getblock(int n)
{

  int a;
  struct block *p;

  if (p = Freetable[n]) {
    Freetable[n] = p->next;
  } else if (n == MAX_N) {
    if ((a = (int) sbrk(Blocksize[MAX_N] + ALIGN - 1)) == -1) {
      Memfail++;
      return 0;
    }
    Morecores++;
    Heapsize += (Blocksize[MAX_N] + ALIGN - 1);
    a += sizeof(struct block *);
    a = (a + ALIGN - 1) & ~(ALIGN - 1);
    a -= sizeof(struct block *);
    p = (struct block *) a;
  } else if (p = getblock(n + 1)) {
#ifdef RECOMBINE
    putblock((struct block *) (Blocksize[n] + (char *) p), n);
#else
    struct block *q = (struct block *) (Blocksize[n] + (char *) p);
    q->next = Freetable[n];
    Freetable[n] = q;
#endif
  } else {
    return 0;
  }
  return p;
}

/*---------------------------------------------------------------------------*/

/* Allocate block of 'nb' bytes */
void *
malloc(nb)
unsigned nb;
{

  int n;
  struct block *p;

  nb += sizeof(struct block *) - 1;
  n = 0;
  if (nb & 0xffff0000) { n += 16; nb >>= 16; }
  if (nb & 0x0000ff00) { n +=  8; nb >>=  8; }
  if (nb & 0x000000f0) { n +=  4; nb >>=  4; }
  if (nb & 0x0000000c) { n +=  2; nb >>=  2; }
  if (nb & 0x00000002) { n++    ; nb >>=  1; }
  if (nb & 0x00000001) { n++    ;            }
  if (n < MIN_N) n = MIN_N;
  if (n > MAX_N) {
    fprintf(stderr, "malloc: size too large\n");
    abort();
  }
  if (!(p = getblock(n))) return 0;
  Sizes[n]++;
  if (Memdebug) memset((char *) p, USEDPATTERN, Blocksize[n]);
  p->next = (struct block *) (Freetable + n);
  Allocs++;
  Inuse += Blocksize[n];
  return (void *) (p + 1);
}

/*---------------------------------------------------------------------------*/

/* Put memory block back on heap */
void
free(blk)
void *blk;
{

  int n;
  struct block *p;

  if (p = (struct block *) blk) {
    if ((ALIGN - 1) & (int) p) {
      fprintf(stderr, "free: bad alignment\n");
      Invalid++;
      return;
    }
    p--;
    n = p->next - (struct block *) Freetable;
    if (n < MIN_N || n > MAX_N) {
      fprintf(stderr, "free: bad free table pointer\n");
      Invalid++;
      return;
    }
    if (Memdebug) memset((char *) p, FREEPATTERN, Blocksize[n]);
    Frees++;
    Inuse -= Blocksize[n];
#ifdef RECOMBINE
    putblock(p, n);
#else
    p->next = Freetable[n];
    Freetable[n] = p;
#endif
  }
}

/*---------------------------------------------------------------------------*/

/* Move existing block to new area */
void *
realloc(area,size)
void *area;
unsigned size;
{

  struct block *tp;
  unsigned osize;
  void *new;

  if (!area)
    return malloc(size);

  if (!size) {
    free(area);
    return 0;
  }

  tp = area;
  tp--;
  osize = Blocksize[tp->next - (struct block *) Freetable] - sizeof(struct block *);
  if (size <= osize) return area;
  if (new = malloc(size)) {
    memcpy(new, area, osize);
    free(area);
  }
  return new;
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

void *
mallocw(nb)
unsigned nb;
{
  return malloc(nb);
}

/*---------------------------------------------------------------------------*/

void *
callocw(nelem,size)
unsigned nelem; /* Number of elements */
unsigned size;  /* Size of each element */
{
  return calloc(nelem, size);
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

#ifndef PURIFY

/* Print heap stats */
static int
dostat(argc,argv,envp)
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

#endif

/*---------------------------------------------------------------------------*/

#ifndef PURIFY

static int
dosizes(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int n;

	for(n=MIN_N;n<=MAX_N;n += 4){
		printf("N=%6u:%7ld| N=%6u:%7ld| N=%6u:%7ld| N=%6u:%7ld\n",
		 Blocksize[n],Sizes[n],Blocksize[n+1],Sizes[n+1],
		 Blocksize[n+2],Sizes[n+2],Blocksize[n+3],Sizes[n+3]);
	}
	return 0;
}

#endif

/*---------------------------------------------------------------------------*/

int
domem(argc,argv,p)
int argc;
char *argv[];
void *p;
{
#ifndef PURIFY
	return subcmd(Memcmds,argc,argv,p);
#else
	return 0;
#endif
}

/*---------------------------------------------------------------------------*/

#ifndef PURIFY

static int
domdebug(argc,argv,ptr)
int argc;
char *argv[];
void *ptr;
{
	setbool(&Memdebug,"Heap debugging",argc,argv);
	return 0;
}

#endif
