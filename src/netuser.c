/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/netuser.c,v 1.7 1991/02/24 20:17:26 deyke Exp $ */

/* Miscellaneous integer and IP address format conversion subroutines
 * Copyright 1991 Phil Karn, KA9Q
 */

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "timer.h"
#include "netuser.h"

int Net_error;

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

int32 aton(s)
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
  static long  lastmtime;
  static long  nextchecktime;
  struct hosttable *hp;
  struct stat statbuf;

  if (nextchecktime > secclock()) return;
  nextchecktime = secclock() + 60;
  if (stat("/tcp/hosts", &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > secclock() - 5) return;
  lastmtime = statbuf.st_mtime;
  while (hp = hosttable) {
    hosttable = hosttable->next;
    free(hp);
  }
  if (!(fp = fopen("/tcp/hosts", "r"))) return;
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

  if (n = aton(name)) return n;
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
  return 0;
}

/*---------------------------------------------------------------------------*/

char  *inet_ntoa(addr)
register int32 addr;
{

  char  buf[16];
  register struct hosttable *p, *q;

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
  return hosttable->name;
}

/*---------------------------------------------------------------------------*/

/* Convert hex-ascii string to long integer */
long
htol(s)
char *s;
{
	long ret;
	char c;

	ret = 0;
	while((c = *s++) != '\0'){
		c &= 0x7f;
		if(c == 'x')
			continue;       /* Ignore 'x', e.g., '0x' prefixes */
		if(c >= '0' && c <= '9')
			ret = ret*16 + (c - '0');
		else if(c >= 'a' && c <= 'f')
			ret = ret*16 + (10 + c - 'a');
		else if(c >= 'A' && c <= 'F')
			ret = ret*16 + (10 + c - 'A');
		else
			break;
	}
	return ret;
}
char *
pinet(s)
struct socket *s;
{
	static char buf[30];

	sprintf(buf,"%s:%u",inet_ntoa(s->address),s->port);
	return buf;
}

