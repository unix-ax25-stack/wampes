/* @(#) $Id: calc_crc.h,v 1.3 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _CALC_CRC_H
#define _CALC_CRC_H

/* In calc_crc.c: */
extern const int Crc_16_table[];
int calc_crc_16(const char *str);

#endif  /* _CALC_CRC_H */
