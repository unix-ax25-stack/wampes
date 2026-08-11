/* @(#) $Id: domain.c,v 1.30 2016/03/13 06:37:28 dl9sau Exp $ */

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <sys/socket.h>

#include "configure.h"

#if HAS_NDBM
#include <ndbm.h>
#else
#if HAS_DB1_NDBM
#include <db1/ndbm.h>
#else
#if HAS_GDBM_NDBM
#include <gdbm-ndbm.h>
#else
#if HAS_GDBM
#include <gdbm.h>
#else
#error Cannot find ndbm.h header file
#endif
#endif
#endif
#endif

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "tcp.h"
#include "udp.h"
#include "timer.h"
#include "netuser.h"
#include "cmdparse.h"
#include "domain.h"
#include "hostdb.h"

#define DBHOSTADDR      TCPDIR "/hostaddr"
#define DBHOSTNAME      TCPDIR "/hostname"
#define LOCALDOMAIN     "ampr.org"

struct cache {
  struct cache *next;
  int32 addr;
  char name[1];
};

static int Dtrace = FALSE;
static char *Dtypes[] = {
	"",
	"A",
	"NS",
	"MD",
	"MF",
	"CNAME",
	"SOA",
	"MB",
	"MG",
	"MR",
	"NULL",
	"WKS",
	"PTR",
	"HINFO",
	"MINFO",
	"MX",
	"TXT"
};
static int Ndtypes = 17;

#if HAS_GDBM
static GDBM_FILE Dbhostaddr;
static GDBM_FILE Dbhostname;
#else
static DBM *Dbhostaddr;
static DBM *Dbhostname;
#endif
static int Usegethostby;
static int32 Nextcacheflushtime;
static struct cache *Cache;
static struct tcb *Domain_tcb;
static struct udp_cb *Domain_ucb;

static void strlwc(char *to, const char *from);
static int dotted_name(char *buf, size_t bufsize, const char *name);
static int isaddr(const char *s);
static void add_to_cache(const char *name, int32 addr);
static char *dtype(int value);
static struct rr *make_rr(int source, char *dname, int dclass, int d_type, int32 ttl, int rdl, void *data);
static void put_rr(FILE *fp, struct rr *rrp);
static void dumpdomain(struct dhdr *dhp);
static int32 in_addr_arpa(char *name);
static struct mbuf *domain_server(struct mbuf *bp);
static void domain_server_udp(struct iface *iface, struct udp_cb *up, int cnt);
static void domain_server_tcp_recv(struct tcb *tcb, int32 cnt);
static void domain_server_tcp_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new);
static int docacheflush(int argc, char *argv[], void *p);
static int docachelist(int argc, char *argv[], void *p);
static int docache(int argc, char *argv[], void *p);
static int dodnsquery(int argc, char *argv[], void *p);
static int dodnstrace(int argc, char *argv[], void *p);
static int dousegethostby(int argc, char *argv[], void *p);

/**
 **     Domain Resolver Commands
 **/

static struct cmds Dcmds[] = {
	{ "query",        dodnsquery,     0, 2, "domain query <name|addr>" },
	{ "trace",        dodnstrace,     0, 0, NULL },
	{ "cache",        docache,        0, 0, NULL },
	{ "usegethostby", dousegethostby, 0, 0, NULL },
	{ NULL }
};

static struct cmds Dcachecmds[] = {
	{ "list",         docachelist,    0, 0, NULL },
	{ "flush",        docacheflush,   0, 0, NULL },
	{ NULL }
};

int
dodomain(
int argc,
char *argv[],
void *p)
{
	return subcmd(Dcmds,argc,argv,p);
}

static int
docache(
int argc,
char *argv[],
void *p)
{
	return subcmd(Dcachecmds,argc,argv,p);
}

static int
docachelist(
int argc,
char *argv[],
void *p)
{
  struct cache *cp;

  for (cp = Cache; cp; cp = cp->next)
    printf("%-25.25s %ld.%ld.%ld.%ld\n",
	   cp->name,
	   (long)((cp->addr >> 24) & 0xff),
	   (long)((cp->addr >> 16) & 0xff),
	   (long)((cp->addr >>  8) & 0xff),
	   (long)((cp->addr      ) & 0xff));
  return 0;
}

