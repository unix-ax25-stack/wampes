/* @(#) $Id: strerror.c,v 1.5 1996/08/12 18:53:41 deyke Exp $ */

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
