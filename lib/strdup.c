/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.3 1994/09/05 12:47:02 deyke Exp $ */

#include <stdlib.h>

extern char *strcpy();
extern int strlen();

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}

