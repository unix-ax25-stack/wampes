/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.h,v 1.9 1992/06/01 10:34:09 deyke Exp $ */

#ifndef _ASY_H
#define _ASY_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#define ASY_MAX 16

struct asymode {
	char *name;
	char trigchar;
	int (*init) __ARGS((struct iface *,int));
	int (*free) __ARGS((struct iface *));
};
extern struct asymode Asymode[];

/* In 8250.c: */
int asy_init __ARGS((int dev,struct iface *ifp,char *arg1,char *arg2,
	int bufsize,int trigchar,long speed,int cts,int rlsd));
int32 asy_ioctl __ARGS((struct iface *ifp,int cmd,int set,int32 val));
int asy_speed __ARGS((int dev,long bps));
int asy_send __ARGS((int dev,struct mbuf *bp));
int asy_stop __ARGS((struct iface *ifp));
int get_rlsd_asy __ARGS((int dev, int new_rlsd));
int get_asy __ARGS((int dev,char *buf,int cnt));

/* In dialer.c: */
void dialer_kick __ARGS((struct asy *asyp));

/* In asyvec.asm: */
INTERRUPT asy0vec __ARGS((void));
INTERRUPT asy1vec __ARGS((void));
INTERRUPT asy2vec __ARGS((void));
INTERRUPT asy3vec __ARGS((void));
INTERRUPT asy4vec __ARGS((void));
INTERRUPT asy5vec __ARGS((void));

#endif  /* _ASY_H */
