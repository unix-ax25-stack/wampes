/* @(#) $Id: strerror.h,v 1.3 1996/08/12 18:53:41 deyke Exp $ */

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
