/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.h,v 1.2 1996/02/13 15:31:00 deyke Exp $ */

#ifndef _STRDUP_H
#define _STRDUP_H

#ifndef _CONFIGURE_H
#include "configure.h"
#endif

#if !HAS_STRDUP

/* In strdup.c: */
char *strdup(const char *s);

#endif

#endif  /* _STRDUP_H */
