/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ni.c,v 1.1 1992/08/02 07:34:17 deyke Exp $ */

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
#define NI_HEADER       16
#define MAX_FRAME       2048
#define BUF_SIZE        (NI_HEADER + MAX_FRAME)

static int Ni_fd;
static struct iface *Ni_iface;

/*---------------------------------------------------------------------------*/

static int ni_send(data, ifp, gateway, tos)
struct mbuf *data;
struct iface *ifp;
int32 gateway;
int tos;
{

  char buf[BUF_SIZE];
  int n;

  dump(ifp, IF_TRACE_OUT, data);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, data);

  n = pullup(&data, buf + NI_HEADER, sizeof(buf) - NI_HEADER);
  if (n <= 0 || data) {
    free_p(data);
    return (-1);
  }

  memset(buf, 0, NI_HEADER);
  buf[1] = 2;

  write(Ni_fd, buf, NI_HEADER + n);
  return n;
}

/*---------------------------------------------------------------------------*/

static void ni_recv(argp)
void *argp;
{

  char buf[BUF_SIZE];
  int n;

  n = read(Ni_fd, buf, sizeof(buf));
  if (n > NI_HEADER)
    net_route(Ni_iface, qdata(buf + NI_HEADER, n - NI_HEADER));
  else
    Ni_iface->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int ni_attach(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  int arg;
  int sock_fd;
  int32 dest;
  int32 mask;
  struct ifreq ifreq;
  struct rtentry rtentry;
  struct sockaddr_in addr;

  if (Ni_iface != NULLIF) {
    printf("ni interface already attached\n");
    return (-1);
  }

  if (if_lookup(argv[1]) != NULLIF) {
    printf("Interface %s already exists\n", argv[1]);
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

  if ((Ni_fd = open("/dev/ni", O_RDWR)) < 0) {
    printf("/dev/ni: %s\n", sys_errlist[errno]);
    return (-1);
  }

  arg = AF_INET;
  if (ioctl(Ni_fd, NIOCBIND, &arg)) {
    printf("NIOCBIND: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    return (-1);
  }

  arg = NI_MTU;
  if (ioctl(Ni_fd, NIOCSMTU, &arg)) {
    printf("NIOCSMTU: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    return (-1);
  }

  arg = IFF_UP | IFF_POINTOPOINT;
  if (ioctl(Ni_fd, NIOCSFLAGS, &arg)) {
    printf("NIOCSFLAGS: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    return (-1);
  }

  if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    printf("socket: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    return (-1);
  }

  strcpy(ifreq.ifr_name, "ni0");
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;

  addr.sin_addr.s_addr = htonl(dest);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFADDR, (char *) &ifreq)) {
    printf("SIOCSIFADDR: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    close(sock_fd);
    return (-1);
  }

  addr.sin_addr.s_addr = htonl(Ip_addr);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFDSTADDR, (char *) &ifreq)) {
    printf("SIOCSIFDSTADDR: %s\n", sys_errlist[errno]);
    close(Ni_fd);
    close(sock_fd);
    return (-1);
  }

  addr.sin_addr.s_addr = htonl(mask);
  ifreq.ifr_addr = *((struct sockaddr *) &addr);
  if (ioctl(sock_fd, SIOCSIFNETMASK, (char *) &ifreq)) {
    printf("SIOCSIFNETMASK: %s\n", sys_errlist[errno]);
    close(Ni_fd);
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

  Ni_iface = callocw(1, sizeof(*Ni_iface));
  Ni_iface->name = strdup(argv[1]);
  Ni_iface->addr = Ip_addr;
  Ni_iface->mtu = NI_MTU;
  setencap(Ni_iface, "None");
  Ni_iface->send = ni_send;
  on_read(Ni_fd, ni_recv, (void * ) Ni_iface);

  Ni_iface->next = Ifaces;
  Ifaces = Ni_iface;

  return 0;
}

#else

int ni_attach(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  printf("ni interface not available\n");
  return (-1);
}

#endif

