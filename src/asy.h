/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.h,v 1.13 1993/05/17 13:44:44 deyke Exp $ */

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
	int (*init)(struct iface *);
	int (*free)(struct iface *);
};
extern struct asymode Asymode[];

/* In n8250.c: */
int asy_init(int dev,struct iface *ifp,int base,int irq,
	int    bufsize,int trigchar,long speed,int cts,int rlsd,int chain);
int32 asy_ioctl(struct iface *ifp,int cmd,int set,int32 val);
int asy_speed(int dev,long bps);
int asy_send(int dev,struct mbuf *bp);
int asy_stop(struct iface *ifp);
int get_rlsd_asy(int dev, int new_rlsd);
int get_asy(int dev,char *buf,int cnt);
void fp_stop(void);

/* In asyvec.asm: */
INTERRUPT asy0vec(void);
INTERRUPT asy1vec(void);
INTERRUPT asy2vec(void);
INTERRUPT asy3vec(void);
INTERRUPT asy4vec(void);
INTERRUPT asy5vec(void);

/* In fourport.asm: */
INTERRUPT fp0vec(void);

#endif  /* _ASY_H */
