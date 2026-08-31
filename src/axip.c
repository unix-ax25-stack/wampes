/* @(#) $Id: axip.c,v 1.31 2006/03/12 10:05:01 dl9sau Exp $ */

#include <sys/types.h>

#include "global.h"
#undef  hiword
#undef  loword
#undef  hibyte
#undef  lobyte

#include <errno.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "strerror.h"

#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "ax25.h"
#include "socket.h"
#include "trace.h"
#include "cmdparse.h"
#include "hpux.h"
#include "crc.h"
#include "../lib/buildsaddr.h"

#define MAX_FRAME       2048

#ifdef	notdef
struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
};
#else
#include "sockaddr_util.h"
#include "uhnp.h"
#endif

struct axip_route {
  uint8 call[AXALEN];
  struct sockaddr_storage dest;         /* outer peer, port filled in at send */
  /* DER GELERNTE QUELLPORT, JE RUFZEICHEN - nach dem Vorbild der
   * bpqether-Route (mac/mac_ifp/mactime): der Wert, das Interface, auf dem er
   * gehoert wurde, und wann.
   *
   * Bis hierher lernte allein uhnp, und zwar JE HOST.  Das geht auf, solange
   * hinter einer Adresse ein Knoten steht - und verwechselt zwei, sobald dort
   * zwei unabhaengige stehen, denn dann teilen sich beide Rufzeichen einen
   * Port und die Antwort an das eine landet beim anderen.  Die Tabelle je
   * Rufzeichen gab es dabei die ganze Zeit; sie warf den Port nur weg
   * (axip_route_add() mit keepport 0).  Sie behaelt ihn jetzt.
   *
   * uhnp bleibt: fuer ipip, das gar keine Rufzeichen kennt, und hier als
   * Rueckfall fuer ein Rufzeichen, das wir noch nie gehoert haben - etwa das
   * zweite eines Partners, der mehrere auf einem ax25ipd fuehrt.
   */
  int lport;
  struct edv_t *ledv;
  time_t ltime;
  struct axip_route *next;
};

static struct axip_route *Axip_routes;

static int axip_raw(struct iface *ifp, struct mbuf **bpp);
static void axip_recv(void *argp);
static void axip_route_add(uint8 *call, const struct sockaddr *dest, int keepport);
static void axip_learn_port(uint8 *call, const struct sockaddr *addr, struct edv_t *edv);
static int axip_learned_port(struct axip_route *rp, struct edv_t *edv);
static int doaxiproute(int argc, char *argv[], void *p);
static int doaxiprouteadd(int argc, char *argv[], void *p);
static int doaxiproutedrop(int argc, char *argv[], void *p);

/*---------------------------------------------------------------------------*/

static int axip_raw(struct iface *ifp, struct mbuf **bpp)
{

  int l;
  int multicast;
  int ndigi;
  struct axip_route *rp;
  struct edv_t *edv;
  uint8 buf[MAX_FRAME];
  uint8 (*mpp)[AXALEN];
  uint8 *dest;
  uint8 *p;

  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  append_crc_ccitt(*bpp);

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, *bpp);

  l = pullup(bpp, buf, sizeof(buf));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }

  edv = (struct edv_t *) ifp->edv;

  /* Walk the AX.25 address field to find the immediate destination.  Bound
   * the walk against the end of the frame and against MAXDIGIS: a frame whose
   * addresses never carry the E bit would otherwise run off the end of buf.
   */
  if (l < 2 * AXALEN)
    return -1;
  dest = buf;
  p = dest + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > buf + l)
      return -1;
    p += AXALEN;
    if (!(p[6] & REPEATED)) {
      dest = p;
      break;
    }
  }

  multicast = 0;
  for (mpp = Ax25multi; (*mpp)[0]; mpp++) {
    if (addreq(dest, *mpp)) {
      multicast = 1;
      break;
    }
  }

  for (rp = Axip_routes; rp; rp = rp->next)
    if (multicast || addreq(rp->call, dest)) {
      struct sockaddr_storage to;
      int port = sockaddr_port((struct sockaddr *) &rp->dest);

      if (!port) port = edv->dport;

      /* One socket speaks one family.  A route for the other one belongs to
       * a second interface - attach axip6 - so skip it here rather than
       * handing sendto() an address it cannot use.
       */
      if (rp->dest.ss_family != (unsigned) edv->family) continue;

      to = rp->dest;
      if (edv->type == USE_UDP) {
        /* Das Rufzeichen zuerst, der Host als Rueckfall: nur so bekommen zwei
         * Stationen hinter einer Adresse ihre eigene Antwort.  Fuer den einen
         * Knoten hinter einer Adresse sagen beide dasselbe.
         */
        int lp = axip_learned_port(rp, edv);
        if (lp) {
          port = lp;
        } else {
          struct sockaddr *sa = search_udp_host_nat_port((struct sockaddr *) &to, edv);
          if (sa) port = sockaddr_port(sa);
        }
        uhnp_cleanup(edv);
      }
      sockaddr_set_port((struct sockaddr *) &to, port);
      sendto(edv->fd, (char *) buf, l, 0, (struct sockaddr *) &to,
             sockaddr_len((struct sockaddr *) &to));
    }

  return l;
}

