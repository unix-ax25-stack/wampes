/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipip.c,v 1.13 1994/08/05 10:35:49 deyke Exp $ */

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
#include <stdlib.h>
#include <string.h>
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

#define MAX_FRAME       2048

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
};

static int ipip_send(struct mbuf *data, struct iface *ifp, int32 gateway, int tos);
static void ipip_receive(void *argp);

/*---------------------------------------------------------------------------*/

static int ipip_send(struct mbuf *data, struct iface *ifp, int32 gateway, int tos)
{

  char buf[MAX_FRAME];
  int l;
  struct edv_t *edv;
  struct sockaddr_in addr;

  dump(ifp, IF_TRACE_OUT, data);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, data);

  l = pullup(&data, buf, sizeof(buf));
  if (l <= 0 || data) {
    free_p(data);
    return (-1);
  }

  edv = ifp->edv;

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(gateway);
  addr.sin_port = htons(edv->port);

  sendto(edv->fd, buf, l, 0, (struct sockaddr *) & addr, sizeof(addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void ipip_receive(void *argp)
{

  char *bufptr;
  char buf[MAX_FRAME];
  int addrlen;
  int hdr_len;
  int l;
  int32 ipaddr;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;
  struct sockaddr_in addr;

  ifp = argp;
  edv = ifp->edv;
  addrlen = sizeof(addr);
  l = recvfrom(edv->fd, bufptr = buf, sizeof(buf), 0, (struct sockaddr *) & addr, &addrlen);
  if (edv->type == USE_IP) {
    if (l <= sizeof(struct ip )) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 0) goto Fail;

  if ((ipaddr = get32(bufptr + 12)) && ismyaddr(ipaddr) == NULLIF)
    rt_add(ipaddr, 32, (int32) ntohl(addr.sin_addr.s_addr), ifp, 1L, 0x7fffffff / 1000, 0);

  net_route(ifp, qdata(bufptr, l));
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

  if (if_lookup(ifname) != NULLIF) {
    printf("Interface %s already exists\n", ifname);
    return (-1);
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
      return (-1);
    }

  if (argc >= 4) port = atoi(argv[3]);

  if (type == USE_IP)
    fd = socket(AF_INET, SOCK_RAW, port);
  else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    return (-1);
  }

  if (type == USE_UDP) {
    memset((char *) &addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *) &addr, sizeof(addr))) {
      printf("cannot bind address: %s\n", strerror(errno));
      close(fd);
      return (-1);
    }
  }

  ifp = callocw(1, sizeof(*ifp));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffL;
  ifp->netmask = 0xffffffffL;
  ifp->mtu = MAX_FRAME;
  ifp->flags = NO_RT_ADD;
  setencap(ifp, "None");

  edv = (struct edv_t *) malloc(sizeof(*edv));
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

