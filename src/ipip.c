/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipip.c,v 1.2 1992/05/14 13:20:10 deyke Exp $ */

#include "global.h"

#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

extern char *sys_errlist[];
extern int errno;

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
#define IP_OVER_IP      0
#define IP_OVER_UDP     1
  int fd;
  struct sockaddr_in addr;
};

static int ipip_send __ARGS((struct mbuf *data, struct iface *ifp, int32 gateway, int prec, int del, int tput, int rel));
static void ipip_recv __ARGS((void *argp));

/*---------------------------------------------------------------------------*/

static int ipip_send(data, ifp, gateway, prec, del, tput, rel)
struct mbuf *data;
struct iface *ifp;
int32 gateway;
int prec;
int del;
int tput;
int rel;
{

  char buf[MAX_FRAME];
  int l;
  struct edv_t *edv;

  dump(ifp, IF_TRACE_OUT, ifp->type, data);
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
  sendto(edv->fd, buf, l, 0, (struct sockaddr *) &edv->addr, sizeof(edv->addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void ipip_recv(argp)
void *argp;
{

  char *bufptr;
  char buf[MAX_FRAME];
  int hdr_len;
  int l;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;

  ifp = argp;
  edv = ifp->edv;
  l = recv(edv->fd, bufptr = buf, sizeof(buf), 0);
  if (edv->type == IP_OVER_IP) {
    if (l <= sizeof(struct ip )) goto fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 0) goto fail;
  net_route(ifp, ifp->type, qdata(bufptr, l));
  return;

fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int ipip_attach(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  int fd;
  int port;
  int type;
  int32 ipaddr;
  struct edv_t *edv;
  struct iface *ifp;
  struct sockaddr_in addr;

  if (if_lookup(argv[1]) != NULLIF) {
    printf("Interface %s already exists\n", argv[1]);
    return (-1);
  }

  switch (*argv[2]) {
  case 'I':
  case 'i':
    type = IP_OVER_IP;
    break;
  case 'U':
  case 'u':
    type = IP_OVER_UDP;
    break;
  default:
    printf("Type must be IP or UDP\n");
    return (-1);
  }

  if (!(ipaddr = resolve(argv[3]))) {
    printf(Badhost, argv[3]);
    return (-1);
  }

  port = atoi(argv[4]);

  if (type == IP_OVER_IP)
    fd = socket(AF_INET, SOCK_RAW, port);
  else
    fd = socket(AF_INET, SOCK_DGRAM, 0);
  if (fd < 0) {
    printf("cannot create socket: %s\n", sys_errlist[errno]);
    return (-1);
  }

  if (type == IP_OVER_UDP) {
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(fd, &addr, sizeof(addr))) {
      printf("cannot bind address: %s\n", sys_errlist[errno]);
      return (-1);
    }
  }

  ifp = callocw(1, sizeof(*ifp));
  ifp->name = strdup(argv[1]);
  ifp->addr = Ip_addr;
  ifp->mtu = MAX_FRAME;
  setencap(ifp, "None");

  edv = calloc(1, sizeof(*edv));
  edv->type = type;
  edv->fd = fd;
  edv->addr.sin_family = AF_INET;
  edv->addr.sin_addr.s_addr = htonl(ipaddr);
  edv->addr.sin_port = htons(port);
  ifp->edv = edv;

  ifp->send = ipip_send;
  on_read(fd, ipip_recv, (void * ) ifp);
  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

