/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.h,v 1.4 1991/02/24 20:17:06 deyke Exp $ */

#ifndef _KISS_H
#define _KISS_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* KISS TNC control */
#define KISS_DATA       0
#define KISS_TXD        1
#define KISS_P          2
#define KISS_ST         3
#define KISS_TXT        4
#define KISS_FD         5
#define KISS_HW         6
#define KISS_RETURN     0xff

/* In kiss.c: */
int kiss_raw __ARGS((struct iface *iface,struct mbuf *data));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));
int kiss_ioctl __ARGS((struct iface *iface,int argc,char *argv[]));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));

#endif  /* _KISS_H */