/*---------------------------------------------------------------------------*/

static void axip_recv(void *argp)
{

  socklen_t addrlen;
  int hdr_len;
  int l;
  int ndigi;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;
  struct mbuf *bp;
  struct sockaddr_storage addr;
  uint8 buf[MAX_FRAME];
  uint8 *bufptr;
  uint8 *p;
  uint8 *src;
  int trust_port;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  addrlen = sizeof(addr);
  l = recvfrom(edv->fd, (char *) (bufptr = buf), sizeof(buf), 0, (struct sockaddr *) &addr, &addrlen);
  if (edv->type == USE_IP) {
    /* cast: l is int, and recvfrom() returns -1 on error.  Comparing against
     * an unsigned sizeof would convert that -1 to SIZE_MAX and pass. */
    if (l <= (int) sizeof(struct ip)) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 2) goto Fail;

  if (!check_crc_ccitt((char *) bufptr, l)) goto Fail;
  l -= 2;

  /* secure-port model of trust: src address adaption, but only
     - if my listen port >= 1024,
     - or if my listen port < 1024 and src port is also < 1024
   * Der Merker haelt das Ergebnis fest, weil dasselbe Vertrauen auch fuer den
   * Port je Rufzeichen gilt - das Rufzeichen steht aber erst nach dem
   * Adressfeld fest, die Bedingung gehoert hierher.
   */
  trust_port = (edv->type == USE_UDP &&
        (edv->port >= 1024 || sockaddr_port((struct sockaddr *) &addr) < 1024));
  if (trust_port) {
    learn_udp_host_nat_port((struct sockaddr *) &addr, edv);
    uhnp_cleanup(edv);
  }

  /* Walk the AX.25 address field to find the immediate source.  Bound the
   * walk against the end of the datagram and against MAXDIGIS.  Without this
   * a datagram whose addresses never carry the E bit walks off the end of
   * buf, and src ends up pointing at stack memory that axip_route_add() would
   * then copy into the AX.25 routing table.
   */
  if (l < 2 * AXALEN) goto Fail;
  p = src = bufptr + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > bufptr + l) goto Fail;
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }
  axip_route_add(src, (struct sockaddr *) &addr, 0);
  if (trust_port)
    axip_learn_port(src, (struct sockaddr *) &addr, edv);

  bp = qdata(bufptr, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int axip_attach(int argc, char *argv[], void *p)
{

  char *ifname = "axip";
  char *bindhost = 0;
  char *av[8];
  int ac = 0;
  int i;
  int fd;
  int family = AF_INET;
  int port = AX25_PTCL;
  int dport = 0;
  int type = USE_IP;
  struct edv_t *edv;
  struct iface *ifp;
  struct sockaddr_storage addr;
  socklen_t addrlen;

  /* WHICH ADDRESS TO LISTEN ON, and the only word here that is not
   * positional - the rest is "each word needs the one before it", and a
   * setting that is usually left out cannot live at the end of such a chain.
   *
   * Left out it is every address, which is what it always was and what a node
   * on the air wants: peers turn up on whatever interface the routing gives
   * them.  Named, it is the one - "bind=127.0.0.1" or "bind=::1" for a node
   * that only talks to programs on the same machine, so the axudp port is not
   * reachable from outside without a firewall in front of it.
   */
  for (i = 0; i < argc && ac < (int) (sizeof(av) / sizeof(av[0])); i++) {
    if (!strncmp(argv[i], "bind=", 5)) {
      bindhost = argv[i] + 5;
      continue;
    }
    av[ac++] = argv[i];
  }
  argc = ac;
  argv = av;

  if (argc >= 2) ifname = argv[1];

  if (if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }

  /* "ip"/"udp" as before; a trailing 6 - "ip6", "udp6" - selects IPv6 for the
   * outer transport.  The encapsulated frame is untouched by that, so an IPv4
   * peer running ax25ipd or XNET sees no difference; an IPv6 peer needs
   * something that speaks it, which today means another WAMPES.
   */
  if (argc >= 3) {
    char *t = argv[2];
    int is6 = *t && t[strlen(t) - 1] == '6';

    switch (*t) {
    case 'I':
    case 'i':
      type = USE_IP;
      break;
    case 'U':
    case 'u':
      type = USE_UDP;
      break;
    default:
      printf("Type must be IP, UDP, IP6 or UDP6\n");
      return -1;
    }
    if (is6) {
#if HAS_AF_INET6
      family = AF_INET6;
#else
      printf("This build has no IPv6 support\n");
      return -1;
#endif
    }
  }

  /* One number means both, as it always did.  "<src>:<dst>" separates them:
   * the first is what we bind to, the second where we send when neither the
   * route nor a learned source port says otherwise.  Needed where the two
   * genuinely differ - behind a NAT that rewrites one of them, or when a
   * peer insists on talking to 93 while we may not bind a privileged port.
   *
   * Only for UDP.  With a raw socket the number is the IP protocol, there is
   * no port at either end, and a colon there would be nonsense rather than a
   * setting nobody uses.
   */
  if (argc >= 4) {
    char *colon = strchr(argv[3], ':');

    if (colon) {
      if (type != USE_UDP) {
        printf("\"%s\": with ip or ip6 the number is the IP protocol, and a "
               "raw\nsocket has no ports to keep apart\n", argv[3]);
        return -1;
      }
      *colon = '\0';
      dport = atoi(colon + 1);
      if (dport <= 0 || dport > 65535) {
        printf("\"%s\" is not a port\n", colon + 1);
        return -1;
      }
    }
    port = atoi(argv[3]);
    if (type == USE_UDP && (port <= 0 || port > 65535)) {
      printf("\"%s\" is not a port\n", argv[3]);
      return -1;
    }
  }
  if (!dport) dport = port;             /* one number means both */

  if (type == USE_IP)
    fd = socket(family, SOCK_RAW, port);
  else
    fd = socket(family, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    return -1;
  }

#if HAS_AF_INET6
  if (family == AF_INET6) {
    int arg = 1;

    /* Pin this down rather than inheriting it: whether an IPv6 socket also
     * accepts IPv4 differs between Linux and the BSDs, and an axip and an
     * axip6 interface have to be able to hold the same port side by side. */
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &arg, sizeof(arg));
  }
#endif

  if (type == USE_UDP || bindhost != NULL) {
    memset(&addr, 0, sizeof(addr));
    if (bindhost != NULL) {
      struct sockaddr *sa;
      int len;

      /* A raw socket has no port; binding it to an address still says which
       * one we answer on, so the host is asked for with port 0 there.
       */
      if (!(sa = build_sockaddr_host(bindhost,
				     type == USE_UDP ? port : 0, &len))) {
	printf("cannot look up \"%s\"\n", bindhost);
	close(fd);
	return -1;
      }
      if (sa->sa_family != family) {
	printf("\"%s\" is not an address of the family this interface "
	       "speaks -\nuse \"udp6\"/\"ip6\" for an IPv6 address\n",
	       bindhost);
	close(fd);
	return -1;
      }
      memcpy(&addr, sa, (size_t) len);
      addrlen = (socklen_t) len;
    } else {
#if HAS_AF_INET6
    if (family == AF_INET6) {
      struct sockaddr_in6 *s6 = (struct sockaddr_in6 *) &addr;

      s6->sin6_family = AF_INET6;
      s6->sin6_addr = in6addr_any;
      s6->sin6_port = htons(port);
    } else
#endif
    {
      struct sockaddr_in *si = (struct sockaddr_in *) &addr;

      si->sin_family = AF_INET;
      si->sin_addr.s_addr = INADDR_ANY;
      si->sin_port = htons(port);
    }
    addrlen = sockaddr_len((struct sockaddr *) &addr);
    }
    if (bind(fd, (struct sockaddr *) &addr, addrlen)) {
      printf("cannot bind address: %s\n", strerror(errno));
      close(fd);
      return -1;
    }
  }

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  ifp->mtu = 256;
  ifp->crccontrol = CRC_CCITT;
  setencap(ifp, "AX25UI");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->type = type;
  edv->port = port;
  edv->dport = dport;
  edv->fd = fd;
  edv->family = family;
  edv->uhnp = 0;
  edv->uhnp_time = secclock();
  ifp->edv = edv;

  ifp->raw = axip_raw;
  on_read(fd, axip_recv, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

/*---------------------------------------------------------------------------*/

/* keepport: the sysop wrote a port into the route and means it.  Learned
 * routes pass zero and keep the old rule - the port comes from the interface,
 * or from what the peer was last seen using, which is a table that expires.
 * Pinning a peer's momentary source port into a route that never expires is
 * a different thing entirely.
 */

static void axip_route_add(uint8 *call, const struct sockaddr *dest, int keepport)
{
  struct axip_route *rp;
  socklen_t len = sockaddr_len(dest);

  if (!len) return;

  for (rp = Axip_routes; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp) {
    if (!(rp = (struct axip_route *) malloc(sizeof(struct axip_route))))
      return;
    addrcp(rp->call, call);
    rp->lport = 0;
    rp->ledv = 0;
    rp->ltime = 0;
    rp->next = Axip_routes;
    Axip_routes = rp;
  } else if (!sockaddr_addr_eq((struct sockaddr *) &rp->dest, dest)) {
    /* Der gelernte Port stirbt mit der Adresse, zu der er gehoerte - sonst
     * traegt eine umgezogene Station den Port ihres Vorgaengers weiter.  Der
     * Empfangsweg lernt ihn unmittelbar danach neu.
     */
    rp->lport = 0;
    rp->ledv = 0;
    rp->ltime = 0;
  }
  memset(&rp->dest, 0, sizeof(rp->dest));
  memcpy(&rp->dest, dest, (size_t) len);
  if (!keepport)
    sockaddr_set_port((struct sockaddr *) &rp->dest, 0);
  /* A port of zero - which is what a route written without one carries -
   * means the same as before: take the interface's, or whatever the peer was
   * last seen using.  A port given here is for a partner that listens
   * somewhere else, which ax25ipd can express and this could not.
   */
}

/*---------------------------------------------------------------------------*/

/* Den Quellport auf dem Rufzeichen merken.  Gerufen nur direkt hinter
 * axip_route_add(), das die Adresse eben erst gesetzt hat - deshalb steht
 * hier keine zweite Adresspruefung.
 */
static void axip_learn_port(uint8 *call, const struct sockaddr *addr,
	struct edv_t *edv)
{
  struct axip_route *rp;

  for (rp = Axip_routes; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp) return;
  rp->lport = sockaddr_port(addr);
  rp->ledv = edv;
  rp->ltime = secclock();
}

/*---------------------------------------------------------------------------*/

/* Der gelernte Port, oder 0.  Nur auf dem Interface, auf dem er gehoert
 * wurde: uhnp haengt am edv und war damit schon immer je Interface, und mit
 * zwei axudp-Ports auf einer Kiste ist die Unterscheidung auch noetig.
 * Dieselbe Frist wie uhnp - was dort altert, altert auch hier.
 */
static int axip_learned_port(struct axip_route *rp, struct edv_t *edv)
{
  if (!rp->lport || rp->ledv != edv) return 0;
  if (rp->ltime + UHNP_LEASETIME < secclock()) {
    rp->lport = 0;
    return 0;
  }
  return rp->lport;
}

/*---------------------------------------------------------------------------*/

static struct cmds Axipcmds[] = {
  { "route",  doaxiproute, 0, 0,
    "axip route                             list the routes\n"
    "       axip route add <call> <host> [<port>]\n"
    "       axip route drop <call>\n"
    "  <port> is for a partner who listens somewhere other than the port of\n"
    "  the interface; left out it means the interface's, or whatever he was\n"
    "  last seen using." },
  { NULL,     NULL,        0, 0, NULL }
};

int doaxip(int argc, char *argv[], void *p)
{
  return subcmd(Axipcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static struct cmds Axiproutecmds[] = {
  { "add",    doaxiprouteadd,  0, 3,
    "axip route add <call> <host> [<port>]" },
  { "drop",   doaxiproutedrop, 0, 2, "axip route drop <call>" },
  { NULL,     NULL,            0, 0, NULL }
};

static int doaxiproute(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  struct axip_route *rp;

  if (argc >= 2)
    return subcmd(Axiproutecmds, argc, argv, p);

  printf("Call       Addr\n");
  for (rp = Axip_routes; rp; rp = rp->next) {
    char abuf[SOCKADDR_STRLEN];

    printf("%-9s  %s", pax25(buf, rp->call),
           sockaddr_to_string((struct sockaddr *) &rp->dest, abuf, sizeof(abuf)));
    if (sockaddr_port((struct sockaddr *) &rp->dest))
      printf("  port %d", sockaddr_port((struct sockaddr *) &rp->dest));
    if (rp->lport)
      printf("  learned %d (%ld s ago)", rp->lport,
             (long) (secclock() - rp->ltime));
    putchar('\n');
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiprouteadd(int argc, char *argv[], void *p)
{

  uint8 call[AXALEN];
  int32 dest;
  int port = 0;                 /* zero: as before, the interface decides */

  if (argc >= 4) {
    port = atoi(argv[3]);
    if (port <= 0 || port > 65535) {
      printf("Invalid port \"%s\"\n", argv[3]);
      return 1;
    }
  }
  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  /* WAMPES' own host table first, so every existing configuration keeps
   * resolving out of TCPDIR/hosts exactly as before.  Only if that has
   * nothing do we ask the host resolver, which is also what handles an IPv6
   * literal or a bracketed [name].  When resolve_sa() exists the first step
   * will be able to return an IPv6 address too.
   */
  if ((dest = resolve(argv[2]))) {
    struct sockaddr_in sin;

    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(dest);
    sin.sin_port = htons(port);
    axip_route_add(call, (struct sockaddr *) &sin, 1);
  } else {
    struct sockaddr *sa;
    int len;

    if (!(sa = build_sockaddr_host(argv[2], port, &len))) {
      printf(Badhost, argv[2]);
      return 1;
    }
    axip_route_add(call, sa, 1);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiproutedrop(int argc, char *argv[], void *p)
{

  uint8 call[AXALEN];
  struct axip_route *rp, *pp;

  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  for (pp = 0, rp = Axip_routes; rp; pp = rp, rp = rp->next)
    if (addreq(rp->call, call)) {
      if (pp)
	pp->next = rp->next;
      else
	Axip_routes = rp->next;
      free(rp);
      break;
    }
  return 0;
}
