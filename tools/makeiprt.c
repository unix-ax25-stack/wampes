#ifndef __lint
static const char rcsid[] = "@(#) $Id: makeiprt.c,v 1.17 1996/08/12 18:52:58 deyke Exp $";
#endif

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

#define MERGE_HOST_ROUTES       0

#define DBHOSTADDR      "/tcp/hostaddr"
#define DBHOSTNAME      "/tcp/hostname"
#define LOCALDOMAIN     "ampr.org"

struct cache {
  struct cache *next;
  long addr;
  char name[1];
};

struct iface {
  struct iface *next;
  char name[1];
};

struct route {
  long dest;
  int bits;
  const struct iface *iface;
  long gateway;
  int metric;
  int priv;
  struct route *next;
};

struct link {
  struct node *node;
  const struct iface *iface;
  long gateway;
  int metric;
  int priv;
  struct link *next;
};

struct node {
  const char *name;
  long addr;
  struct link *links;
  struct route *routes;
  int changed;
  struct node *next;
};

static DBM *Dbhostaddr;
static DBM *Dbhostname;
static const struct iface *Loopback_iface;
static int Usegethostby;
static struct cache *Cache;
static struct iface *Ifaces;
static struct node *Nodes;

/*---------------------------------------------------------------------------*/

static void strlwc(char *to, const char *from)
{
  while ((*to++ = tolower(*from++ & 0xff))) ;
}

/*---------------------------------------------------------------------------*/

static long aton(const char *name)
{

  const char * cp;
  int i;
  long addr;

  if (!name || !isdigit(*name & 0xff)) return (-1);
  for (cp = name; *cp; cp++)
    if (!isdigit(*cp & 0xff) && *cp != '.') return (-1);
  addr = 0;
  for (i = 24; i >= 0; i -= 8) {
    addr |= atol(name) << i;
    if (!(name = strchr(name, '.'))) break;
    name++;
    if (!*name) break;
  }
  return addr;
}

/*---------------------------------------------------------------------------*/

static void add_to_cache(const char *name, long addr)
{
  struct cache *cp;

  cp = (struct cache *) malloc(sizeof(struct cache) + strlen(name));
  strcpy(cp->name, name);
  cp->addr = addr;
  cp->next = Cache;
  Cache = cp;
}

/*---------------------------------------------------------------------------*/

static long resolve(const char *name)
{

  char *p;
  char names[3][1024];
  datum daddr;
  datum dname;
  int i;
  long addr;
  struct cache *curr;
  struct cache *prev;
  struct hostent *hp;

  if ((addr = aton(name)) != -1L) return addr;

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

  if (Usegethostby && (hp = gethostbyname((char *) name))) {
    addr = ntohl(((struct in_addr *)(hp->h_addr))->s_addr);
    strlwc(names[0], hp->h_name);
    add_to_cache(names[0], addr);
    return addr;
  }

  fprintf(stderr, "Cannot resolve \"%s\"\n", name);
  exit(1);
  return 0;
}

/*---------------------------------------------------------------------------*/

static const char *resolve_a(long addr)
{

  char buf[1024];
  datum daddr;
  datum dname;
  struct cache *curr;
  struct cache *prev;
  struct hostent *hp;
  struct in_addr in_addr;

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
    daddr.dptr = (char *) &addr;
    daddr.dsize = sizeof(addr);
    dname = dbm_fetch(Dbhostname, daddr);
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

  sprintf(buf, "%ld.%ld.%ld.%ld",
	  (addr >> 24) & 0xff,
	  (addr >> 16) & 0xff,
	  (addr >>  8) & 0xff,
	  (addr      ) & 0xff);
  add_to_cache(buf, addr);
  return Cache->name;
}

/*---------------------------------------------------------------------------*/

static const struct iface *get_iface(const char *name)
{
  struct iface *ip;

  for (ip = Ifaces; ip && strcmp(ip->name, name); ip = ip->next) ;
  if (!ip) {
    ip = (struct iface *) malloc(sizeof(struct iface) + strlen(name));
    strcpy(ip->name, name);
    ip->next = Ifaces;
    Ifaces = ip;
  }
  return ip;
}

/*---------------------------------------------------------------------------*/

static int is_in(long dest1, int bits1, long dest2, int bits2)
{
  long mask2;

  if (bits1 < bits2) return 0;
  if (!bits2) return 1;
  mask2 = (~0L) << (32 - bits2);
  return ((dest1 & mask2) == dest2);
}

/*---------------------------------------------------------------------------*/

