/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lcsum.c,v 1.8 1994/10/06 16:15:29 deyke Exp $ */

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

uint16
lcsum(
register uint16 *wp,
register uint16 len)
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

