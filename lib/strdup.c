/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.4 1994/10/06 16:15:48 deyke Exp $ */

extern char *strcpy(char *s1, const char *s2);
extern int strlen(const char *s);
extern void *malloc(int size);

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = (char *) malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}

