/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ni.c,v 1.3 1993/05/17 13:45:12 deyke Exp $ */

#ifdef __hpux

#include "global.h"

#include <sys/types.h>

#include <sys/socket.h>

#include <fcntl.h>
#include <net/if.h>
#include <net/if_ni.h>
#include <net/route.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

extern char *sys_errlist[];
extern int errno;

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

static int ni_send(struct mbuf *data, struct iface *ifp, int32 gateway, int tos);
static void ni_recv(void *argp);

/*---------------------------------------------------------------------------*/

static int ni_send(struct mbuf *data, struct iface *ifp, int32 gateway, int tos)
{

  int l;
  struct edv_t *edv;
  struct ni_packet ni_packet;

  dump(ifp, IF_TRACE_OUT, data);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, data);

  l = pullup(&data, ni_packet.data, sizeof(ni_packet.data));
  if (l <= 0 || data) {
    free_p(data);
    return (-1);
  }

  edv = ifp->edv;

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
  struct ni_packet ni_packet;

  ifp = argp;
  edv = ifp->edv;
  l = read(edv->fd, &ni_packet, sizeof(ni_packet)) - sizeof(ni_packet.addr);
  if (l <= 0) goto Fail;

  net_route(ifp, qdata(ni_packet.data, l));
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

  if (if_lookup(ifname) != NULLIF) {
    printf("Interface %s already exists\n", ifname);
    return (-1);
  }

  if (!(dest = resolve(argv[2]))) {
    printf(Badhost, argv[2]);
    return (-1);
  }

  mask = 0xff000000;
  if (argc >= 4 && !(mask = resolve(argv[3]))) {
    printf(Badhost, argv[3]);
    return (-1);
  }

  if ((fd = open("/dev/ni", O_RDWR)) < 0) {
    printf("/dev/ni: %s\n", sys_errlist[errno]);
    return (-1);
  }

  if (ioctl(fd, NIOCGUNIT, &unit_number)) {
    printf("NIOCGUNIT: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  arg = AF_INET;
  if (ioctl(fd, NIOCBIND, &arg)) {
    printf("NIOCBIND: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  arg = NI_MTU;
  if (ioctl(fd, NIOCSMTU, &arg)) {
    printf("NIOCSMTU: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  arg = NI_MQL;
  if (ioctl(fd, NIOCSQLEN, &arg)) {
    printf("NIOCSQLEN: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  arg = IFF_UP | IFF_POINTOPOINT;
  if (ioctl(fd, NIOCSFLAGS, &arg)) {
    printf("NIOCSFLAGS: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    printf("socket: %s\n", sys_errlist[errno]);
    close(fd);
    return (-1);
  }

  sprintf(ifreq.ifr_name, "ni%d", unit_number);
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  addr.sin_addr.s_addr = htonl(dest);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFADDR, (char *) &ifreq)) {
    printf("SIOCSIFADDR: %s\n", sys_errlist[errno]);
    close(fd);
    close(sock_fd);
    return (-1);
  }

  addr.sin_addr.s_addr = htonl(Ip_addr);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFDSTADDR, (char *) &ifreq)) {
    printf("SIOCSIFDSTADDR: %s\n", sys_errlist[errno]);
    close(fd);
    close(sock_fd);
    return (-1);
  }

  addr.sin_addr.s_addr = htonl(mask);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFNETMASK, (char *) &ifreq)) {
    printf("SIOCSIFNETMASK: %s\n", sys_errlist[errno]);
    close(fd);
    close(sock_fd);
    return (-1);
  }

  addr.sin_addr.s_addr = htonl(dest & mask);
  rtentry.rt_dst = *((struct sockaddr *) &addr);
  addr.sin_addr.s_addr = htonl(dest);
  rtentry.rt_gateway = *((struct sockaddr *) &addr);
  rtentry.rt_flags = RTF_UP;
  ioctl(sock_fd, SIOCADDRT, (char *) &rtentry);

  close(sock_fd);

  ifp = callocw(1, sizeof(*ifp));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->mtu = NI_MTU;
  setencap(ifp, "None");

  edv = malloc(sizeof(*edv));
  edv->fd = fd;
  ifp->edv = edv;

  ifp->send = ni_send;
  on_read(fd, ni_recv, (void * ) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

#endif

