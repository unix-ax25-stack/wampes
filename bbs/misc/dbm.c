#define _HPUX_SOURCE

#include <fcntl.h>
#include <ndbm.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{

  DBM * db;
  datum key, value;

  if (argc < 2) return 2;
  if (!(db = dbm_open("/usr/lib/mail/paths", O_RDONLY, 0444))) return 1;
  key.dptr = argv[1];
  key.dsize = strlen(key.dptr) + 1;
  value = dbm_fetch(db, key);
  puts(value.dptr);
  dbm_close(db);
  return 0;
}

