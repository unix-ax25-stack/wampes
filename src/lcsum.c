/* @(#) $Id: lcsum.c,v 1.13 1999/01/24 22:23:24 deyke Exp $ */

/*
 * Word aligned linear buffer checksum routine.  Called from mbuf checksum
 * routine with simple args.  Intent is that this routine may be replaced
 * by assembly language routine for speed if so desired. (On the PC, the
 * replacement is in pcgen.asm.)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */

#include "global.h"
#include "ip.h"

uint
lcsum(
register uint16 *wp,
register uint len)
{
	register int32 sum = 0;
	uint16 result;
	static const short byte_order_test = 0x1234;

	while(len-- != 0)
		sum += *wp++;
	result = eac(sum);
	if (*(((const char *) &byte_order_test)) == 0x34) { /* LITTLE_ENDIAN */
		/* Swap the result because of the (char *) to (int *) type punning */
		result = (result << 8) | (result >> 8);
	}
	return result;
}