static int
docacheflush(
int argc,
char *argv[],
void *p)
{
  struct cache *cp;

  while ((cp = Cache)) {
    Cache = cp->next;
    free(cp);
  }
  if (Dbhostaddr) {
#if HAS_GDBM
    gdbm_close(Dbhostaddr);
#else
    dbm_close(Dbhostaddr);
#endif
    Dbhostaddr = 0;
  }
  if (Dbhostname) {
#if HAS_GDBM
    gdbm_close(Dbhostaddr);
#else
    dbm_close(Dbhostname);
#endif
    Dbhostname = 0;
  }
  Nextcacheflushtime = secclock() + 86400;
  return 0;
}

static int
dodnsquery(
int argc,
char *argv[],
void *p)
{
  int32 addr;

  if (isaddr(argv[1])) {
    printf("%s\n", resolve_a(aton(argv[1]), 0));
  } else {
    if (!(addr = resolve(argv[1])))
      printf(Badhost, argv[1]);
    else
      printf("%ld.%ld.%ld.%ld\n",
	     (long)((addr >> 24) & 0xff),
	     (long)((addr >> 16) & 0xff),
	     (long)((addr >>  8) & 0xff),
	     (long)((addr      ) & 0xff));
  }
  return 0;
}

static int
dodnstrace(
int argc,
char *argv[],
void *p)
{
	return setbool(&Dtrace,"server trace",argc,argv);
}

static int
dousegethostby(
int argc,
char *argv[],
void *p)
{
  return setbool(&Usegethostby, "Using gethostby", argc, argv);
}

/*---------------------------------------------------------------------------*/

static void strlwc(
char *to,
const char *from)
{
  while ((*to++ = Xtolower(*from++))) ;
}

/*---------------------------------------------------------------------------*/

/* Copy a resolved host name into buf, making sure it ends in a dot.  Returns
 * 0 - leaving buf untouched - if the name is empty or would not fit.
 *
 * resolve_a() hands back cache entries whose length is bounded by nothing at
 * all: they come from the hostname dbm file or from gethostbyaddr().  The
 * callers used to strcpy() one of those into a 256-byte stack buffer and then
 * strcat() a dot onto it.  An empty name was equally unwelcome, because the
 * trailing-dot test read buffer[-1].
 */
static int dotted_name(
char *buf,
size_t bufsize,
const char *name)
{
  size_t len;

  if (!name) return 0;
  len = strlen(name);
  if (len == 0 || len + 2 > bufsize) return 0;
  memcpy(buf, name, len);
  if (buf[len-1] != '.') buf[len++] = '.';
  buf[len] = '\0';
  return 1;
}

/*---------------------------------------------------------------------------*/

static int isaddr(
const char *s)
{
  int c;

  if (s)
    while ((c = (*s++ & 0xff)))
      if (c != '[' && c != ']' && !isdigit(c) && c != '.') return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

/* The forms to try for a name: as given, and - unless it was already
 * absolute - with LOCALDOMAIN appended.  An empty string terminates the list.
 * Shared so that resolve() and resolve_sa() cannot drift apart.
 */
static void expand_names(
const char *name,
char names[3][1024])
{
  char *p;

  names[0][0] = names[1][0] = names[2][0] = 0;
  if (!name || !*name) return;
  strlwc(names[0], name);
  p = names[0] + strlen(names[0]) - 1;
  if (*p == '.') {
    *p = 0;
  } else {
    strcpy(names[1], names[0]);
    strcat(names[0], ".");
    strcat(names[0], LOCALDOMAIN);
  }
}

/*---------------------------------------------------------------------------*/

static void add_to_cache(
const char *name,
int32 addr)
{
  struct cache *cp;

  for (cp = Cache; cp; cp = cp->next)
    if (cp->addr == addr && !strcmp(cp->name, name)) return;
  cp = (struct cache *) malloc(sizeof(struct cache) + strlen(name));
  strcpy(cp->name, name);
  cp->addr = addr;
  cp->next = Cache;
  Cache = cp;
}

/*---------------------------------------------------------------------------*/

int32 resolve(
char *name)
{

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

  if (Nextcacheflushtime <= secclock()) docacheflush(0, 0, 0);

  expand_names(name, names);

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

#if HAS_GDBM
  if (Dbhostaddr || (Dbhostaddr = gdbm_open(DBHOSTADDR, 0, GDBM_READER, 0644, NULL)))
#else
  if (Dbhostaddr || (Dbhostaddr = dbm_open(DBHOSTADDR, O_RDONLY, 0644)))
#endif
    for (i = 0; names[i][0]; i++) {
      dname.dptr = names[i];
      dname.dsize = strlen(names[i]) + 1;
#if HAS_GDBM
      daddr = gdbm_fetch(Dbhostaddr, dname);
#else
      daddr = dbm_fetch(Dbhostaddr, dname);
#endif
      if (daddr.dptr) {
	int family;
	unsigned char a[16];

	/* An IPv6 entry cannot come back through this function - it returns
	 * an int32.  Callers that can take one use resolve_sa(). */
	if (hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, a)
	    == 4 && family == HOSTDB_V4) {
	  addr = ((int32) a[0] << 24) | ((int32) a[1] << 16) |
		 ((int32) a[2] << 8) | (int32) a[3];
	  add_to_cache(names[i], addr);
	  return addr;
	}
      }
    }

  if (Usegethostby && (hp = gethostbyname(name))) {
    addr = ntohl(((struct in_addr *)(hp->h_addr))->s_addr);
    strlwc(names[0], hp->h_name);
    add_to_cache(names[0], addr);
    return addr;
  }

  return 0;
}

