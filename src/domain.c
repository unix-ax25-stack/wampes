/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/domain.c,v 1.4 1992/11/12 15:20:32 deyke Exp $ */

#include "global.h"

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <ndbm.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "udp.h"
#include "netuser.h"
#include "cmdparse.h"

#define DBHOSTADDR      "/tcp/hostaddr"
#define DBHOSTNAME      "/tcp/hostname"
#define LOCALDOMAIN     "ampr.org"

struct cache {
  struct cache *next;
  int32 addr;
  char name[1];
};

static DBM *Dbhostaddr;
static DBM *Dbhostname;
static int Useserver;
static struct cache *Cache;
static struct udp_cb *Domain_up;

static void strlwc __ARGS((char *to, const char *from));
static int isaddr __ARGS((const char *s));
static void add_to_cache __ARGS((const char *name, int32 addr));
static void domain_server __ARGS((struct iface *iface, struct udp_cb *up, int cnt));
static int docacheflush __ARGS((int argc, char *argv [], void *p));
static int docachelist __ARGS((int argc, char *argv [], void *p));
static int docache __ARGS((int argc, char *argv [], void *p));
static int doquery __ARGS((int argc, char *argv [], void *p));
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

static void add_to_cache(name, addr)
const char *name;
int32 addr;
{
  struct cache *cp;

  for (cp = Cache; cp; cp = cp->next)
    if (cp->addr == addr && !strcmp(cp->name, name)) return;
  cp = malloc(sizeof(*cp) + strlen(name));
  strcpy(cp->name, name);
  cp->addr = addr;
  cp->next = Cache;
  Cache = cp;
}

/*---------------------------------------------------------------------------*/

int32 resolve(name)
char *name;
{

  char *p;
  char names[3][1024];
  datum daddr;
  datum dname;
  int i;
  int32 addr;
  struct cache *curr;
  struct cache *prev;
  struct hostent *hp;

  if (!name || !*name) return 0;

  if (isaddr(name)) return aton(name);

  strlwc(names[0], name);
  p = names[0] + strlen(names[0]) - 1;
  if (*p == '.') {
    *p = 0;
    names[1][0] = 0;
  } else {
    strcpy(names[1], names[0]);
    strcat(names[0], ".");
    strcat(names[0], LOCALDOMAIN);
    names[2][0] = 0;
  }

  for (i = 0; names[i][0]; i++) {
    for (prev = 0, curr = Cache; curr; prev = curr, curr = curr->next)
      if (!strcmp(curr->name, names[i])) {
	if (prev) {
	  prev->next = curr->next;
	  curr->next = Cache;
	  Cache = curr;
	}
	return curr->addr;
      }
  }

  if (Dbhostaddr || (Dbhostaddr = dbm_open(DBHOSTADDR, O_RDONLY, 0644)))
    for (i = 0; names[i][0]; i++) {
      dname.dptr = names[i];
      dname.dsize = strlen(names[i]) + 1;
      daddr = dbm_fetch(Dbhostaddr, dname);
      if (daddr.dptr) {
	memcpy(&addr, daddr.dptr, sizeof(addr));
	add_to_cache(names[i], addr);
	return addr;
      }
    }

  if (Useserver && (hp = gethostbyname(name))) {
    addr = ntohl(((struct in_addr *)(hp->h_addr))->s_addr);
    strlwc(names[0], hp->h_name);
    add_to_cache(names[0], addr);
    return addr;
  }

  return 0;
}

/*---------------------------------------------------------------------------*/

char *resolve_a(addr, shorten)
int32 addr;
int shorten;
{

  char buf[1024];
  datum daddr;
  datum dname;
  struct cache *curr;
  struct cache *prev;
  struct hostent *hp;
  struct in_addr in_addr;

  if (!addr) return "*";

  for (prev = 0, curr = Cache; curr; prev = curr, curr = curr->next)
    if (curr->addr == addr) {
      if (prev) {
	prev->next = curr->next;
	curr->next = Cache;
	Cache = curr;
      }
      return Cache->name;
    }

  if (Dbhostname || (Dbhostname = dbm_open(DBHOSTNAME, O_RDONLY, 0644))) {
    daddr.dptr = (char *) & addr;
    daddr.dsize = sizeof(addr);
    dname = dbm_fetch(Dbhostname, daddr);
    if (dname.dptr) {
      add_to_cache(dname.dptr, addr);
      return Cache->name;
    }
  }

  if (Useserver) {
    in_addr.s_addr = htonl(addr);
    hp = gethostbyaddr((char *) & in_addr, sizeof(in_addr), AF_INET);
    if (hp) {
      strlwc(buf, hp->h_name);
      add_to_cache(buf, addr);
      return Cache->name;
    }
  }

  sprintf(buf,
	  "%u.%u.%u.%u",
	  uchar(addr >> 24),
	  uchar(addr >> 16),
	  uchar(addr >>  8),
	  uchar(addr      ));
  add_to_cache(buf, addr);
  return Cache->name;
}

/*---------------------------------------------------------------------------*/

static void domain_server(iface, up, cnt)
struct iface *iface;
struct udp_cb *up;
int cnt;
{

  struct mbuf *bp;
  struct socket fsock;

  recv_udp(up, &fsock, &bp);
  free_p(bp);
}

/*---------------------------------------------------------------------------*/

int domain0(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  if (Domain_up) {
    del_udp(Domain_up);
    Domain_up = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int domain1(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  struct socket sock;

  sock.address = INADDR_ANY;
  if (argc < 2)
    sock.port = IPPORT_DOMAIN;
  else
    sock.port = atoi(argv[1]);
  if (Domain_up) del_udp(Domain_up);
  Domain_up = open_udp(&sock, domain_server);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int docacheflush(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  struct cache *cp;

  while (cp = Cache) {
    Cache = cp->next;
    free(cp);
  }
  if (Dbhostaddr) {
    dbm_close(Dbhostaddr);
    Dbhostaddr = 0;
  }
  if (Dbhostname) {
    dbm_close(Dbhostname);
    Dbhostname = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int docachelist(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  struct cache *cp;

  for (cp = Cache; cp; cp = cp->next)
    printf("%-25.25s %u.%u.%u.%u\n",
	   cp->name,
	   uchar(cp->addr >> 24),
	   uchar(cp->addr >> 16),
	   uchar(cp->addr >>  8),
	   uchar(cp->addr      ));
  return 0;
}

/*---------------------------------------------------------------------------*/

static struct cmds Dcachecmds[] = {

  "flush", docacheflush, 0, 0, NULLCHAR,
  "list",  docachelist,  0, 0, NULLCHAR,

  NULLCHAR, NULLFP,      0, 0, NULLCHAR
};

static int docache(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  return subcmd(Dcachecmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int doquery(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  int32 addr;

  if (isaddr(argv[1])) {
    printf("%s\n", resolve_a(aton(argv[1]), 0));
  } else {
    if (!(addr = resolve(argv[1])))
      printf(Badhost, argv[1]);
    else
      printf("%u.%u.%u.%u\n",
	     uchar(addr >> 24),
	     uchar(addr >> 16),
	     uchar(addr >>  8),
	     uchar(addr      ));
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doduseserver(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  return setbool(&Useserver, "Using server", argc, argv);
}

/*---------------------------------------------------------------------------*/

static struct cmds Dcmds[] = {

  "cache",     docache,      0, 0, NULLCHAR,
  "query",     doquery,      0, 2, "domain query <name|addr>",
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

