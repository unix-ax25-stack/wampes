/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strerror.h,v 1.2 1996/08/11 18:17:33 deyke Exp $ */

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
