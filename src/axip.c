/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axip.c,v 1.1 1991/07/16 17:56:27 deyke Exp $ */

#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#undef IPPROTO_IP

extern char *sys_errlist[];
extern int errno;

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "netuser.h"
#include "ax25.h"
#include "socket.h"
#include "trace.h"
#include "pktdrvr.h"
#include "cmdparse.h"
#include "hpux.h"

#define MAX_FRAME       2048

struct axip_route {
  char call[AXALEN];
  struct sockaddr_in addr;
  struct axip_route *next;
};

static int sock = -1;
static struct axip_route *Axip_routes;
static struct iface *Axip_iface;

static int axip_raw __ARGS((struct iface *iface, struct mbuf *data));
static void axip_recv __ARGS((void *argp));
static void axip_route_add __ARGS((char *call, const struct sockaddr_in *addr));
static int doaxiproute __ARGS((int argc, char *argv [], void *p));
static int doaxiprouteadd __ARGS((int argc, char *argv [], void *p));
static int doaxiproutedrop __ARGS((int argc, char *argv [], void *p));

/*---------------------------------------------------------------------------*/

static int16 fcstab[256] = {
  0x0000, 0x1189, 0x2312, 0x329b, 0x4624, 0x57ad, 0x6536, 0x74bf,
  0x8c48, 0x9dc1, 0xaf5a, 0xbed3, 0xca6c, 0xdbe5, 0xe97e, 0xf8f7,
  0x1081, 0x0108, 0x3393, 0x221a, 0x56a5, 0x472c, 0x75b7, 0x643e,
  0x9cc9, 0x8d40, 0xbfdb, 0xae52, 0xdaed, 0xcb64, 0xf9ff, 0xe876,
  0x2102, 0x308b, 0x0210, 0x1399, 0x6726, 0x76af, 0x4434, 0x55bd,
  0xad4a, 0xbcc3, 0x8e58, 0x9fd1, 0xeb6e, 0xfae7, 0xc87c, 0xd9f5,
  0x3183, 0x200a, 0x1291, 0x0318, 0x77a7, 0x662e, 0x54b5, 0x453c,
  0xbdcb, 0xac42, 0x9ed9, 0x8f50, 0xfbef, 0xea66, 0xd8fd, 0xc974,
  0x4204, 0x538d, 0x6116, 0x709f, 0x0420, 0x15a9, 0x2732, 0x36bb,
  0xce4c, 0xdfc5, 0xed5e, 0xfcd7, 0x8868, 0x99e1, 0xab7a, 0xbaf3,
  0x5285, 0x430c, 0x7197, 0x601e, 0x14a1, 0x0528, 0x37b3, 0x263a,
  0xdecd, 0xcf44, 0xfddf, 0xec56, 0x98e9, 0x8960, 0xbbfb, 0xaa72,
  0x6306, 0x728f, 0x4014, 0x519d, 0x2522, 0x34ab, 0x0630, 0x17b9,
  0xef4e, 0xfec7, 0xcc5c, 0xddd5, 0xa96a, 0xb8e3, 0x8a78, 0x9bf1,
  0x7387, 0x620e, 0x5095, 0x411c, 0x35a3, 0x242a, 0x16b1, 0x0738,
  0xffcf, 0xee46, 0xdcdd, 0xcd54, 0xb9eb, 0xa862, 0x9af9, 0x8b70,
  0x8408, 0x9581, 0xa71a, 0xb693, 0xc22c, 0xd3a5, 0xe13e, 0xf0b7,
  0x0840, 0x19c9, 0x2b52, 0x3adb, 0x4e64, 0x5fed, 0x6d76, 0x7cff,
  0x9489, 0x8500, 0xb79b, 0xa612, 0xd2ad, 0xc324, 0xf1bf, 0xe036,
  0x18c1, 0x0948, 0x3bd3, 0x2a5a, 0x5ee5, 0x4f6c, 0x7df7, 0x6c7e,
  0xa50a, 0xb483, 0x8618, 0x9791, 0xe32e, 0xf2a7, 0xc03c, 0xd1b5,
  0x2942, 0x38cb, 0x0a50, 0x1bd9, 0x6f66, 0x7eef, 0x4c74, 0x5dfd,
  0xb58b, 0xa402, 0x9699, 0x8710, 0xf3af, 0xe226, 0xd0bd, 0xc134,
  0x39c3, 0x284a, 0x1ad1, 0x0b58, 0x7fe7, 0x6e6e, 0x5cf5, 0x4d7c,
  0xc60c, 0xd785, 0xe51e, 0xf497, 0x8028, 0x91a1, 0xa33a, 0xb2b3,
  0x4a44, 0x5bcd, 0x6956, 0x78df, 0x0c60, 0x1de9, 0x2f72, 0x3efb,
  0xd68d, 0xc704, 0xf59f, 0xe416, 0x90a9, 0x8120, 0xb3bb, 0xa232,
  0x5ac5, 0x4b4c, 0x79d7, 0x685e, 0x1ce1, 0x0d68, 0x3ff3, 0x2e7a,
  0xe70e, 0xf687, 0xc41c, 0xd595, 0xa12a, 0xb0a3, 0x8238, 0x93b1,
  0x6b46, 0x7acf, 0x4854, 0x59dd, 0x2d62, 0x3ceb, 0x0e70, 0x1ff9,
  0xf78f, 0xe606, 0xd49d, 0xc514, 0xb1ab, 0xa022, 0x92b9, 0x8330,
  0x7bc7, 0x6a4e, 0x58d5, 0x495c, 0x3de3, 0x2c6a, 0x1ef1, 0x0f78
};