static void add_route(struct node *np, long dest, int bits, const struct iface *iface, long gateway, int metric, int priv)
{
  struct route *rp;

  for (rp = np->routes; rp && (dest != rp->dest || bits != rp->bits); rp = rp->next) ;
  if (!rp) {
    rp = (struct route *) calloc(1, sizeof(struct route));
    rp->next = np->routes;
    np->routes = rp;
    rp->metric = metric + 1;
  }
  if (rp->metric == metric && (rp->iface != iface || rp->gateway != gateway)) {
    fprintf(stderr, "Warning: same metric for:\n");
    fprintf(stderr, "%-20s %-20s %-8s %-20s\n", np->name, resolve_a(rp->dest), rp->iface->name, resolve_a(rp->gateway));
    fprintf(stderr, "%-20s %-20s %-8s %-20s\n", np->name, resolve_a(dest), iface->name, resolve_a(gateway));
  }
  if (rp->metric > metric) {
    rp->dest = dest;
    rp->bits = bits;
    rp->iface = iface;
    rp->gateway = gateway;
    rp->metric = metric;
    rp->priv = priv;
    np->changed = 1;
  }
}

/*---------------------------------------------------------------------------*/

static struct node *get_node(long addr)
{
  struct node *np;

  for (np = Nodes; np && np->addr != addr; np = np->next) ;
  if (!np) {
    np = (struct node *) calloc(1, sizeof(struct node));
    np->name = resolve_a(addr);
    np->addr = addr;
    np->next = Nodes;
    Nodes = np;
    add_route(np, addr, 32, Loopback_iface, 0, 1, 1);
  }
  return np;
}

/*---------------------------------------------------------------------------*/

static void read_routes(void)
{

  char deststr[1024];
  char gatewaystr[1024];
  char hoststr[1024];
  char ifacestr[1024];
  char line[1024];
  char privstr[1024];
  FILE *fp;
  int bits;
  int linnum;
  int metric;
  long dest;
  long gateway;
  struct node *np;

  if (!(fp = fopen("ip_routes", "r"))) {
    perror("ip_routes");
    exit(1);
  }
  linnum = 0;
  while (fgets(line, sizeof(line), fp)) {
    linnum++;
    if (*line == '\n' || *line == '#') continue;
    strcpy(gatewaystr, "0");
    *privstr = 0;
    if (sscanf(line, "%s %s %d %s %d %s %s", hoststr, deststr, &bits, ifacestr, &metric, gatewaystr, privstr) < 5 || bits < 0 || bits > 32 || metric <= 0) {
      fprintf(stderr, "ip_routes: syntax error in line %d\n", linnum);
      exit(1);
    }
    np = get_node(resolve(hoststr));
    if (bits)
      dest = resolve(deststr) & ((~0L) << (32 - bits));
    else
      dest = 0;
    gateway = resolve(gatewaystr);
    if (bits == 32 && !gateway) gateway = dest;
    add_route(np, dest, bits, get_iface(ifacestr), gateway, metric, *privstr);
  }
}

/*---------------------------------------------------------------------------*/

static void create_links(void)
{

  struct link *lp;
  struct link *nlp;
  struct node *nnp;
  struct node *np;
  struct route *rp;

  for (np = Nodes; np; np = np->next)
    for (rp = np->routes; rp; rp = rp->next)
      if (rp->bits == 32 && rp->dest != np->addr) {
	for (nnp = Nodes; nnp && nnp->addr != rp->dest; nnp = nnp->next) ;
	if (nnp) {
	  lp = (struct link *) malloc(sizeof(struct link));
	  lp->node = nnp;
	  lp->iface = rp->iface;
	  lp->gateway = rp->gateway;
	  lp->metric = rp->metric;
	  lp->priv = rp->priv;
	  lp->next = np->links;
	  np->links = lp;
	}
      }

  for (np = Nodes; np; np = np->next)
    for (lp = np->links; lp; lp = lp->next) {
      for (nlp = lp->node->links; nlp; nlp = nlp->next)
	if (nlp->node == np) goto Found;
      fprintf(stderr, "Missing route from %s to %s\n", lp->node->name, np->name);
      exit(1);
Found:
      if (lp->metric != nlp->metric) {
	fprintf(stderr, "Inconsistent metrics between %s and %s\n", lp->node->name, nlp->node->name);
	exit(1);
      }
    }

}

/*---------------------------------------------------------------------------*/

static void propagate_routes(void)
{

  int changed;
  struct link *lp;
  struct node *np;
  struct route *rp;

  do {
    changed = 0;
    for (np = Nodes; np; np = np->next) {
      np->changed = 0;
      for (lp = np->links; lp; lp = lp->next)
	for (rp = lp->node->routes; rp; rp = rp->next)
	  if (!rp->priv)
	    add_route(np, rp->dest, rp->bits, lp->iface, lp->gateway,
		      lp->metric + rp->metric, lp->priv);
      if (np->changed) changed = 1;
    }
  } while (changed);
}

