/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strtoul.h,v 1.1 1996/02/13 15:32:14 deyke Exp $ */

#ifndef _STRTOUL_H
#define _STRTOUL_H

#ifndef _CONFIGURE_H
#include "configure.h"
#endif

#if !HAS_STRTOUL

/* In strtoul.c: */
unsigned long strtoul(const char *nptr, char **endptr, register int base);

#endif

#endif  /* _STRTOUL_H */
