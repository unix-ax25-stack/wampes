/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mbuf.h,v 1.3 1990/09/11 13:46:01 deyke Exp $ */

#ifndef NULLBUF

#include <stdio.h>
#include "global.h"

/* Basic message buffer structure */
struct mbuf {
	struct mbuf *next;      /* Links mbufs belonging to single packets */
	struct mbuf *anext;     /* Links packets on queues */
	int16 size;             /* Size of associated data buffer */
	int refcnt;             /* Reference count */
	struct mbuf *dup;       /* Pointer to duplicated mbuf */
	char *data;             /* Active working pointers */
	int16 cnt;
};
#define NULLBUF (struct mbuf *)0
#define NULLBUFP (struct mbuf **)0

#define PULLCHAR(bpp)\
 ((bpp) != NULL && (*bpp) != NULLBUF && (*bpp)->cnt > 1 ? \
 ((*bpp)->cnt--,(unsigned char)*(*bpp)->data++) : pullchar(bpp))

/* In mbuf.c: */
void enqueue __ARGS((struct mbuf **q,struct mbuf *bp));
void append __ARGS((struct mbuf **bph,struct mbuf *bp));
void free_q __ARGS((struct mbuf **q));
void trim_mbuf __ARGS((struct mbuf **bpp,int length));
struct mbuf *alloc_mbuf __ARGS((int size));
struct mbuf *ambufw __ARGS((int size));
struct mbuf *free_mbuf __ARGS((struct mbuf *bp));
struct mbuf *dequeue __ARGS((struct mbuf **q));
struct mbuf *copy_p __ARGS((struct mbuf *bp,int cnt));
struct mbuf *free_p __ARGS((struct mbuf *bp));
struct mbuf *qdata __ARGS((char *data,int cnt));
struct mbuf *pushdown __ARGS((struct mbuf *bp,int size));
int16 pullup __ARGS((struct mbuf **bph,char *buf,int cnt));
int16 dup_p __ARGS((struct mbuf **hp,struct mbuf *bp,int offset,int cnt));
int16 len_p __ARGS((struct mbuf *bp));
int16 dqdata __ARGS((struct mbuf *bp,char *buf,unsigned cnt));
int16 len_q __ARGS((struct mbuf *bp));
int32 pull32 __ARGS((struct mbuf **bpp));
int32 get32 __ARGS((char *cp));
int16 pull16 __ARGS((struct mbuf **bpp));
void refiq __ARGS((void));
void mbuf_crunch __ARGS((struct mbuf **bpp));
int16 get16 __ARGS((char *cp));
int pullchar __ARGS((struct mbuf **bpp));
char *put16 __ARGS((char *cp,int x));
char *put32 __ARGS((char *cp,int32 x));
int write_p __ARGS((FILE *fp,struct mbuf *bp));
void iqstat __ARGS((void));

#define AUDIT(bp)       audit(bp,__FILE__,__LINE__)

#endif  /* NULLBUF */

