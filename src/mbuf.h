/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mbuf.h,v 1.9 1993/05/17 13:45:10 deyke Exp $ */

#ifndef _MBUF_H
#define _MBUF_H

#include <stdio.h>

#ifndef _GLOBAL_H
#include "global.h"
#endif

extern long Pushdowns;          /* Total calls to pushdown() */
extern long Pushalloc;          /* Calls to pushdown that call malloc() */

/* Basic message buffer structure */
struct mbuf {
	struct mbuf *next;      /* Links mbufs belonging to single packets */
	struct mbuf *anext;     /* Links packets on queues */
	uint16 size;            /* Size of associated data buffer */
	int refcnt;             /* Reference count */
	struct mbuf *dup;       /* Pointer to duplicated mbuf */
	char *data;             /* Active working pointers */
	uint16 cnt;
};
#define NULLBUF (struct mbuf *)0
#define NULLBUFP (struct mbuf **)0

#define PULLCHAR(bpp)\
 ((bpp) != NULL && (*bpp) != NULLBUF && (*bpp)->cnt > 1 ? \
 ((*bpp)->cnt--,(unsigned char)*(*bpp)->data++) : pullchar(bpp))

/* In mbuf.c: */
struct mbuf *alloc_mbuf(int    size);
struct mbuf *free_mbuf(struct mbuf *bp);

struct mbuf *ambufw(int    size);
struct mbuf *copy_p(struct mbuf *bp,int    cnt);
uint16 dup_p(struct mbuf **hp,struct mbuf *bp,int    offset,int    cnt);
uint16 extract(struct mbuf *bp,int    offset,char *buf,int    len);
struct mbuf *free_p(struct mbuf *bp);
uint16 len_p(struct mbuf *bp);
void trim_mbuf(struct mbuf **bpp,int    length);
int write_p(FILE *fp,struct mbuf *bp);

struct mbuf *dequeue(struct mbuf **q);
void enqueue(struct mbuf **q,struct mbuf *bp);
void free_q(struct mbuf **q);
uint16 len_q(struct mbuf *bp);

struct mbuf *qdata(char *data,int    cnt);
uint16 dqdata(struct mbuf *bp,char *buf,unsigned cnt);

void append(struct mbuf **bph,struct mbuf *bp);
struct mbuf *pushdown(struct mbuf *bp,int    size);
uint16 pullup(struct mbuf **bph,char *buf,int    cnt);

int pullchar(struct mbuf **bpp);       /* returns -1 if nothing */
long pull16(struct mbuf **bpp); /* returns -1 if nothing */
int32 pull32(struct mbuf **bpp);        /* returns  0 if nothing */

uint16 get16(char *cp);
int32 get32(char *cp);
char *put16(char *cp,int    x);
char *put32(char *cp,int32 x);

void iqstat(void);
void refiq(void);
void mbuf_crunch(struct mbuf **bpp);

#define AUDIT(bp)       audit(bp,__FILE__,__LINE__)

#endif  /* _MBUF_H */
