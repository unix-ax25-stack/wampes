/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strerror.c,v 1.4 1996/08/11 18:17:33 deyke Exp $ */

#include "configure.h"

#if !HAS_STRERROR

extern char *sys_errlist[];
extern int sys_nerr;

#include "strerror.h"

char *strerror(int errnum)
{
  if (errnum > 0 && errnum <= sys_nerr)
    return sys_errlist[errnum];
  if (errnum)
    return "unknown error";
  return "no details given";
}

#else

struct prevent_empty_file_message;

#endif
