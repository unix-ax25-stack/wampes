/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/flexnet.h,v 1.1 1994/10/21 11:55:14 deyke Exp $ */

#ifndef _FLEXNET_H
#define _FLEXNET_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _AX25_H
#include "ax25.h"
#endif

void flexnet_input(struct iface *iface, struct ax25_cb *axp, char *src, char *destination, struct mbuf *bp, int mcast);

#endif /* _FLEXNET_H */
