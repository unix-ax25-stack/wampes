/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.2 1993/06/10 09:46:32 deyke Exp $ */

#include <stdlib.h>

extern char *strcpy();
extern int strlen();

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if (p = malloc(strlen(s) + 1)) strcpy(p, s);
  return p;
}

