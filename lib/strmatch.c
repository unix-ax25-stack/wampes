/* @(#) $Id: strmatch.c,v 1.6 1996/08/12 18:53:41 deyke Exp $ */

#include "strmatch.h"

int stringmatch(const char *s, const char *p)
{

  int c1;
  int c2;
  int c;
  int match;
  int reverse;

  for (;;)
    switch (*p) {
    case 0:
      return !*s;
    case '?':
      if (!*s++)
	return 0;
      p++;
      break;
    case '*':
      if (!*++p)
	return 1;
      for (;;) {
	if (stringmatch(s, p))
	  return 1;
	if (!*s++)
	  return 0;
      }
    case '[':
      if (!(c = (*s++ & 0xff)))
	return 0;
      if ((reverse = (*++p == '!')))
	p++;
      match = 0;
      do {
	if (!(c1 = (*p++ & 0xff)))
	  return 0;
	match |= (c1 == c);
	if (*p == '-' && p[1] != ']') {
	  p++;
	  if (!(c2 = (*p++ & 0xff)))
	    return 0;
	  match |= (c1 <= c2) ? (c >= c1 && c <= c2) : (c >= c2 && c <= c1);
	}
      } while (*p != ']');
      if (match == reverse)
	return 0;
      p++;
      break;
    case '\\':
      if (!*++p)
	return 0;
    default:
      if (*s++ != *p++)
	return 0;
      break;
    }
}
