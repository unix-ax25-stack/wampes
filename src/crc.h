/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/crc.h,v 1.3 1993/01/29 06:48:19 deyke Exp $ */

#ifndef _CRC_H
#define _CRC_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

/* In crc.c: */
void append_crc_16 __ARGS((struct mbuf *bp));
int check_crc_16 __ARGS((struct mbuf *head));
void append_crc_rmnc __ARGS((struct mbuf *bp));
int check_crc_rmnc __ARGS((struct mbuf *head));
void append_crc_ccitt __ARGS((struct mbuf *bp));
int check_crc_ccitt __ARGS((const char *buf, int cnt));

#endif  /* _CRC_H */
