/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strerror.c,v 1.2 1996/01/04 19:11:56 deyke Exp $ */

#include "configure.h"

#if !HAS_STRERROR

extern char *sys_errlist[];
extern int sys_nerr;

char *strerror(int errnum)
{
  if (errnum > 0 && errnum <= sys_nerr)
    return sys_errlist[errnum];
  if (errnum)
    return "unknown error";
  return "no details given";
}

#else

int strerror_dummy;     /* Prevent "Empty source file" message */

#endif
