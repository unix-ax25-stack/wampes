/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/filter.c,v 1.1 1992/08/13 19:00:52 deyke Exp $ */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/

static char  *strtrim(s)
register char  *s;
{
  register char  *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(*p & 0xff)) ;
  p[1] = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strpos(str, pat)
char  *str, *pat;
{
  register char  *sp, *pp;

  for (; ; str++)
    for (sp = str, pp = pat; ; ) {
      if (!*pp) return str;
      if (!*sp) return (char *) 0;
      if (*sp++ != *pp++) break;
    }
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{

  char  buf[1024];
  char  line[1024];
  int  i;
  int  pass = 0;

  while (gets(line)) {
    strtrim(line);
    if (!strncmp(line, "tnc", 3)) {
      strcpy(buf, line);
      gets(line);
      strtrim(line);
      for (pass = i = 1; i < argc; i++)
	if (!strpos(line, argv[i])) pass = 0;
      if (pass) {
	putchar('\n');
	puts(buf);
	puts(line);
      }
    } else if (pass)
      puts(line);
  }
}

