/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/calc_crc.h,v 1.1 1994/01/14 21:29:47 deyke Exp $ */

#ifndef _CALC_CRC_H
#define _CALC_CRC_H

/* In calc_crc.c: */
extern const int Crc_16_table[];
int calc_crc_16(const char *str);

#endif  /* _CALC_CRC_H */
