#define _HPUX_SOURCE

#include <ctype.h>
#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/

/* Use private function because some platforms are broken, eg 386BSD */

static int Xtolower(int c)
{
  return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

/*---------------------------------------------------------------------------*/

static char *strlwc(char *s)
{
  char *p;

  for (p = s; *p = Xtolower(*p); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *strtrim(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(*p & 0xff)) ;
  p[1] = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *mail_alias(const char *name)
{

  DBM * db;
  datum key, value;
  static char buf[1024];

  while (isspace(*name & 0xff)) name++;
  strlwc(strtrim(strcpy(buf, name)));
  if (db = dbm_open("/usr/lib/aliases", O_RDONLY, 0444)) {
    for (; ; ) {
      if (strchr(buf, '@') || strchr(buf, '!') || strchr(buf, ',')) break;
      key.dptr = buf;
      key.dsize = strlen(key.dptr) + 1;
      value = dbm_fetch(db, key);
      if (!value.dptr) break;
      while (isspace(*value.dptr & 0xff)) value.dptr++;
      if (!*value.dptr || strchr(value.dptr, ',')) break;
      strlwc(strtrim(strcpy(buf, value.dptr)));
    }
    dbm_close(db);
  }
  return buf;
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  if (argc >= 2) puts(mail_alias(argv[1]));
  return 0;
}