/*---------------------------------------------------------------------------*/

static int axip_raw(iface, data)
struct iface *iface;
struct mbuf *data;
{

  char *dest;
  char *p;
  char buf[MAX_FRAME];
  int cnt;
  int l;
  int16 fcs;
  struct axip_route *rp;
  struct mbuf *bp;

  dump(iface, IF_TRACE_OUT, iface->type, data);
  iface->rawsndcnt++;
  iface->lastsent = secclock();

  fcs = 0xffff;
  for (bp = data; bp; bp = bp->next) {
    p = bp->data;
    for (cnt = bp->cnt; cnt > 0; cnt--)
      fcs = (fcs >> 8) ^ fcstab[(fcs ^ *p++) & 0xff];
  }
  fcs ^= 0xffff;
  bp = ambufw(2);
  bp->data[0] = fcs;
  bp->data[1] = fcs >> 8;
  bp->cnt = 2;
  append(&data, bp);

  if (iface->trace & IF_TRACE_RAW)
    raw_dump(iface, -1, data);

  l = pullup(&data, buf, sizeof(buf));
  if (l <= 0 || data) {
    free_p(data);
    return (-1);
  }

  dest = buf;
  p = dest + AXALEN;
  while (!(p[6] & E)) {
    p += AXALEN;
    if (!(p[6] & REPEATED)) {
      dest = p;
      break;
    }
  }

  for (rp = Axip_routes; ; rp = rp->next) {
    if (!rp) return (-1);
    if (addreq(rp->call, dest)) break;
  }

  return sendto(sock, buf, l, 0, (struct sockaddr *) & rp->addr, sizeof(rp->addr));
}

/*---------------------------------------------------------------------------*/

static void axip_recv(argp)
void *argp;
{

  char *bufptr;
  char *p;
  char *src;
  char buf[MAX_FRAME];
  int cnt;
  int fromlen;
  int hdr_len;
  int l;
  int16 fcs;
  struct ip *ipptr;
  struct sockaddr_in from;

  fromlen = sizeof(from);
  l = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *) & from, &fromlen);
  if (l <= sizeof(struct ip )) return;
  ipptr = (struct ip *) buf;
  hdr_len = 4 * ipptr->ip_hl;
  bufptr = buf + hdr_len;
  l -= hdr_len;
  if (l <= 2) return;

  fcs = 0xffff;
  p = bufptr;
  for (cnt = l; cnt > 0; cnt--)
    fcs = (fcs >> 8) ^ fcstab[(fcs ^ *p++) & 0xff];
  if (fcs != 0xf0b8) return;
  l -= 2;

  p = src = bufptr + AXALEN;
  while (!(p[6] & E)) {
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }
  axip_route_add(src, &from);

  net_route(Axip_iface, Axip_iface->type, qdata(bufptr, l));
}

/*---------------------------------------------------------------------------*/

int axip_attach(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  if (Axip_iface) {
    tprintf("axip interface already attached\n");
    return (-1);
  }

  sock = socket(AF_INET, SOCK_RAW, IPPROTO_AX25);
  if (sock < 0) {
    tprintf("cannot create raw socket: %s\n", sys_errlist[errno]);
    return (-1);
  }

  Axip_iface = callocw(1, sizeof(struct iface ));
  Axip_iface->addr = Ip_addr;
  Axip_iface->name = strdup("axip");
  Axip_iface->hwaddr = mallocw(AXALEN);
  addrcp(Axip_iface->hwaddr, Mycall);
  Axip_iface->mtu = 256;
  setencap(Axip_iface, "AX25");
  Axip_iface->raw = axip_raw;
  on_read(sock, axip_recv, (void * ) 0);
  Axip_iface->next = Ifaces;
  Ifaces = Axip_iface;

  return 0;
}

/*---------------------------------------------------------------------------*/

static void axip_route_add(call, addr)
char *call;
const struct sockaddr_in *addr;
{
  struct axip_route *rp;

  for (rp = Axip_routes; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp) {
    rp = malloc(sizeof(*rp));
    addrcp(rp->call, call);
    rp->next = Axip_routes;
    Axip_routes = rp;
  }
  rp->addr = *addr;
}

/*---------------------------------------------------------------------------*/

static struct cmds Axipcmds[] = {
  "route",  doaxiproute, 0, 0, NULLCHAR,
  NULLCHAR, NULLFP,      0, 0, NULLCHAR
};

int doaxip(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  return subcmd(Axipcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static struct cmds Axiproutecmds[] = {
  "add",    doaxiprouteadd,  0, 3, "axip route add <call> <host>",
  "drop",   doaxiproutedrop, 0, 2, "axip route drop <call>",
  NULLCHAR, NULLFP,          0, 0, NULLCHAR
};

static int doaxiproute(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  char buf[AXBUF];
  struct axip_route *rp;

  if (argc >= 2)
    return subcmd(Axiproutecmds, argc, argv, p);

  tprintf("Call       Addr\n");
  for (rp = Axip_routes; rp; rp = rp->next)
    tprintf("%-9s  %s\n",
	    pax25(buf, rp->call),
	    inet_ntoa(ntohl(rp->addr.sin_addr.s_addr)));
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiprouteadd(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  char call[AXALEN];
  int32 ipaddr;
  struct sockaddr_in addr;

  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (!(ipaddr = resolve(argv[2]))) {
    printf(Badhost, argv[2]);
    return 1;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(ipaddr);
  axip_route_add(call, &addr);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doaxiproutedrop(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  char call[AXALEN];
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