/*---------------------------------------------------------------------------*/

/* Resolve a name, or an address literal of either family, to a sockaddr out
 * of WAMPES' own host table in TCPDIR/hosts.
 *
 * resolve() cannot report an IPv6 address - it returns an int32 - so callers
 * that can take one ask here instead.  Returns 1 on success, 0 if the name is
 * not in the table.  The cache is not consulted: it holds int32 only, and the
 * callers of this are configuration commands rather than hot paths.
 */

int resolve_sa(
const char *name,
struct sockaddr_storage *ss,
socklen_t *len)
{

  char names[3][1024];
  datum daddr;
  datum dname;
  int family;
  int i;
  unsigned char a[16];

  if (!name || !*name || !ss || !len) return 0;
  memset(ss, 0, sizeof(*ss));
  *len = 0;

  /* A literal needs no table */
  if (strchr(name, ':')) {
    struct in6_addr a6;

    if (inet_pton(AF_INET6, name, &a6) != 1) return 0;
    memcpy(a, &a6, 16);
    family = HOSTDB_V6;
  } else if (isaddr(name)) {
    struct in_addr a4;

    if (inet_pton(AF_INET, name, &a4) != 1) return 0;
    memcpy(a, &a4, 4);
    family = HOSTDB_V4;
  } else {
    if (Nextcacheflushtime <= secclock()) docacheflush(0, 0, 0);
    expand_names(name, names);
#if HAS_GDBM
    if (!Dbhostaddr && !(Dbhostaddr = gdbm_open(DBHOSTADDR, 0, GDBM_READER, 0644, NULL)))
      return 0;
#else
    if (!Dbhostaddr && !(Dbhostaddr = dbm_open(DBHOSTADDR, O_RDONLY, 0644)))
      return 0;
#endif
    for (i = 0; names[i][0]; i++) {
      dname.dptr = names[i];
      dname.dsize = strlen(names[i]) + 1;
#if HAS_GDBM
      daddr = gdbm_fetch(Dbhostaddr, dname);
#else
      daddr = dbm_fetch(Dbhostaddr, dname);
#endif
      if (daddr.dptr &&
	  hostdb_decode((unsigned char *) daddr.dptr, daddr.dsize, &family, a))
	break;
    }
    if (!names[i][0]) return 0;
  }

#if HAS_AF_INET6
  if (family == HOSTDB_V6) {
    struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) ss;

    s6->sin6_family = AF_INET6;
    memcpy(&s6->sin6_addr, a, 16);
    *len = sizeof(struct sockaddr_in6);
    return 1;
  }
