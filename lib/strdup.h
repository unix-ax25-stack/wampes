/* @(#) $Id: strdup.h,v 1.4 1996/08/12 18:53:41 deyke Exp $ */

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
