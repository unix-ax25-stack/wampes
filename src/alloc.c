/* @(#) $Id: alloc.c,v 1.37 1999/02/01 22:24:25 deyke Exp $ */

/* memory allocation routines
 */

#if defined PURIFY || defined __NeXT__ || defined __NetBSD__ || defined __sgi
#define USE_WAMPES_MALLOC 0
#else
#define USE_WAMPES_MALLOC 1
#endif

#if USE_WAMPES_MALLOC

#define malloc  Xmalloc
#define free    Xfree
#define realloc Xrealloc
#define calloc  Xcalloc

#endif

#define NO_FIX_MALLOC

#include <stdio.h>
#include <unistd.h>

#include "global.h"

#if USE_WAMPES_MALLOC

#undef malloc
#undef free
#undef realloc
#undef calloc

#include "mbuf.h"
#include "cmdparse.h"

#define MIN_N           4       /* Min size is 16 bytes */
#define MAX_N           16      /* Max size is 65536 bytes */

#define USEROFFSET      8       /* Offset of user area from block start */

struct block {
  int n;                        /* Block size index */
  int inuse;                    /* Block in-use flag */
  struct block *prev;           /* Linked-list pointer (free blocks only) */
  struct block *next;           /* Linked-list pointer (free blocks only) */
};

