/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axip.c,v 1.23 1995/12/30 15:05:42 deyke Exp $ */

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
#include "ax25.h"
#include "socket.h"
#include "trace.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "hpux.h"
#include "crc.h"

#define MAX_FRAME       2048

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
};

struct axip_route {
  uint8 call[AXALEN];
  int32 dest;
  struct axip_route *next;
};

static struct axip_route *Axip_routes;

static int axip_raw(struct iface *ifp, struct mbuf **bpp);
static void axip_recv(void *argp);
static void axip_route_add(uint8 *call, int32 dest);
static int doaxiproute(int argc, char *argv[], void *p);
static int doaxiprouteadd(int argc, char *argv[], void *p);
static int doaxiproutedrop(int argc, char *argv[], void *p);

/*---------------------------------------------------------------------------*/

static int axip_raw(struct iface *ifp, struct mbuf **bpp)
{

  int l;
  int multicast;
  struct axip_route *rp;
  struct edv_t *edv;
  struct sockaddr_in addr;
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

  dest = buf;
  p = dest + AXALEN;
  while (!(p[6] & E)) {
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
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(rp->dest);
      addr.sin_port = htons(edv->port);
      sendto(edv->fd, (char *) buf, l, 0, (struct sockaddr *) &addr, sizeof(addr));
    }

  return l;
}

/*---------------------------------------------------------------------------*/

static void axip_recv(void *argp)
{

  int addrlen;
  int hdr_len;
  int l;
  struct edv_t *edv;
  struct iface *ifp;
  struct ip *ipptr;
  struct mbuf *bp;
  struct sockaddr_in addr;
  uint8 buf[MAX_FRAME];
  uint8 *bufptr;
  uint8 *p;
  uint8 *src;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  addrlen = sizeof(addr);
  l = recvfrom(edv->fd, (char *) (bufptr = buf), sizeof(buf), 0, (struct sockaddr *) &addr, &addrlen);
  if (edv->type == USE_IP) {
    if (l <= sizeof(struct ip)) goto Fail;
    ipptr = (struct ip *) bufptr;
    hdr_len = 4 * ipptr->ip_hl;
    bufptr += hdr_len;
    l -= hdr_len;
  }
  if (l <= 2) goto Fail;

  if (!check_crc_ccitt((char *) bufptr, l)) goto Fail;
  l -= 2;

  p = src = bufptr + AXALEN;
  while (!(p[6] & E)) {
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }
  axip_route_add(src, ntohl(addr.sin_addr.s_addr));

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
  int fd;
  int port = AX25_PTCL;
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
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  ifp->mtu = 256;
  ifp->crccontrol = CRC_CCITT;
  setencap(ifp, "AX25UI");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->type = type;
  edv->port = port;
  edv->fd = fd;
  ifp->edv = edv;

  ifp->raw = axip_raw;
  on_read(fd, axip_recv, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

/*---------------------------------------------------------------------------*/

static void axip_route_add(uint8 *call, int32 dest)
{
  struct axip_route *rp;

  for (rp = Axip_routes; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp) {
    rp = (struct axip_route *) malloc(sizeof(struct axip_route));
    addrcp(rp->call, call);
    rp->next = Axip_routes;
    Axip_routes = rp;
  }
  rp->dest = dest;
}

/*---------------------------------------------------------------------------*/

static struct cmds Axipcmds[] = {
  "route",  doaxiproute, 0, 0, NULL,
  NULL,     NULL,        0, 0, NULL
};

int doaxip(int argc, char *argv[], void *p)
{
  return subcmd(Axipcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static struct cmds Axiproutecmds[] = {
  "add",    doaxiprouteadd,  0, 3, "axip route add <call> <host>",
  "drop",   doaxiproutedrop, 0, 2, "axip route drop <call>",
  NULL,     NULL,            0, 0, NULL
};

static int doaxiproute(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  struct axip_route *rp;

  if (argc >= 2)
    return subcmd(Axiproutecmds, argc, argv, p);

  printf("Call       Addr\n");
  for (rp = Axip_routes; rp; rp = rp->next)
    printf("%-9s  %s\n", pax25(buf, rp->call), inet_ntoa(rp->dest));
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiprouteadd(int argc, char *argv[], void *p)
{

  uint8 call[AXALEN];
  int32 dest;

  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (!(dest = resolve(argv[2]))) {
    printf(Badhost, argv[2]);
    return 1;
  }
  axip_route_add(call, dest);
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
