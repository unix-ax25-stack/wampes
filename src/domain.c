/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/domain.c,v 1.14 1993/09/10 16:05:20 deyke Exp $ */

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

#define DBHOSTADDR      "/tcp/hostaddr"
#define DBHOSTNAME      "/tcp/hostname"
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

static DBM *Dbhostaddr;
static DBM *Dbhostname;
static int Usegethostby;
static int32 Nextcacheflushtime;
static struct cache *Cache;
static struct tcb *Domain_tcb;
static struct udp_cb *Domain_ucb;

static void strlwc(char *to, const char *from);
static int isaddr(const char *s);
static void add_to_cache(const char *name, int32 addr);
static char *dtype(int value);
static struct rr *make_rr(int source, char *dname, int dclass, int dtype, int32 ttl, int rdl, void *data);
static void put_rr(FILE *fp, struct rr *rrp);
static void dumpdomain(struct dhdr *dhp);
static int32 in_addr_arpa(char *name);
static struct mbuf *domain_server(struct mbuf *bp);
static void domain_server_udp(struct iface *iface, struct udp_cb *up, int cnt);
static void domain_server_tcp_recv(struct tcb *tcb, int cnt);
static void domain_server_tcp_state(struct tcb *tcb, int old, int new);
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
	"query",        dodnsquery,   512, 2, "domain query <name|addr>",
	"trace",        dodnstrace,     0, 0, NULLCHAR,
	"cache",        docache,        0, 0, NULLCHAR,
	"usegethostby", dousegethostby, 0, 0, NULLCHAR,
	NULLCHAR,
};

static struct cmds Dcachecmds[] = {
	"list",         docachelist,  512, 0, NULLCHAR,
	"flush",        docacheflush,   0, 0, NULLCHAR,
	NULLCHAR,
};

int
dodomain(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Dcmds,argc,argv,p);
}

static int
docache(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Dcachecmds,argc,argv,p);
}

static int
docachelist(argc,argv,p)
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

static int
docacheflush(argc,argv,p)
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
  Nextcacheflushtime = secclock() + 86400;
  return 0;
}

static int
dodnsquery(argc,argv,p)
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

static int
dodnstrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setbool(&Dtrace,"server trace",argc,argv);
}

static int
dousegethostby(argc,argv,p)
int argc;
char *argv[];
void *p;
{
  return setbool(&Usegethostby, "Using gethostby", argc, argv);
}

/*---------------------------------------------------------------------------*/

static void strlwc(to, from)
char *to;
const char *from;
{
  while (*to++ = Xtolower(*from++)) ;
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

  if (Nextcacheflushtime <= secclock()) docacheflush(0, (char **) 0, (void *) 0);

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
	memcpy((char *) &addr, daddr.dptr, sizeof(addr));
	add_to_cache(names[i], addr);
	return addr;
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

  if (Nextcacheflushtime <= secclock()) docacheflush(0, (char **) 0, (void *) 0);

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

  if (Usegethostby) {
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

/**
 **     Domain Resource Record Utilities
 **/

static char *
dtype(value)
int value;
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
free_rr(rrlp)
register struct rr *rrlp;
{
	register struct rr *rrp;

	while((rrp = rrlp) != NULLRR){
		rrlp = rrlp->next;

		free(rrp->comment);
		free(rrp->name);
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
				free(rrp->rdata.name);
				break;
			case TYPE_HINFO:
				free(rrp->rdata.hinfo.cpu);
				free(rrp->rdata.hinfo.os);
				break;
			case TYPE_MX:
				free(rrp->rdata.mx.exch);
				break;
			case TYPE_SOA:
				free(rrp->rdata.soa.mname);
				free(rrp->rdata.soa.rname);
				break;
			}
		}
		free((char *)rrp);
	}
}

/*---------------------------------------------------------------------------*/

