/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/domain.c,v 1.2 1991/07/17 14:59:01 deyke Exp $ */

#include <sys/types.h>

#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "global.h"
#include "timer.h"
#include "netuser.h"
#include "cmdparse.h"

#define HOSTSFILE       "/tcp/hosts"
#define LOCALDOMAIN     ".ampr.org"

struct hosttable {
  struct hosttable *next;
  int32 addr;
  char name[1];
};

static int Useserver;
static struct hosttable *hosttable;

static void strlwc __ARGS((char *to, const char *from));
static int isaddr __ARGS((const char *s));
static void add_to_hosttable __ARGS((const char *name, int32 addr));
static void load_hosttable __ARGS((void));
static int32 search_name_in_hosttable __ARGS((const char *name));
static int doduseserver __ARGS((int argc, char *argv [], void *p));

/*---------------------------------------------------------------------------*/

static void strlwc(to, from)
char *to;
const char *from;
{
  while (*to++ = tolower(uchar(*from++))) ;
}

/*---------------------------------------------------------------------------*/

static int isaddr(s)
const char *s;
{
  int c;

  if (s)
    while (c = uchar(*s++))
      if (c != '[' && c != ']' && !isdigit(c) && c != '.') return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

static void add_to_hosttable(name, addr)
const char *name;
int32 addr;
{
  struct hosttable *hp;

  hp = malloc(sizeof(struct hosttable ) + strlen(name));
  strlwc(hp->name, name);
  hp->addr = addr;
  hp->next = hosttable;
  hosttable = hp;
}

/*---------------------------------------------------------------------------*/

static void load_hosttable()
{

  FILE * fp;
  char *p;
  char line[1024], addr[1024], name[1024];
  int32 n;
  static long lastmtime;
  static long nextchecktime;
  struct hosttable *hp;
  struct stat statbuf;

  if (nextchecktime > secclock()) return;
  nextchecktime = secclock() + 60;
  if (stat(HOSTSFILE, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > secclock() - 5)
    return;
  lastmtime = statbuf.st_mtime;
  while (hp = hosttable) {
    hosttable = hosttable->next;
    free(hp);
  }
  if (!(fp = fopen(HOSTSFILE, "r"))) return;
  while (fgets(line, sizeof(line), fp)) {
    if (p = strchr(line, '#')) *p = '\0';
    if (sscanf(line, "%s %s", addr, name) == 2 &&
	isaddr(addr) &&
	(n = aton(addr))) {
      strcat(name, LOCALDOMAIN);
      add_to_hosttable(name, n);
    }
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static int32 search_name_in_hosttable(name)
const char *name;
{
  struct hosttable *p, *q;

  load_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (!strcmp(name, p->name)) {
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

int32 resolve(name)
char *name;
{

  char *p;
  char full_name[1024];
  char lwc_name[1024];
  int32 addr;
  struct hostent *hp;

  if (isaddr(name))
    return aton(name);

  strlwc(lwc_name, name);
  p = lwc_name + strlen(lwc_name) - 1;
  if (*p == '.') {
    *p = '\0';
    if (addr = search_name_in_hosttable(lwc_name))
      return addr;
    if (Useserver && (hp = gethostbyname(name)))
      return ntohl(((struct in_addr *)(hp->h_addr))->s_addr);
    return 0;
  }

  if (!strstr(lwc_name, LOCALDOMAIN)) {
    strcpy(full_name, lwc_name);
    strcat(full_name, LOCALDOMAIN);
    if (addr = search_name_in_hosttable(full_name))
      return addr;
  }

  if (addr = search_name_in_hosttable(lwc_name))
    return addr;
  if (Useserver && (hp = gethostbyname(name)))
    return ntohl(((struct in_addr *)(hp->h_addr))->s_addr);

  return 0;
}

/*---------------------------------------------------------------------------*/

char *resolve_a(addr, shorten)
int32 addr;
int shorten;
{

  char buf[16];
  struct hostent *hp;
  struct hosttable *p, *q;
  struct in_addr in_addr;

  if (!addr)
    return "*";

  load_hosttable();
  for (q = 0, p = hosttable; p; q = p, p = p->next)
    if (addr == p->addr) {
      if (q) {
	q->next = p->next;
	p->next = hosttable;
	hosttable = p;
      }
      return hosttable->name;
    }

  if (Useserver) {
    in_addr.s_addr = htonl(addr);
    hp = gethostbyaddr((char *) & in_addr, sizeof(in_addr), AF_INET);
    if (hp) {
      add_to_hosttable(hp->h_name, addr);
      return hosttable->name;
    }
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

static struct cmds Dcmds[] = {
  "useserver", doduseserver, 0, 0, NULLCHAR,
  NULLCHAR,    NULLFP,       0, 0, NULLCHAR
};

int dodomain(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  return subcmd(Dcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int doduseserver(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  return setbool(&Useserver, "Using server", argc, argv);
}

