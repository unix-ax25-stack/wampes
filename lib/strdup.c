/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.7 1996/02/13 15:31:00 deyke Exp $ */

#include "configure.h"

#if !HAS_STRDUP

#include <stdlib.h>
#include <string.h>

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = (char *) malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}

#else

struct prevent_empty_file_message;

#endif
