/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ni.c,v 1.14 1996/08/11 18:16:09 deyke Exp $ */

#ifdef __hpux

#include "global.h"
#undef  hiword
#undef  loword
#undef  hibyte
#undef  lobyte

#include <sys/types.h>

#include <sys/socket.h>

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <net/if_ni.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "strerror.h"

#include "mbuf.h"
#include "iface.h"
#include "netuser.h"
#include "trace.h"
#include "hpux.h"

#define NI_MTU          256
#define NI_MQL          16
#define MAX_FRAME       2048

struct edv_t {
  int fd;
};

struct ni_packet {
  struct sockaddr addr;
  char data[MAX_FRAME];
};

/*---------------------------------------------------------------------------*/

static int ni_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  int l;
  struct edv_t *edv;
  struct ni_packet ni_packet;

  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, *bpp);

  memset(&ni_packet, 0, sizeof(struct ni_packet));
  l = pullup(bpp, ni_packet.data, sizeof(ni_packet.data));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }

  edv = (struct edv_t *) ifp->edv;

  ni_packet.addr.sa_family = AF_INET;

  write(edv->fd, &ni_packet, l + sizeof(ni_packet.addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void ni_recv(void *argp)
{

  int l;
  struct edv_t *edv;
  struct iface *ifp;
  struct mbuf *bp;
  struct ni_packet ni_packet;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  l = read(edv->fd, &ni_packet, sizeof(ni_packet)) - sizeof(ni_packet.addr);
  if (l <= 0) goto Fail;

  bp = qdata(ni_packet.data, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int ni_attach(int argc, char *argv[], void *p)
{

  char *ifname;
  int arg;
  int fd;
  int sock_fd;
  int unit_number;
  int32 dest;
  int32 mask;
  struct edv_t *edv;
  struct iface *ifp;
  struct ifreq ifreq;
  struct rtentry rtentry;
  struct sockaddr_in addr;

  ifname = argv[1];

  if (if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }

  if (!(dest = resolve(argv[2]))) {
    printf(Badhost, argv[2]);
    return -1;
  }

  mask = 0xff000000;
  if (argc >= 4 && !(mask = resolve(argv[3]))) {
    printf(Badhost, argv[3]);
    return -1;
  }

  if ((fd = open("/dev/ni", O_RDWR)) < 0) {
    printf("/dev/ni: %s\n", strerror(errno));
    return -1;
  }

  if (ioctl(fd, NIOCGUNIT, &unit_number)) {
    printf("NIOCGUNIT: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  arg = AF_INET;
  if (ioctl(fd, NIOCBIND, &arg)) {
    printf("NIOCBIND: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  arg = NI_MTU;
  if (ioctl(fd, NIOCSMTU, &arg)) {
    printf("NIOCSMTU: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  arg = NI_MQL;
  if (ioctl(fd, NIOCSQLEN, &arg)) {
    printf("NIOCSQLEN: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  arg = IFF_UP | IFF_POINTOPOINT;
  if (ioctl(fd, NIOCSFLAGS, &arg)) {
    printf("NIOCSFLAGS: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    printf("socket: %s\n", strerror(errno));
    close(fd);
    return -1;
  }

  memset(&ifreq, 0, sizeof(struct ifreq));
  sprintf(ifreq.ifr_name, "ni%d", unit_number);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  addr.sin_addr.s_addr = htonl(dest);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFADDR, (char *) &ifreq)) {
    printf("SIOCSIFADDR: %s\n", strerror(errno));
    close(fd);
    close(sock_fd);
    return -1;
  }

  addr.sin_addr.s_addr = htonl(Ip_addr);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFDSTADDR, (char *) &ifreq)) {
    printf("SIOCSIFDSTADDR: %s\n", strerror(errno));
    close(fd);
    close(sock_fd);
    return -1;
  }

  addr.sin_addr.s_addr = htonl(mask);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFNETMASK, (char *) &ifreq)) {
    printf("SIOCSIFNETMASK: %s\n", strerror(errno));
    close(fd);
    close(sock_fd);
    return -1;
  }

  addr.sin_addr.s_addr = htonl(dest & mask);
  rtentry.rt_dst = *((struct sockaddr *) &addr);
  addr.sin_addr.s_addr = htonl(dest);
  rtentry.rt_gateway = *((struct sockaddr *) &addr);
  rtentry.rt_flags = RTF_UP;
  ioctl(sock_fd, SIOCADDRT, (char *) &rtentry);

  close(sock_fd);

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->mtu = NI_MTU;
  setencap(ifp, "None");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->fd = fd;
  ifp->edv = edv;

  ifp->send = ni_send;
  on_read(fd, ni_recv, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

#else

struct prevent_empty_file_message;

#endif