/*---------------------------------------------------------------------------*/

static void sort_routes(void)
{

  int bits;
  struct node *np;
  struct route *rp;
  struct route *rt[33];

  for (np = Nodes; np; np = np->next) {
    for (bits = 0; bits <= 32; bits++)
      rt[bits] = 0;
    while ((rp = np->routes)) {
      np->routes = rp->next;
      rp->next = rt[rp->bits];
      rt[rp->bits] = rp;
    }
    for (bits = 0; bits <= 32; bits++) {
      while ((rp = rt[bits])) {
	rt[bits] = rp->next;
	rp->next = np->routes;
	np->routes = rp;
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

static void merge_routes(void)
{

  struct node *np;
  struct route *curr;
  struct route *prev;
  struct route *rp;

  for (np = Nodes; np; np = np->next) {
Retry:
    for (prev = 0, curr = np->routes; curr; prev = curr, curr = curr->next) {
#if MERGE_HOST_ROUTES
				/* hosts and networks */
#else
      if (curr->bits < 32)      /* networks only */
#endif
	for (rp = curr->next; rp; rp = rp->next) {
	  if (is_in(curr->dest, curr->bits, rp->dest, rp->bits)) {
	    if (curr->iface != rp->iface || curr->gateway != rp->gateway)
	      break;
	    if (prev)
	      prev->next = curr->next;
	    else
	      np->routes = curr->next;
	    free(curr);
	    goto Retry;
	  }
	}
    }
  }
}

/*---------------------------------------------------------------------------*/

static void print_links(void)
{

  struct link *lp;
  struct node *np;

  printf("\nLINKS:\n\n");
  for (np = Nodes; np; np = np->next) {
    for (lp = np->links; lp; lp = lp->next)
      printf("%-20s %-20s %-8s %-20s %5d %c\n", np->name, lp->node->name, lp->iface->name, resolve_a(lp->gateway), lp->metric, lp->priv ? 'P' : ' ');
  }
}

/*---------------------------------------------------------------------------*/

static void print_routes(void)
{

  const char *gateway;
  char dest[1024];
  struct node *np;
  struct route *rp;

  printf("\nROUTES:\n\n");
  for (np = Nodes; np; np = np->next) {
    for (rp = np->routes; rp; rp = rp->next) {
      if (!rp->bits)
	strcpy(dest, "default");
      else if (rp->bits == 32)
	strcpy(dest, resolve_a(rp->dest));
      else
	sprintf(dest, "%s/%d", resolve_a(rp->dest), rp->bits);
      if (rp->iface != Loopback_iface) {
	if (!rp->gateway || (rp->bits == 32 && rp->dest == rp->gateway))
	  gateway = "";
	else
	  gateway = resolve_a(rp->gateway);
	printf("%-20s %-20s %-8s %-20s %5d %c\n", np->name, dest, rp->iface->name, gateway, rp->metric, rp->priv ? 'P' : ' ');
      }
    }
    printf("\n");
  }
}

/*---------------------------------------------------------------------------*/

static void make_route_files(void)
{

  char command[1024];
  char dest[1024];
  char *cp;
  const char * gateway;
  FILE *fp;
  struct node *np;
  struct route *rp;

  for (np = Nodes; np; np = np->next) {
    strcpy(dest, np->name);
    if ((cp = strchr(dest, '.'))) *cp = 0;
    sprintf(command, "sort > /tmp/iprt.%s", dest);
    if (!(fp = popen(command, "w"))) {
      perror(command);
      exit(1);
    }
    for (rp = np->routes; rp; rp = rp->next) {
      if (!rp->bits)
	strcpy(dest, "default");
      else if (rp->bits == 32)
	strcpy(dest, resolve_a(rp->dest));
      else
	sprintf(dest, "%s/%d", resolve_a(rp->dest), rp->bits);
      if (rp->iface != Loopback_iface) {
	if (!rp->gateway || (rp->bits == 32 && rp->dest == rp->gateway))
	  gateway = "";
	else
	  gateway = resolve_a(rp->gateway);
	fprintf(fp, "route add%c %-20s %-8s %-20s\n", rp->priv ? 'p' : ' ', dest, rp->iface->name, gateway);
      }
    }
    pclose(fp);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  Loopback_iface = get_iface("loopback");

  read_routes();
  create_links();
  propagate_routes();
  sort_routes();
  merge_routes();

  if (argc > 1) {
    print_links();
    print_routes();
  } else {
    make_route_files();
  }

  return 0;
}

