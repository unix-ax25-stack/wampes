/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.h,v 1.7 1992/01/08 13:44:59 deyke Exp $ */

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

/* In 8250.c: */
int asy_init __ARGS((int dev,struct iface *ifp,char *arg1,char *arg2,
	int bufsize,int trigchar,int monitor,long speed));
int32 asy_ioctl __ARGS((struct iface *ifp,int cmd,int set,int32 val));
int asy_speed __ARGS((int dev,long bps));
int asy_send __ARGS((int dev,struct mbuf *bp));
int asy_stop __ARGS((struct iface *ifp));
int get_rlsd_asy __ARGS((int dev, int new_rlsd));
int get_asy __ARGS((int dev,char *buf,int cnt));

/* In asyvec.asm: */
INTERRUPT asy0vec __ARGS((void));
INTERRUPT asy1vec __ARGS((void));
INTERRUPT asy2vec __ARGS((void));
INTERRUPT asy3vec __ARGS((void));
INTERRUPT asy4vec __ARGS((void));

#endif  /* _ASY_H */
