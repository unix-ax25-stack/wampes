/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipip.c,v 1.16 1995/12/20 09:46:46 deyke Exp $ */

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

#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "internet.h"
#include "netuser.h"
#include "socket.h"
#include "trace.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "hpux.h"

struct route *rt_add(int32 target, unsigned int bits, int32 gateway, struct iface *iface, int32 metric, int32 ttl, uint8 private);

#define MAX_FRAME       2048

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
};

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
  addr.sin_port = htons(edv->port);

  sendto(edv->fd, buf, l, 0, (struct sockaddr *) &addr, sizeof(addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void ipip_receive(void *argp)
{

  int addrlen;
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
  l = recvfrom(edv->fd, (char *) (bufptr = buf), sizeof(buf), 0, (struct sockaddr *) & addr, &addrlen);
  if (edv->type == USE_IP) {
    if (l <= sizeof(struct ip )) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 0) goto Fail;

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
  int fd;
  int port = IP4_PTCL;
  int type = USE_IP;
  struct edv_t *edv;
  struct iface *ifp;
  struct sockaddr_in addr;

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

  if (argc >= 4) port = atoi(argv[3]);

  if (type == USE_IP)
    fd = socket(AF_INET, SOCK_RAW, port);
  else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    return -1;
  }

  if (type == USE_UDP) {
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr))) {
      printf("cannot bind address: %s\n", strerror(errno));
      close(fd);
      return -1;
    }
  }

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffL;
  ifp->netmask = 0xffffffffL;
  ifp->mtu = MAX_FRAME;
  ifp->flags = NO_RT_ADD;
  setencap(ifp, "None");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->type = type;
  edv->port = port;
  edv->fd = fd;
  ifp->edv = edv;

  ifp->send = ipip_send;
  on_read(fd, ipip_receive, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}
