/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strerror.h,v 1.1 1996/02/13 15:32:03 deyke Exp $ */

#ifndef _STRERROR_H
#define _STRERROR_H

#ifndef _CONFIGURE_H
#include "configure.h"
#endif

#if !HAS_STRERROR

/* In strerror.c: */
char *strerror(int errnum);

#endif

#endif  /* _STRERROR_H */
