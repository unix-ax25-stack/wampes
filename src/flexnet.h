/* @(#) $Id: flexnet.h,v 1.4 1996/08/12 18:51:17 deyke Exp $ */

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

void flexnet_input(struct iface *iface, struct ax25_cb *axp, uint8 *src, uint8 *destination, struct mbuf **bpp, int mcast);

#endif /* _FLEXNET_H */
