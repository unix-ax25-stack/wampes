/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strerror.c,v 1.1 1995/03/24 13:02:18 deyke Exp $ */

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

#endif
