/* @(#) $Id: crc.h,v 1.6 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _CRC_H
#define _CRC_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

/* In crc.c: */
void append_crc_16(struct mbuf *bp);
int check_crc_16(struct mbuf *head);
void append_crc_rmnc(struct mbuf *bp);
int check_crc_rmnc(struct mbuf *head);
void append_crc_ccitt(struct mbuf *bp);
int check_crc_ccitt(const char *buf, int cnt);

#endif  /* _CRC_H */
