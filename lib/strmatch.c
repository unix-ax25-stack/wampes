int  strmatch(s, p)
register unsigned char  *s;
register unsigned char  *p;

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

{

  int  flag;
  int  notflag;
  unsigned char  *x;
  unsigned char  c1;
  unsigned char  c2;

  for (; ; )
    switch (*p) {
    case 0:
      return !*s;
    case '?':
      if (!*s) return 0;
      s++;
      p++;
      break;
    case '*':
      if (!*++p) return 1;
      for (; ; ) {
	if (strmatch(s, p)) return 1;
	if (!*s) return 0;
	s++;
      }
    case '[':
      if (!*s) return 0;
      p++;
      if (notflag = (*p == '!')) p++;
      if (!*p) return 0;
      for (x = p + 1; *x != ']'; x++) if (!*x) return 0;
      flag = 0;
      do {
	flag |= ((c1 = *p++) == *s);
	if (*p == '-' && p[1] != ']') {
	  p++;
	  c2 = *p++;
	  flag |= (c1 <= c2) ? (*s >= c1 && *s <= c2) : (*s >= c2 && *s <= c1);
	}
      } while (*p != ']');
      if (flag == notflag) return 0;
      s++;
      p++;
      break;
    case '\\':
      if (!*++p || *s++ != *p++) return 0;
      break;
    default:
      if (*s++ != *p++) return 0;
      break;
    }
}
