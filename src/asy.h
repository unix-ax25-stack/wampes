/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.h,v 1.3 1990/09/11 13:44:56 deyke Exp $ */

#ifndef ASY_MAX

#include "global.h"

#define ASY_MAX         16
extern int Nasy;
extern int Nslip;
extern int Nnrs;

#define SLIP_MODE       0
#define AX25_MODE       1
#define NRS_MODE        2
#define UNKNOWN_MODE    3

/* In 8250.c: */
int asy_init __ARGS((int dev,struct iface *iface,char *arg1,char *arg2,
	unsigned bufsize,int trigchar,int cts));
int asy_ioctl __ARGS((struct iface *iface,int argc,char *argv[]));
void asy_output __ARGS((int dev,char *buf,int cnt));
int asy_send __ARGS((int dev,struct mbuf *bp));
int asy_speed __ARGS((int dev,long speed));
int asy_stop __ARGS((struct iface *iface));
void asyint __ARGS((int dev));
void asy_tx __ARGS((int dev,void *p1,void *p2));
int get_asy __ARGS((int dev,char *buf,int cnt));
int stxrdy __ARGS((int dev));

/* In asyvec.asm: */
INTERRUPT asy0vec __ARGS((void));
INTERRUPT asy1vec __ARGS((void));
INTERRUPT asy2vec __ARGS((void));
INTERRUPT asy3vec __ARGS((void));
INTERRUPT asy4vec __ARGS((void));

#endif  /* ASY_MAX */

