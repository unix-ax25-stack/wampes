/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.h,v 1.5 1991/04/12 18:34:36 deyke Exp $ */

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

#define SLIP_MODE       0
#define AX25_MODE       1
#define NRS_MODE        2
#define UNKNOWN_MODE    3
#define PPP_MODE        4

/* In 8250.c: */
int asy_autobaud __ARGS((int dev));
int asy_init __ARGS((int dev,struct iface *iface,char *arg1,char *arg2,
	unsigned bufsize,int trigchar,int cts,int rlsd));
int32 asy_ioctl __ARGS((struct iface *iface,int cmd,int set,int32 val));
void asy_output __ARGS((int dev,char *buf,int cnt));
int asy_send __ARGS((int dev,struct mbuf *bp));
int asy_speed __ARGS((int dev,long speed,long autospeed));
int asy_stop __ARGS((struct iface *iface));
void asyint __ARGS((int dev));
int get_rlsd_asy __ARGS((int dev, int new_rlsd));
void asy_tx __ARGS((int dev,void *p1,void *p2));
int get_asy __ARGS((int dev,char *buf,int cnt));
int stxrdy __ARGS((int dev));

/* In asyvec.asm: */
INTERRUPT asy0vec __ARGS((void));
INTERRUPT asy1vec __ARGS((void));
INTERRUPT asy2vec __ARGS((void));
INTERRUPT asy3vec __ARGS((void));
INTERRUPT asy4vec __ARGS((void));

#endif  /* _ASY_H */
