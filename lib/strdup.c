/* @(#) $Id: strdup.c,v 1.10 1996/08/12 18:53:41 deyke Exp $ */

#include "configure.h"

#if !HAS_STRDUP || defined _AIX

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
