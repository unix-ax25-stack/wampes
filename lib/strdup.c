/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strdup.c,v 1.5 1994/10/09 08:23:11 deyke Exp $ */

#ifdef __cplusplus
#define EXTERN_C        extern "C"
#else
#define EXTERN_C
#endif

EXTERN_C char *strcpy(char *s1, const char *s2);
EXTERN_C unsigned int strlen(const char *s);
EXTERN_C void *malloc(int size);

#include "strdup.h"

char *strdup(const char *s)
{
  char *p;

  if ((p = (char *) malloc(strlen(s) + 1))) strcpy(p, s);
  return p;
}

