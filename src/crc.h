/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/crc.h,v 1.1 1991/04/25 18:28:45 deyke Exp $ */

#ifndef _CRC_H
#define _CRC_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

/* In crc.c: */
void append_crc __ARGS((struct mbuf *bp));
int check_crc __ARGS((struct mbuf *head));

#endif  /* _CRC_H */
