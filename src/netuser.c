/* Miscellaneous format conversion subroutines */

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "netuser.h"
#include "hpux.h"

extern void free();

int net_error;

static struct hosttable {
  struct hosttable *next;
  int32 addr;
  char  name[1];
} *hosttable;

/*---------------------------------------------------------------------------*/

static void strlwc(to, from)
register char  *to, *from;
{
  while (*to++ = tolower(uchar(*from++))) ;
}

/*---------------------------------------------------------------------------*/

static int32 aton(s)
register char  *s;
{

  register int  i;
  register int32 n;

  if (*s == '[') s++;
  for (n = 0, i = 24; i >= 0; i -= 8) {
    n |= atoi(s) << i;
    if (!(s = strchr(s, '.'))) break;
    s++;
  }
  return n;
}

/*---------------------------------------------------------------------------*/

static void add_to_hosttable(name, addr)
char  *name;
int32 addr;
{
  struct hosttable *hp;

  hp = (struct hosttable *) malloc(sizeof(struct hosttable ) + strlen(name));
  strlwc(hp->name, name);
  hp->addr = addr;
  hp->next = hosttable;
  hosttable = hp;
}

/*---------------------------------------------------------------------------*/

static void read_hosttable()
{

  FILE * fp;
  char  line[1024], addr[1024], name[1024];
  int32 n;
  register char  *p;
  static long  laststat, lastmtime;
  struct hosttable *hp;
  struct stat statbuf;

  if (laststat == currtime / 60) return;
  laststat = currtime / 60;
  if (stat(hosts, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > currtime - 5) return;
  lastmtime = statbuf.st_mtime;
  while (hp = hosttable) {
    hosttable = hosttable->next;
    free((char *) hp);
  }
  if (!(fp = fopen(hosts, "r"))) return;
  while (fgets(line, sizeof(line), fp)) {
    if (p = strchr(line, '#')) *p = '\0';
    if (sscanf(line, "%s %s", addr, name) == 2 && (n = aton(addr)))
      add_to_hosttable(name, n);
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

int32 resolve(name)
register char  *name;
{

  char  lname[1024];
  int32 n;
  register struct hosttable *p, *q;

  strlwc(lname, name);
  read_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (!strcmp(lname, p->name)) {
      if (q) {
	q->next = p->next;
	p->next = hosttable;
	hosttable = p;
      }
      return p->addr;
    }
  add_to_hosttable(lname, n = aton(lname));
  return n;
}

/*---------------------------------------------------------------------------*/

char  *inet_ntoa(addr)
register int32 addr;
{

  register struct hosttable *p, *q;
  static char  buf[16];

  if (!addr) return "*";
  read_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (addr == p->addr) {
      if (q) {
	q->next = p->next;
	p->next = hosttable;
	hosttable = p;
      }
      return p->name;
    }
  sprintf(buf,
	  "%u.%u.%u.%u",
	  uchar(addr >> 24),
	  uchar(addr >> 16),
	  uchar(addr >>  8),
	  uchar(addr      ));
  add_to_hosttable(buf, addr);
  return buf;
}

/*---------------------------------------------------------------------------*/

char  *psocket(s)
struct socket *s;
{
  static char  buf[64];

  sprintf(buf, "%s:%s", inet_ntoa(s->address), tcp_port(s->port));
  return buf;
}

/*---------------------------------------------------------------------------*/

long  htol(s)
register char  *s;
{
  register int  c;
  register long  l;

  for (l = 0; isxdigit(c = (*s++ & 0x7f)); ) {
    if (c > '9') c -= 7;
    l = (l << 4) | (c & 0xf);
  }
  return l;
}

