#include <memory.h>
#include <stdio.h>

extern char  *sbrk();

#define ALLOCSIZE       0x4000
#define FREETABLESIZE   1024
#define MEMFULL         ((struct block *) (-1))
#define MINSIZE         16
#define SBRK(i)         ((struct block *) sbrk((int) (i)))

struct block {
  struct block *next;
};

static struct block freetable[FREETABLESIZE];
static unsigned int  allocated;
static unsigned int  failures;
static unsigned int  inuse;

/*---------------------------------------------------------------------------*/

char  *malloc(size)
register unsigned int  size;
{

  static struct block *freespace;
  static unsigned int  freesize;

  register struct block *p, *tp;
  struct block *tpf;

  size = (size + sizeof(struct block *) + MINSIZE - 1) & ~(MINSIZE - 1);
  if ((tp = freetable + size / MINSIZE) >= freetable + FREETABLESIZE) {
    fprintf(stderr, "malloc: requested block too large\n");
    abort();
  }
  if (p = tp->next)
    tp->next = p->next;
  else {
    if (size > freesize) {
      if (freesize) {
	tpf = freetable + freesize / MINSIZE;
	freespace->next = tpf->next;
	tpf->next = freespace;
	freesize = 0;
      }
      if ((freespace = SBRK(ALLOCSIZE)) == MEMFULL) {
	failures++;
	return (char *) 0;
      }
      allocated += (freesize = ALLOCSIZE);
    }
    p = freespace;
    freespace += (size / sizeof(struct block *));
    freesize -= size;
  }
  p->next = tp;
  inuse += size;
  return (char *) (p + 1);
}

/*---------------------------------------------------------------------------*/

void free(pp)
char  *pp;
{
  register struct block *p, *tp;

  if (p = (struct block *) pp) {
    p--;
    tp = p->next;
    inuse -= (tp - freetable) * MINSIZE;
    p->next = tp->next;
    tp->next = p;
  }
}

/*---------------------------------------------------------------------------*/

/* Return size of allocated buffer, in bytes */

unsigned int  blksize(p)
char  *p;
{
  register struct block *tp;

  tp = (struct block *) p;
  tp--;
  tp = tp->next;
  return (tp - freetable) * MINSIZE - sizeof(struct block *);
}

/*---------------------------------------------------------------------------*/

char  *realloc(p, size)
char  *p;
unsigned int  size;
{

  char  *q;
  unsigned int  oldsize;

  oldsize = blksize(p);
  if (size <= oldsize) return p;
  if (q = malloc(size)) {
    memcpy(q, p, (int) oldsize);
    free(p);
  }
  return q;
}

/*---------------------------------------------------------------------------*/

char  *calloc(nelem, elsize)
unsigned int  nelem, elsize;
{
  register char  *p, *q;
  register unsigned int  size;

  if (p = q = malloc(size = nelem * elsize))
    while (size--) *q++ = '\0';
  return p;
}

/*---------------------------------------------------------------------------*/

int  memstat()
{
  printf("%7u bytes of memory allocated\n", allocated);
  printf("%7u bytes of memory being used (%lu%% used)\n", inuse, 100l * inuse / allocated);
  printf("%7u requests for memory denied\n", failures);
  return 0;
}