#endif
  if (family != HOSTDB_V4) return 0;
  {
    struct sockaddr_in *si = (struct sockaddr_in *) ss;

    si->sin_family = AF_INET;
    memcpy(&si->sin_addr, a, 4);
    *len = sizeof(struct sockaddr_in);
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

char *resolve_a(
int32 addr,
int shorten)
{

  char buf[1024];
  datum daddr;
  datum dname;
  struct cache *curr;
  struct cache *prev;
  struct hostent *hp;
  struct in_addr in_addr;

  if (!addr) return "*";

  if (Nextcacheflushtime <= secclock()) docacheflush(0, 0, 0);

  for (prev = 0, curr = Cache; curr; prev = curr, curr = curr->next)
    if (curr->addr == addr) {
      if (prev) {
	prev->next = curr->next;
	curr->next = Cache;
	Cache = curr;
      }
      return Cache->name;
    }

#if HAS_GDBM
  if (Dbhostname || (Dbhostname = gdbm_open(DBHOSTNAME, 0, GDBM_READER, 0644, NULL))) {
#else
  if (Dbhostname || (Dbhostname = dbm_open(DBHOSTNAME, O_RDONLY, 0644))) {
#endif
    {
      unsigned char a[4];
      unsigned char rec[HOSTDB_RECLEN];

      a[0] = (unsigned char) (addr >> 24);
      a[1] = (unsigned char) (addr >> 16);
      a[2] = (unsigned char) (addr >> 8);
      a[3] = (unsigned char) addr;
      daddr.dptr = (char *) rec;
      daddr.dsize = hostdb_encode(HOSTDB_V4, a, rec);
#if HAS_GDBM
      dname = gdbm_fetch(Dbhostname, daddr);
#else
      dname = dbm_fetch(Dbhostname, daddr);
#endif
      if (!dname.dptr) {
	/* A database from an older mkhostdb keys on a bare int32 */
	daddr.dptr = (char *) &addr;
	daddr.dsize = sizeof(addr);
#if HAS_GDBM
	dname = gdbm_fetch(Dbhostname, daddr);
#else
	dname = dbm_fetch(Dbhostname, daddr);
#endif
      }
    }
    if (dname.dptr) {
      add_to_cache(dname.dptr, addr);
      return Cache->name;
    }
  }

  if (Usegethostby) {
    in_addr.s_addr = htonl(addr);
    hp = gethostbyaddr((char *) &in_addr, sizeof(in_addr), AF_INET);
    if (hp) {
      strlwc(buf, hp->h_name);
      add_to_cache(buf, addr);
      return Cache->name;
    }
  }

  sprintf(buf,
	  "%ld.%ld.%ld.%ld",
	  (long)((addr >> 24) & 0xff),
	  (long)((addr >> 16) & 0xff),
	  (long)((addr >>  8) & 0xff),
	  (long)((addr      ) & 0xff));
  add_to_cache(buf, addr);
  return Cache->name;
}

/*---------------------------------------------------------------------------*/

/**
 **     Domain Resource Record Utilities
 **/

static char *
dtype(
int value)
{
	static char buf[10];

	if (value < Ndtypes)
		return Dtypes[value];

	sprintf( buf, "{%d}", value);
	return buf;
}

/*---------------------------------------------------------------------------*/

/* Free (list of) resource records */
void
free_rr(
struct rr **rrlp)
{
	struct rr *rrp;
	struct rr *rrnext;

	if(rrlp == NULL || (rrp = *rrlp) == NULL)
		return;
	*rrlp = NULL;
	for(;rrp != NULL;rrp = rrnext){
		rrnext = rrp->next;

		FREE(rrp->comment);
		FREE(rrp->name);
		if(rrp->rdlength > 0){
			switch(rrp->type){
			case TYPE_A:
				break;  /* Nothing allocated in rdata section */
			case TYPE_CNAME:
			case TYPE_MB:
			case TYPE_MG:
			case TYPE_MR:
			case TYPE_NS:
			case TYPE_PTR:
			case TYPE_TXT:
				FREE(rrp->rdata.name);
				break;
			case TYPE_HINFO:
				FREE(rrp->rdata.hinfo.cpu);
				FREE(rrp->rdata.hinfo.os);
				break;
			case TYPE_MX:
				FREE(rrp->rdata.mx.exch);
				break;
			case TYPE_SOA:
				FREE(rrp->rdata.soa.mname);
				FREE(rrp->rdata.soa.rname);
				break;
			}
		}
		FREE(rrp);
	}
}

/*---------------------------------------------------------------------------*/

static struct rr *
make_rr(
int source,
char *dname,
int dclass,
int d_type,
int32 ttl,
int rdl,
void *data)
{
	struct rr *newrr;

	newrr = (struct rr *)callocw(1,sizeof(struct rr));
	newrr->source = source;
	newrr->name = strdup(dname);
	newrr->class = dclass;
	newrr->type = d_type;
	newrr->ttl = ttl;
	if((newrr->rdlength = rdl) == 0)
		return newrr;

	switch(d_type){
	case TYPE_A:
	  {
		int32 *ap = (int32 *)data;
		newrr->rdata.addr = *ap;
		break;
	  }
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
	  {
		newrr->rdata.name = strdup((char *)data);
		break;
	  }
	case TYPE_HINFO:
	  {
		struct hinfo *hinfop = (struct hinfo *)data;
		newrr->rdata.hinfo.cpu = strdup(hinfop->cpu);
		newrr->rdata.hinfo.os = strdup(hinfop->os);
		break;
	  }
	case TYPE_MX:
	  {
		struct mx *mxp = (struct mx *)data;
		newrr->rdata.mx.pref = mxp->pref;
		newrr->rdata.mx.exch = strdup(mxp->exch);
		break;
	  }
	case TYPE_SOA:
	  {
		struct soa *soap = (struct soa *)data;
		newrr->rdata.soa.mname =        strdup(soap->mname);
		newrr->rdata.soa.rname =        strdup(soap->rname);
		newrr->rdata.soa.serial =       soap->serial;
		newrr->rdata.soa.refresh =      soap->refresh;
		newrr->rdata.soa.retry =        soap->retry;
		newrr->rdata.soa.expire =       soap->expire;
		newrr->rdata.soa.minimum =      soap->minimum;
		break;
	  }
	}
	return newrr;
}

/*---------------------------------------------------------------------------*/

/* Print a resource record */
static void
put_rr(
FILE *fp,
struct rr *rrp)
{
	char * stuff;

	if(fp == NULL || rrp == NULL)
		return;

	if(rrp->name == NULL && rrp->comment != NULL){
		fprintf(fp,"%s",rrp->comment);
		return;
	}

	fprintf(fp,"%s",rrp->name);
	if(rrp->ttl != TTL_MISSING)
		fprintf(fp,"\t%ld",(long)rrp->ttl);
	if(rrp->class == CLASS_IN)
		fprintf(fp,"\tIN");
	else
		fprintf(fp,"\t<%u>",rrp->class);

	stuff = dtype(rrp->type);
	fprintf(fp,"\t%s",stuff);
	if(rrp->rdlength == 0){
		/* Null data portion, indicates nonexistent record */
		/* or unsupported type.  Hopefully, these will filter */
		/* as time goes by. */
		fprintf(fp,"\n");
		return;
	}
	switch(rrp->type){
	case TYPE_A:
		fprintf(fp,"\t%ld.%ld.%ld.%ld\n",
			(long)((rrp->rdata.addr >> 24) & 0xff),
			(long)((rrp->rdata.addr >> 16) & 0xff),
			(long)((rrp->rdata.addr >>  8) & 0xff),
			(long)((rrp->rdata.addr      ) & 0xff));
		break;
	case TYPE_CNAME:
	case TYPE_MB:
	case TYPE_MG:
	case TYPE_MR:
	case TYPE_NS:
	case TYPE_PTR:
	case TYPE_TXT:
		/* These are all printable text strings */
		fprintf(fp,"\t%s\n",rrp->rdata.name);
		break;
	case TYPE_HINFO:
		fprintf(fp,"\t%s\t%s\n",
		 rrp->rdata.hinfo.cpu,
		 rrp->rdata.hinfo.os);
		break;
	case TYPE_MX:
		fprintf(fp,"\t%u\t%s\n",
		 rrp->rdata.mx.pref,
		 rrp->rdata.mx.exch);
		break;
	case TYPE_SOA:
		fprintf(fp,"\t%s\t%s\t%lu\t%lu\t%lu\t%lu\t%lu\n",
		 rrp->rdata.soa.mname,rrp->rdata.soa.rname,
		 (unsigned long)rrp->rdata.soa.serial,
		 (unsigned long)rrp->rdata.soa.refresh,
		 (unsigned long)rrp->rdata.soa.retry,
		 (unsigned long)rrp->rdata.soa.expire,
		 (unsigned long)rrp->rdata.soa.minimum);
		break;
	default:
		fprintf(fp,"\n");
		break;
	}
}

/*---------------------------------------------------------------------------*/

/**
 **     Domain Server Utilities
 **/

static void
dumpdomain(
struct dhdr *dhp)
{
	struct rr *rrp;
	char * stuff;

	printf("id %u qr %u opcode %u aa %u tc %u rd %u ra %u rcode %u\n",
	 dhp->id,
	 dhp->qr,dhp->opcode,dhp->aa,dhp->tc,dhp->rd,
	 dhp->ra,dhp->rcode);
	printf("%u questions:\n",dhp->qdcount);
	for(rrp = dhp->questions; rrp != NULL; rrp = rrp->next){
		stuff = dtype(rrp->type);
		printf("%s type %s class %u\n",rrp->name,
		 stuff,rrp->class);
	}
	printf("%u answers:\n",dhp->ancount);
	for(rrp = dhp->answers; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u authority:\n",dhp->nscount);
	for(rrp = dhp->authority; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u additional:\n",dhp->arcount);
	for(rrp = dhp->additional; rrp != NULL; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static int32 in_addr_arpa(
char *name)
{
  int32 addr;

  for (addr = 0; isdigit(*name & 0xff); name++) {
    addr = ((addr >> 8) & 0xffffff) | (atol(name) << 24);
    if (!(name = strchr(name, '.'))) return 0;
  }
  return stricmp(name, "in-addr.arpa.") ? 0 : addr;
}

/*---------------------------------------------------------------------------*/

static struct mbuf *domain_server(
struct mbuf *bp)
{

  char *cp = 0;
  char buffer[256];
  int32 addr;
  struct dhdr *dhp;
  struct rr *qp;
  struct rr *rrp;

  dhp = (struct dhdr *) malloc(sizeof(struct dhdr));
  if (ntohdomain(dhp, &bp)) goto Done;
  if (Dtrace) {
    printf("recv: ");
    dumpdomain(dhp);
  }
  if (dhp->qr != QUERY) goto Done;
  dhp->qr = RESPONSE;
  dhp->aa = 0;
  dhp->tc = 0;
  dhp->ra = 0;
  switch (dhp->opcode) {
  case SQUERY:
    dhp->rcode = NO_ERROR;
    for (qp = dhp->questions; qp; qp = qp->next) {
      if ((qp->class == CLASS_IN || qp->class == CLASS_ANY) &&
	  (qp->type == TYPE_A || qp->type == TYPE_ANY) &&
	  (addr = resolve(qp->name))) {
	rrp = make_rr(RR_NONE, qp->name, CLASS_IN, TYPE_A, 86400, sizeof(addr), &addr);
	rrp->next = dhp->answers;
	dhp->answers = rrp;
	dhp->ancount++;
      }
      if ((qp->class == CLASS_IN || qp->class == CLASS_ANY) &&
	  (qp->type == TYPE_PTR || qp->type == TYPE_ANY) &&
	  (addr = in_addr_arpa(qp->name)) &&
	  !isaddr(cp = resolve_a(addr, 0)) &&
	  dotted_name(buffer, sizeof(buffer), cp)) {
	rrp = make_rr(RR_NONE, qp->name, CLASS_IN, TYPE_PTR, 86400, strlen(buffer) + 1, buffer);
	rrp->next = dhp->answers;
	dhp->answers = rrp;
	dhp->ancount++;
      }
    }
    break;
  case IQUERY:
    dhp->rcode = NO_ERROR;
    for (qp = dhp->answers; qp; qp = qp->next) {
      if (qp->class == CLASS_IN &&
	  qp->type == TYPE_A &&
	  !isaddr(cp = resolve_a(qp->rdata.addr, 0)) &&
	  dotted_name(buffer, sizeof(buffer), cp)) {
	rrp = make_rr(RR_NONE, buffer, CLASS_IN, TYPE_A, 86400, sizeof(qp->rdata.addr), &qp->rdata.addr);
	rrp->next = dhp->questions;
	dhp->questions = rrp;
	dhp->qdcount++;
	free(qp->name);
	qp->name = strdup(rrp->name);
	qp->ttl = rrp->ttl;
      }
      if (qp->class == CLASS_IN &&
	  qp->type == TYPE_PTR &&
	  (addr = resolve(qp->rdata.name))) {
	snprintf(buffer, sizeof(buffer), "%ld.%ld.%ld.%ld.in-addr.arpa.",
		(long)((addr      ) & 0xff),
		(long)((addr >>  8) & 0xff),
		(long)((addr >> 16) & 0xff),
		(long)((addr >> 24) & 0xff));
	rrp = make_rr(RR_NONE, buffer, CLASS_IN, TYPE_PTR, 86400, strlen(qp->rdata.name) + 1, qp->rdata.name);
	rrp->next = dhp->questions;
	dhp->questions = rrp;
	dhp->qdcount++;
	free(qp->name);
	qp->name = strdup(rrp->name);
	qp->ttl = rrp->ttl;
      }
    }
    break;
  default:
    dhp->rcode = NOT_IMPL;
    break;
  }
  if (Dtrace) {
    printf("sent: ");
    dumpdomain(dhp);
  }
  bp = htondomain(dhp);

Done:
  free_rr(&dhp->questions);
  free_rr(&dhp->answers);
  free_rr(&dhp->authority);
  free_rr(&dhp->additional);
  free(dhp);
  return bp;
}

/*---------------------------------------------------------------------------*/

static void domain_server_udp(
struct iface *iface,
struct udp_cb *up,
int cnt)
{

  struct mbuf *bp;
  struct socket fsocket;

  /* recv_udp() returns -1 without touching bp, which would leave it an
   * uninitialized stack pointer for domain_server() below.
   */
  if (recv_udp(up, &fsocket, &bp) < 0) return;
  bp = domain_server(bp);
  if (bp) send_udp(&up->socket, &fsocket, 0, 0, &bp, 0, 0, 0);
}

/*---------------------------------------------------------------------------*/

static void domain_server_tcp_recv(struct tcb *tcb, int32 cnt)
{

  int len;
  struct mbuf **rcvqptr;
  struct mbuf *bp;

  if (recv_tcp(tcb, &bp, cnt) <= 0) return;
  rcvqptr = (struct mbuf **) &tcb->user;
  append(rcvqptr, &bp);
  if (len_p(*rcvqptr) < 2) return;
  len = (int) pull16(rcvqptr);
  if (len_p(*rcvqptr) < len) {
    pushdown(rcvqptr, NULL, 2);
    put16((*rcvqptr)->data, len);
    return;
  }
  dup_p(&bp, *rcvqptr, 0, len);
  pullup(rcvqptr, NULL, len);
  bp = domain_server(bp);
  if (!bp) return;
  len = len_p(bp);
  pushdown(&bp, NULL, 2);
  put16(bp->data, len);
  send_tcp(tcb, &bp);
}

/*---------------------------------------------------------------------------*/

static void domain_server_tcp_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new)
{
  switch (new) {
  case TCP_ESTABLISHED:
    tcb->user = 0;
    logmsg(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    break;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    break;
  case TCP_CLOSED:
    free_p((struct mbuf **) &tcb->user);
    logmsg(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
    del_tcp(&tcb);
    if (tcb == Domain_tcb) Domain_tcb = 0;
    break;
  default:
    break;
  }
}

/*---------------------------------------------------------------------------*/

int domain0(
int argc,
char *argv[],
void *p)
{
  if (Domain_ucb) {
    del_udp(&Domain_ucb);
    Domain_ucb = 0;
  }
  if (Domain_tcb) {
    close_tcp(Domain_tcb);
    Domain_tcb = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int domain1(
int argc,
char *argv[],
void *p)
{
  struct socket lsocket;

  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_DOMAIN : udp_port_number(argv[1]);
  if (Domain_ucb) del_udp(&Domain_ucb);
  Domain_ucb = open_udp(&lsocket, domain_server_udp);

  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_DOMAIN : tcp_port_number(argv[1]);
  if (Domain_tcb) close_tcp(Domain_tcb);
  Domain_tcb = open_tcp(&lsocket, NULL, TCP_SERVER, 0, domain_server_tcp_recv, NULL, domain_server_tcp_state, 0, 0);

  return 0;
}
