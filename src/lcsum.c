/* @(#) $Id: lcsum.c,v 1.12 1996/08/19 16:30:14 deyke Exp $ */

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

	while(len-- != 0)
		sum += *wp++;
	result = eac(sum);
#if BYTE_ORDER == LITTLE_ENDIAN
	/* Swap the result because of the (char *) to (int *) type punning */
	result = (result << 8) | (result >> 8);
#endif
	return result;
}