static struct rr *
make_rr(source,dname,dclass,dtype,ttl,rdl,data)
int source;
char *dname;
uint16 dclass;
uint16 dtype;
int32 ttl;
uint16 rdl;
void *data;
{
	register struct rr *newrr;

	newrr = (struct rr *)callocw(1,sizeof(struct rr));
	newrr->source = source;
	newrr->name = strdup(dname);
	newrr->class = dclass;
	newrr->type = dtype;
	newrr->ttl = ttl;
	if((newrr->rdlength = rdl) == 0)
		return newrr;

	switch(dtype){
	case TYPE_A:
	  {
		register int32 *ap = (int32 *)data;
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
		register struct hinfo *hinfop = (struct hinfo *)data;
		newrr->rdata.hinfo.cpu = strdup(hinfop->cpu);
		newrr->rdata.hinfo.os = strdup(hinfop->os);
		break;
	  }
	case TYPE_MX:
	  {
		register struct mx *mxp = (struct mx *)data;
		newrr->rdata.mx.pref = mxp->pref;
		newrr->rdata.mx.exch = strdup(mxp->exch);
		break;
	  }
	case TYPE_SOA:
	  {
		register struct soa *soap = (struct soa *)data;
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
put_rr(fp,rrp)
FILE *fp;
struct rr *rrp;
{
	char * stuff;

	if(fp == NULLFILE || rrp == NULLRR)
		return;

	if(rrp->name == NULLCHAR && rrp->comment != NULLCHAR){
		fprintf(fp,"%s",rrp->comment);
		return;
	}

	fprintf(fp,"%s",rrp->name);
	if(rrp->ttl != TTL_MISSING)
		fprintf(fp,"\t%ld",rrp->ttl);
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
		fprintf(fp,"\t%u.%u.%u.%u\n",
			uchar(rrp->rdata.addr >> 24),
			uchar(rrp->rdata.addr >> 16),
			uchar(rrp->rdata.addr >>  8),
			uchar(rrp->rdata.addr      ));
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
		 rrp->rdata.soa.serial,rrp->rdata.soa.refresh,
		 rrp->rdata.soa.retry,rrp->rdata.soa.expire,
		 rrp->rdata.soa.minimum);
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
dumpdomain(dhp)
struct dhdr *dhp;
{
	struct rr *rrp;
	char * stuff;

	printf("id %u qr %u opcode %u aa %u tc %u rd %u ra %u rcode %u\n",
	 dhp->id,
	 dhp->qr,dhp->opcode,dhp->aa,dhp->tc,dhp->rd,
	 dhp->ra,dhp->rcode);
	printf("%u questions:\n",dhp->qdcount);
	for(rrp = dhp->questions; rrp != NULLRR; rrp = rrp->next){
		stuff = dtype(rrp->type);
		printf("%s type %s class %u\n",rrp->name,
		 stuff,rrp->class);
	}
	printf("%u answers:\n",dhp->ancount);
	for(rrp = dhp->answers; rrp != NULLRR; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u authority:\n",dhp->nscount);
	for(rrp = dhp->authority; rrp != NULLRR; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	printf("%u additional:\n",dhp->arcount);
	for(rrp = dhp->additional; rrp != NULLRR; rrp = rrp->next){
		put_rr(stdout,rrp);
	}
	fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static int32 in_addr_arpa(name)
char *name;
{
  int32 addr;

  for (addr = 0; isdigit(*name & 0xff); name++) {
    addr = ((addr >> 8) & 0xffffff) | (atol(name) << 24);
    if (!(name = strchr(name, '.'))) return 0;
  }
  return stricmp(name, "in-addr.arpa.") ? 0 : addr;
}

/*---------------------------------------------------------------------------*/

static struct mbuf *domain_server(bp)
struct mbuf *bp;
{

  char *cp;
  char buffer[256];
  int32 addr;
  struct dhdr *dhp;
  struct rr *qp;
  struct rr *rrp;

  dhp = malloc(sizeof(*dhp));
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
	  !isaddr(cp = resolve_a(addr, 0))) {
	strcpy(buffer, cp);
	if (buffer[strlen(buffer)-1] != '.') strcat(buffer, ".");
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
	  !isaddr(cp = resolve_a(qp->rdata.addr, 0))) {
	strcpy(buffer, cp);
	if (buffer[strlen(buffer)-1] != '.') strcat(buffer, ".");
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
	sprintf(buffer, "%u.%u.%u.%u.in-addr.arpa.",
		uchar(addr      ),
		uchar(addr >>  8),
		uchar(addr >> 16),
		uchar(addr >> 24));
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

#if 0
  {

    struct dhdr *dhp1;
    struct mbuf *bp1;

    dup_p(&bp1, bp, 0, 9999);
    dhp1 = malloc(sizeof(*dhp1));
    if (ntohdomain(dhp1, &bp1)) {
      printf("ntohdomain failed!\n");
    } else {
      printf("check: ");
      dumpdomain(dhp1);
    }
    free_rr(dhp1->questions);
    free_rr(dhp1->answers);
    free_rr(dhp1->authority);
    free_rr(dhp1->additional);
    free(dhp1);

  }
#endif

Done:
  free_rr(dhp->questions);
  free_rr(dhp->answers);
  free_rr(dhp->authority);
  free_rr(dhp->additional);
  free(dhp);
  return bp;
}

/*---------------------------------------------------------------------------*/

static void domain_server_udp(iface, up, cnt)
struct iface *iface;
struct udp_cb *up;
int cnt;
{

  struct mbuf *bp;
  struct socket fsocket;

  recv_udp(up, &fsocket, &bp);
  bp = domain_server(bp);
  if (bp) send_udp(&up->socket, &fsocket, 0, 0, bp, 0, 0, 0);
}

/*---------------------------------------------------------------------------*/

static void domain_server_tcp_recv(tcb, cnt)
struct tcb *tcb;
int cnt;
{

  int len;
  struct mbuf **rcvqptr;
  struct mbuf *bp;

  if (recv_tcp(tcb, &bp, cnt) <= 0) return;
  rcvqptr = (struct mbuf **) &tcb->user;
  append(rcvqptr, bp);
  if (len_p(*rcvqptr) < 2) return;
  len = pull16(rcvqptr);
  if (len_p(*rcvqptr) < len) {
    *rcvqptr = pushdown(*rcvqptr, 2);
    put16((*rcvqptr)->data, len);
    return;
  }
  dup_p(&bp, *rcvqptr, 0, len);
  pullup(rcvqptr, NULLCHAR, len);
  bp = domain_server(bp);
  if (!bp) return;
  len = len_p(bp);
  bp = pushdown(bp, 2);
  put16(bp->data, len);
  send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void domain_server_tcp_state(tcb, old, new)
struct tcb *tcb;
int old, new;
{
  switch (new) {
  case TCP_ESTABLISHED:
    tcb->user = 0;
    log(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    break;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    break;
  case TCP_CLOSED:
    free_p((struct mbuf *) tcb->user);
    log(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
    del_tcp(tcb);
    if (tcb == Domain_tcb) Domain_tcb = 0;
    break;
  }
}

/*---------------------------------------------------------------------------*/

int domain0(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  if (Domain_ucb) {
    del_udp(Domain_ucb);
    Domain_ucb = 0;
  }
  if (Domain_tcb) {
    close_tcp(Domain_tcb);
    Domain_tcb = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int domain1(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  struct socket lsocket;

  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_DOMAIN : udp_port_number(argv[1]);
  if (Domain_ucb) del_udp(Domain_ucb);
  Domain_ucb = open_udp(&lsocket, domain_server_udp);

  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_DOMAIN : tcp_port_number(argv[1]);
  if (Domain_tcb) close_tcp(Domain_tcb);
  Domain_tcb = open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, domain_server_tcp_recv, NULLVFP, domain_server_tcp_state, 0, 0);

  return 0;
}

