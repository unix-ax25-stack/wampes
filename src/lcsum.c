/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lcsum.c,v 1.3 1990/09/11 13:45:47 deyke Exp $ */

#if     (defined(MPU8086) || defined(MPU8080) || defined(vax))
#define LITTLE_ENDIAN   /* Low order bytes are first in memory */
#endif                  /* Almost all other machines are big-endian */
#include "global.h"
#include "ip.h"

/*
 * Word aligned linear buffer checksum routine.  Called from mbuf checksum
 * routine with simple args.  Intent is that this routine may be replaced
 * by assembly language routine for speed if so desired.
 */
int16
lcsum(wp,len)
register int16 *wp;
register int16 len;
{
	register int32 sum = 0;
	int16 result;

	while(len-- != 0)
		sum += *wp++;
	result = eac(sum);
#ifdef  LITTLE_ENDIAN
	/* Swap the result because of the (char *) to (int *) type punning */
	result = (result << 8) | (result >> 8);
#endif
	return result;
}

