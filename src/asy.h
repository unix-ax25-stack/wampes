/* @(#) $Id: asy.h,v 1.20 1999/01/27 18:45:40 deyke Exp $ */

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
	int trigchar;
	int (*init)(struct iface *);
	int (*free)(struct iface *);
};
extern struct asymode Asymode[];

/* In n8250.c: */
int asy_init(int dev,struct iface *ifp,int base,int irq,
	uint bufsize,int trigchar,long speed,int cts,int rlsd,int chain);
int32 asy_ioctl(struct iface *ifp,int cmd,int set,int32 val);
int asy_speed(int dev,long bps);
int asy_send(int dev,struct mbuf **bpp);
int asy_stop(struct iface *ifp);
int get_rlsd_asy(int dev, int new_rlsd);
int get_asy(int dev,uint8 *buf,int cnt);
void fp_stop(void);

#endif  /* _ASY_H */
