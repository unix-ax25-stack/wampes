/* @(#) $Id: strerror.c,v 1.6 1998/03/09 17:46:26 deyke Exp $ */

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

void strerror_prevent_empty_file_message(void)
{
}

#endif
