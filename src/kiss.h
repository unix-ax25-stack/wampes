/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.h,v 1.7 1993/01/29 06:48:27 deyke Exp $ */

#ifndef _KISS_H
#define _KISS_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* In kiss.c: */
int kiss_free __ARGS((struct iface *ifp));
int kiss_raw __ARGS((struct iface *iface,struct mbuf *data));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));
int kiss_init __ARGS((struct iface *ifp));
int32 kiss_ioctl __ARGS((struct iface *iface,int cmd,int set,int32 val));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));

#endif  /* _KISS_H */
