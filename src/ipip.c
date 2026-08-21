/* @(#) $Id: ipip.c,v 1.24 2006/03/12 10:05:01 dl9sau Exp $ */

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
#include "buildsaddr.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "socket.h"
#include "trace.h"
#include "cmdparse.h"
#include "hpux.h"

struct route *rt_add(int32 target, unsigned int bits, int32 gateway, struct iface *iface, int32 metric, int32 ttl, uint8 private);

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

/*---------------------------------------------------------------------------*/

static int ipip_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  char buf[MAX_FRAME];
  int l;
  struct edv_t *edv;
  struct sockaddr_in addr;

  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, *bpp);

  l = pullup(bpp, buf, sizeof(buf));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }

  edv = (struct edv_t *) ifp->edv;

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(gateway);

  if (edv->type == USE_UDP) {
    /* ipip stays IPv4: the outer peer is struct route.gateway, an int32 of
     * the general routing table.  Only the lookup key has to be built as a
     * sockaddr now that the table is family agnostic. */
    struct sockaddr *sa = search_udp_host_nat_port((struct sockaddr *) &addr, edv);
    addr.sin_port = sa ? ((struct sockaddr_in *) sa)->sin_port
			: htons(edv->dport);
    uhnp_cleanup(edv);
  } else
    addr.sin_port = htons(edv->port);

  sendto(edv->fd, buf, l, 0, (struct sockaddr *) &addr, sizeof(addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void ipip_receive(void *argp)
{

  socklen_t addrlen;
  int hdr_len;
  int l;
  int32 ipaddr;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;
  struct mbuf *bp;
  struct sockaddr_in addr;
  uint8 buf[MAX_FRAME];
  uint8 *bufptr;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  addrlen = sizeof(addr);
  l = recvfrom(edv->fd, (char *) (bufptr = buf), sizeof(buf), 0, (struct sockaddr *) &addr, &addrlen);
  if (edv->type == USE_IP) {
    /* cast: l is int and recvfrom() returns -1 on error, which against an
     * unsigned sizeof would convert to SIZE_MAX and pass the test. */
    if (l <= (int) sizeof(struct ip)) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  /* What is left has to be an IP datagram - it is handed to net_route()
   * below, and its source address is read a few lines down.  The old test
   * was "l <= 0", so a one-byte datagram was enough to make get32() read
   * four bytes that had never been received: uninitialised stack, which then
   * went into the routing table via rt_add().
   */
  if (l < (int) sizeof(struct ip)) goto Fail;

  if (edv->type == USE_UDP &&
        (htons(edv->port) >= 1024 || htons(addr.sin_port) < 1024)) {
        /* secure-port model of trust: src address adaption, but only
           - if my listen port >= 1024,
           - or if my listen port < 1024 and src port is also < 1024 */
    learn_udp_host_nat_port((struct sockaddr *) &addr, edv);
    uhnp_cleanup(edv);
  }

  if ((ipaddr = get32(bufptr + 12)) && ismyaddr(ipaddr) == NULL)
    rt_add(ipaddr, 32, (int32) ntohl(addr.sin_addr.s_addr), ifp, 1L, 0x7fffffff / 1000, 0);

  bp = qdata(bufptr, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int ipip_attach(int argc, char *argv[], void *p)
{

  char *ifname = "ipip";
  char *bindhost = 0;
  char *av[8];
  int ac = 0;
  int i;
  int fd;
  int port = IP4_PTCL;
  int dport = 0;
  int type = USE_IP;
  struct edv_t *edv;
  struct iface *ifp;
  struct sockaddr_in addr;

  /* "bind=<addr>" - which local address to answer on, the same word "attach
   * axip" takes and for the same reason.  Left out it is every address; named
   * it is that one.  Not positional, so it may stand anywhere in the line.
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

  if (argc >= 3)
    switch (*argv[2]) {
    case 'I':
    case 'i':
      type = USE_IP;
      break;
    case 'U':
    case 'u':
      type = USE_UDP;
      break;
    default:
      printf("Type must be IP or UDP\n");
      return -1;
    }

  /* Same as axip: one number means both ends, "<src>:<dst>" separates them,
   * and only UDP has two ends to separate - with a raw socket the number is
   * the IP protocol.
   */
  if (argc >= 4) {
    char *colon = strchr(argv[3], ':');

    if (colon) {
      if (type != USE_UDP) {
        printf("\"%s\": with ip the number is the IP protocol, and a raw "
               "socket\nhas no ports to keep apart\n", argv[3]);
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
    fd = socket(AF_INET, SOCK_RAW, port);
  else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    return -1;
  }

  if (type == USE_UDP || bindhost != NULL) {
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(type == USE_UDP ? port : 0);
    if (bindhost != NULL) {
      /* IPv4 only here - ipip has no v6 transport yet, so an address that is
       * not v4 is a mistake worth naming rather than a family to switch to.
       */
      struct sockaddr *sa;
      int len;

      if (!(sa = build_sockaddr_host(bindhost,
				     type == USE_UDP ? port : 0, &len))) {
	printf("cannot look up \"%s\"\n", bindhost);
	close(fd);
	return -1;
      }
      if (sa->sa_family != AF_INET) {
	printf("\"%s\" is not an IPv4 address - ipip has no IPv6 transport\n",
	       bindhost);
	close(fd);
	return -1;
      }
      memcpy(&addr, sa, sizeof(addr));
    }
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr))) {
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
  ifp->mtu = MAX_FRAME;
  ifp->flags = NO_RT_ADD;
  setencap(ifp, "None");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->type = type;
  edv->port = port;
  /* Where we send when neither the route nor a learned source port says.  It
   * matters beyond the sending: learn_udp_host_nat_port() asks dport for the
   * port that makes a learned entry superfluous, so left at zero from
   * malloc() no learned source port would ever be dropped again.
   */
  edv->dport = dport;
  edv->fd = fd;
  edv->family = AF_INET;
  edv->uhnp = 0;
  edv->uhnp_time = secclock();
  ifp->edv = edv;

  ifp->send = ipip_send;
  on_read(fd, ipip_receive, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}
