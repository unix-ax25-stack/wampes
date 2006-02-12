/* @(#) $Id: tun.c,v 1.6 2006/02/12 17:49:57 dl9sau Exp $ */

/*
   Interface to FreeBSD's tun device - Olaf Erb, dc1ik 960728
   parts and idea taken from FreeBSD's ppp implementation
 */

#if defined __FreeBSD__ || defined __MACOSX__

#include "global.h"
#undef  hiword
#undef  loword
#undef  hibyte
#undef  lobyte

#include <sys/types.h>

#include <sys/socket.h>
#include <sys/select.h>

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#ifdef	__FreeBSD__
#include <net/if_tun.h>
#endif
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

#define MAX_FRAME       2048
#define MAX_TUN         256

struct edv_t {
  int fd;
};

struct tun_packet {
  char data[MAX_FRAME];
  struct sockaddr addr;
};

static char *IfDevName;
static int IfIndex;
static struct ifaliasreq ifra;
static struct ifreq ifrq;

/*---------------------------------------------------------------------------*/

static int tun_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  int l;
  struct edv_t *edv;
  struct tun_packet tun_packet;

  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();

  if (ifp->trace & IF_TRACE_RAW)
    raw_dump(ifp, -1, *bpp);

  memset(&tun_packet, 0, sizeof(struct tun_packet));
  l = pullup(bpp, tun_packet.data, sizeof(tun_packet.data));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }

  edv = (struct edv_t *) ifp->edv;

  tun_packet.addr.sa_family = AF_INET;

  write(edv->fd, &tun_packet, l + sizeof(tun_packet.addr));

  return l;
}

/*---------------------------------------------------------------------------*/

static void tun_recv(void *argp)
{

  int l;
  struct edv_t *edv;
  struct iface *ifp;
  struct mbuf *bp;
  struct tun_packet tun_packet;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  l = read(edv->fd, &tun_packet, sizeof(tun_packet));
  if (l <= 0)
    goto Fail;

  bp = qdata(tun_packet.data, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

static int GetIfIndex(char *name)
{

  int s, len, elen, index;
  struct ifconf ifconfs;
  struct ifreq reqbuf[32];
  struct ifreq *ifrp;

  s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    perror("socket");
    return -1;
  }

  ifconfs.ifc_len = sizeof(reqbuf);
  ifconfs.ifc_buf = (caddr_t) reqbuf;
  if (ioctl(s, SIOCGIFCONF, &ifconfs) < 0) {
    perror("IFCONF");
    return -1;
  }

  ifrp = ifconfs.ifc_req;

  index = 1;
  for (len = ifconfs.ifc_len; len > 0; len -= sizeof(struct ifreq)) {
    elen = ifrp->ifr_addr.sa_len - sizeof(struct sockaddr);
    if (ifrp->ifr_addr.sa_family == AF_LINK) {
      if (strcmp(ifrp->ifr_name, name) == 0) {
	IfIndex = index;
	return index;
      }
      index++;
    }

    len -= elen;
    ifrp = (struct ifreq *) ((char *) ifrp + elen);
    ifrp++;
  }

  close(s);
  return -1;
}

/*---------------------------------------------------------------------------*/

int tun_attach(int argc, char *argv[], void *p)
{

  char devname[14];             /* sufficient room for "/dev/tun65535" */
  char ifname[IFNAMSIZ];
  char *ifnamew;
  int arg;
  int fd;
  int sock_fd;
  int s;
  int ifmtu;
  int unit_number;
  int32 dest;
  int32 mask;
  struct edv_t *edv;
  struct iface *ifp;
  struct ifreq ifreq;
  struct sockaddr_in addr;
#ifdef	__FreeBSD__
  struct tuninfo info;
#endif
  unsigned unit, enoentcount = 0;

  ifnamew = argv[1];
  ifmtu = atoi(argv[2]);

  if (if_lookup(ifnamew) != NULL) {
    printf("Interface %s already exists\n", ifnamew);
    return -1;
  }

  for (unit = 0; unit <= MAX_TUN; unit++) {
    sprintf(devname, "/dev/tun%d", unit);
    fd = open(devname, O_RDWR);
    if (fd >= 0)
      break;
    if (errno == ENXIO)
      unit = MAX_TUN + 1;
    else if (errno == ENOENT) {
      enoentcount++;
      if (enoentcount > 2)
	unit = MAX_TUN + 1;
    }
  }
  if (unit > MAX_TUN) {
    fprintf(stderr, "No tunnel device is available.\n");
    return -1;
  }

  /*
   * At first, name the interface.
   */
  strcpy(ifname, devname + 5);

  bzero((char *) &ifra, sizeof(ifra));
  bzero((char *) &ifrq, sizeof(ifrq));

  strncpy(ifrq.ifr_name, ifname, IFNAMSIZ);
  strncpy(ifra.ifra_name, ifname, IFNAMSIZ);

  s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) {
    perror("socket");
    return -1;
  }

  /*
   *  Now, bring up the interface.
   */
  if (ioctl(s, SIOCGIFFLAGS, &ifrq) < 0) {
    perror("SIOCGIFFLAGS");
    close(s);
    return -1;
  }

  ifrq.ifr_flags |= IFF_UP;
  if (ioctl(s, SIOCSIFFLAGS, &ifrq) < 0) {
    perror("SIOCSIFFLAGS");
    close(s);
    return -1;
  }

#ifdef	__FreeBSD__
  info.type = 0x6;      /* Ethernet */
  info.mtu = ifmtu;
  info.baudrate = 0;
  if (ioctl(fd, TUNSIFINFO, &info) < 0)
    perror("TUNSIFINFO");
#endif

  IfDevName = devname + 5;
  if (GetIfIndex(IfDevName) < 0) {
    fprintf(stderr, "can't find ifindex.\n");
    close(s);
    return -1;
  }
  printf("Using interface: %s\r\n", IfDevName);
  close(s);

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifnamew);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->mtu = ifmtu;
  setencap(ifp, "None");

  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->fd = fd;
  ifp->edv = edv;

  ifp->send = tun_send;
  on_read(fd, tun_recv, (void *) ifp);

  ifp->next = Ifaces;
  Ifaces = ifp;

  return 0;
}

#else

void tun_prevent_empty_file_message(void)
{
}

#endif