static const unsigned int Blocksize[] = {
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
static int Memmerge = 1;
static unsigned long Splits;
static unsigned long Merges;

#define FREEPATTERN     0xbb
#define USEDPATTERN     0xdd

static int domdebug(int argc,char *argv[],void *ptr);
static int dostat(int argc,char *argv[],void *p);
static int dofreelist(int argc,char *argv[],void *p);
static int domerge(int argc,char *argv[],void *p);
static int dosizes(int argc,char *argv[],void *p);

static struct cmds Memcmds[] = {
	{ "debug",      domdebug,       0, 0, NULL },
	{ "freelist",   dofreelist,     0, 0, NULL },
	{ "merge",      domerge,        0, 0, NULL },
	{ "sizes",      dosizes,        0, 0, NULL },
	{ "status",     dostat,         0, 0, NULL },
	{ NULL,         NULL,           0, 0, NULL }
};

/*---------------------------------------------------------------------------*/

#define link_block(p, n) \
{ \
  p->n = n; \
  p->inuse = 0; \
  p->prev = 0; \
  if ((p->next = Freetable[n])) \
    p->next->prev = p; \
  Freetable[n] = p; \
}

/*---------------------------------------------------------------------------*/

#define unlink_block(p, n) \
{ \
  if (p->prev) \
    p->prev->next = p->next; \
  else \
    Freetable[n] = p->next; \
  if (p->next) \
    p->next->prev = p->prev; \
}

/*---------------------------------------------------------------------------*/

static struct block *getblock(int n)
{

  int a;
  struct block *buddy;
  struct block *p;

  if ((p = Freetable[n])) {
    unlink_block(p, n);
  } else if (n == MAX_N) {
    if ((a = (int) sbrk(0)) == -1) {
      Memfail++;
      return 0;
    }
    a = (a + Blocksize[MAX_N] - 1) & ~(Blocksize[MAX_N] - 1);
    if (brk((void *) (a + Blocksize[MAX_N]))) {
      Memfail++;
      return 0;
    }
    Morecores++;
    Heapsize += Blocksize[MAX_N];
    p = (struct block *) a;
  } else if ((p = getblock(n + 1))) {
    buddy = (struct block *) ((int) p + Blocksize[n]);
    link_block(buddy, n);
    Splits++;
  } else {
    return 0;
  }
  return p;
}

/*---------------------------------------------------------------------------*/

/* Allocate block of 'nb' bytes */
void *
malloc(unsigned nb)
{

  int n;
  struct block *p;

  nb += USEROFFSET - 1;
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
  if (Memdebug) memset(p, USEDPATTERN, Blocksize[n]);
  p->n = n;
  p->inuse = 1;
  Allocs++;
  Inuse += Blocksize[n];
  return (void *) ((int) p + USEROFFSET);
}

/*---------------------------------------------------------------------------*/

/* Put memory block back on heap */
void
free(void *blk)
{

  int n;
  struct block *buddy;
  struct block *p;

  if (!blk)
    return;
  p = (struct block *) ((int) blk - USEROFFSET);
  n = p->n;
  if (n < MIN_N || n > MAX_N) {
    fprintf(stderr, "free: n out of range\n");
    Invalid++;
    return;
  }
  if (!p->inuse) {
    fprintf(stderr, "free: inuse flag not set\n");
    Invalid++;
    return;
  }
  if ((int) p & (Blocksize[n] - 1)) {
    fprintf(stderr, "free: bad alignment\n");
    Invalid++;
    return;
  }
  if (Memdebug)
    memset(p, FREEPATTERN, Blocksize[n]);
  Frees++;
  Inuse -= Blocksize[n];
  if (Memmerge) {
    while (n < MAX_N) {
      buddy = (struct block *) ((int) p ^ Blocksize[n]);
      if (buddy->inuse || buddy->n != n)
	break;
      unlink_block(buddy, n);
      if (p > buddy)
	p = buddy;
      n++;
      Merges++;
    }
  }
  link_block(p, n);
}

/*---------------------------------------------------------------------------*/

/* Move existing block to new area */
void *
realloc(void *area,unsigned size)
{

  int n;
  struct block *p;
  unsigned osize;
  void *newp;

  if (!area)
    return malloc(size);

  if (!size) {
    free(area);
    return 0;
  }

  p = (struct block *) ((int) area - USEROFFSET);
  n = p->n;
  if (n < MIN_N || n > MAX_N) {
    fprintf(stderr, "realloc: n out of range\n");
    Invalid++;
    return malloc(size);
  }
  if (!p->inuse) {
    fprintf(stderr, "realloc: inuse flag not set\n");
    Invalid++;
    return malloc(size);
  }
  if ((int) p & (Blocksize[n] - 1)) {
    fprintf(stderr, "realloc: bad alignment\n");
    Invalid++;
    return malloc(size);
  }
  if ((newp = malloc(size))) {
    osize = Blocksize[n] - USEROFFSET;
    memcpy(newp, area, osize < size ? osize : size);
    free(area);
  }
  return newp;
}

/*---------------------------------------------------------------------------*/

/* Allocate block of cleared memory */
void *
calloc(
unsigned nelem, /* Number of elements */
unsigned size   /* Size of each element */
){

	unsigned i;
	char *cp;

	i = nelem * size;
	if((cp = (char *) malloc(i)) != NULL)
		memset(cp,0,i);
	return cp;
}

#endif

/*---------------------------------------------------------------------------*/

void *
mallocw(unsigned nb)
{
  return malloc(nb);
}

/*---------------------------------------------------------------------------*/

void *
callocw(
unsigned nelem, /* Number of elements */
unsigned size   /* Size of each element */
){
  return calloc(nelem, size);
}

/*---------------------------------------------------------------------------*/

#if USE_WAMPES_MALLOC

/* Print heap stats */
static int
dostat(
int argc,
char *argv[],
void *envp
){
	printf("heap size %lu avail %lu (%lu%%) morecores %lu\n",
	 Heapsize,Heapsize-Inuse,100L*(Heapsize-Inuse)/Heapsize,
	 Morecores);
	printf("allocs %lu frees %lu (diff %lu) alloc fails %lu invalid frees %lu\n",
		Allocs,Frees,Allocs-Frees,Memfail,Invalid);
	printf("splits %lu merges %lu (diff %lu)\n",
		Splits,Merges,Splits-Merges);
	printf("\n");
	mbufstat();
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Print heap free list */
static int
dofreelist(
int argc,
char *argv[],
void *envp
){

	int n;
	struct block *p;
	unsigned long l;
	unsigned long len[33];

	for(n=MIN_N;n<=32;n++){
		l = 0;
		for(p=Freetable[n];p;p=p->next)
			l++;
		len[n] = l;
	}

	for(n=MIN_N;n<=MAX_N;n += 4){
		printf("N=%6u:%10lu|N=%6u:%10lu|N=%6u:%10lu|N=%6u:%10lu\n",
		 Blocksize[n],len[n],Blocksize[n+1],len[n+1],
		 Blocksize[n+2],len[n+2],Blocksize[n+3],len[n+3]);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int
domerge(
int argc,
char *argv[],
void *envp)
{
	return setbool(&Memmerge, "Heap merging", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int
dosizes(
int argc,
char *argv[],
void *p
){
	int n;

	for(n=MIN_N;n<=MAX_N;n += 4){
		printf("N=%6u:%10lu|N=%6u:%10lu|N=%6u:%10lu|N=%6u:%10lu\n",
		 Blocksize[n],Sizes[n],Blocksize[n+1],Sizes[n+1],
		 Blocksize[n+2],Sizes[n+2],Blocksize[n+3],Sizes[n+3]);
	}
	mbufsizes();
	return 0;
}

#endif

/*---------------------------------------------------------------------------*/

int
domem(
int argc,
char *argv[],
void *p
){
#if USE_WAMPES_MALLOC
	return subcmd(Memcmds,argc,argv,p);
#else
	return 0;
#endif
}

/*---------------------------------------------------------------------------*/

#if USE_WAMPES_MALLOC

static int
domdebug(
int argc,
char *argv[],
void *ptr
){
	setbool(&Memdebug,"Heap debugging",argc,argv);
	return 0;
}

#endif
