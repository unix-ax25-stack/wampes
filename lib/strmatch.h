/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/strmatch.h,v 1.1 1994/11/08 14:26:07 deyke Exp $ */

#ifndef _STRMATCH_H
#define _STRMATCH_H

/*

   *       Matches any string, including the null string.

   ?       Matches any single character.

   [...]   Matches any one of the enclosed characters.  A pair of
	   characters separated by - matches any character lexically
	   between the pair, inclusive.  A NOT operator, !, can be
	   specified immediately following the left bracket to match any
	   single character not enclosed in the brackets.

   \       Removes any special meaning of the following character.

	   Any other character matches itself.

 */

/* In strmatch.c: */
int stringmatch(const char *s, const char *p);

#endif  /* _STRMATCH_H */
