/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strmatch.c,v 1.3 1990/10/12 12:46:39 deyke Exp $ */

#include "global.h"

/*
 *
 *  *       Matches any string, including the null string.
 *
 *  ?       Matches any single character.
 *
 *  [...]   Matches any one of the enclosed characters. A pair of characters
 *          separated by - matches any character lexically between the pair,
 *          inclusive. A NOT operator, !, can be specified immediately following
 *          the left bracket to match any single character not enclosed in the
 *          brackets.
 *
 *  \       Removes any special meaning of the following character.
 *
 *          Any other character matches itself.
 *
 */

int  strmatch(s, p)
char  *s, *p;
{
  int  c, c1, c2, match, reverse;

  for (; ; )
    switch (*p) {
    case 0:
      return !*s;
    case '?':
      if (!*s++) return 0;
      p++;
      break;
    case '*':
      if (!*++p) return 1;
      for (; ; ) {
	if (strmatch(s, p)) return 1;
	if (!*s++) return 0;
      }
    case '[':
      if (!(c = uchar(*s++))) return 0;
      if (reverse = (*++p == '!')) p++;
      match = 0;
      do {
	if (!(c1 = uchar(*p++))) return 0;
	match |= (c1 == c);
	if (*p == '-' && p[1] != ']') {
	  p++;
	  if (!(c2 = uchar(*p++))) return 0;
	  match |= (c1 <= c2) ? (c >= c1 && c <= c2) : (c >= c2 && c <= c1);
	}
      } while (*p != ']');
      if (match == reverse) return 0;
      p++;
      break;
    case '\\':
      if (!*++p) return 0;
    default:
      if (*s++ != *p++) return 0;
      break;
    }
}

