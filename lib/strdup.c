/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.1 1993/06/06 08:25:45 deyke Exp $ */

extern char *strcpy();
extern int strlen();
extern void *malloc();

char *strdup(const char *s)
{
  char *p;

  if (p = malloc(strlen(s) + 1)) strcpy(p, s);
  return p;
}

