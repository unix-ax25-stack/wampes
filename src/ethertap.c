/* @(#) $Id: ethertap.c,v 1.2 2000/01/15 18:34:15 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "global.h"
#include "hpux.h"
#include "iface.h"
#include "mbuf.h"
#include "netuser.h"
#include "strerror.h"
#include "trace.h"

struct edv_t {
  int fd;
};

struct ethertap_packet {
  char ethernet_header[16];
  char data[2048];
};

/*---------------------------------------------------------------------------*/

static int ethertap_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;

  static const unsigned char ethernet_header[16] = {
    0x00, 0x00,                                 /* ??? */
    0xfe, 0xfd, 0x00, 0x00, 0x00, 0x00,         /* Destination address (kernel ethertap module) */
    0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00,         /* Source address (WAMPES ethertap module) */
    0x08, 0x00                                  /* Protocol (IP) */
  };

  edv = (struct edv_t *) ifp->edv;
  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();
  if (ifp->trace & IF_TRACE_RAW) {
    raw_dump(ifp, -1, *bpp);
  }
  l = pullup(bpp, ethertap_packet.data, sizeof(ethertap_packet.data));
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }
  memcpy(ethertap_packet.ethernet_header, (const char *) ethernet_header, sizeof(ethernet_header));
  write(edv->fd, &ethertap_packet, l + sizeof(ethertap_packet.ethernet_header));
  return l;
}

/*---------------------------------------------------------------------------*/

static void ethertap_recv(void *argp)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;
  struct iface *ifp;
  struct mbuf *bp;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  l = read(edv->fd, &ethertap_packet, sizeof(ethertap_packet)) - sizeof(ethertap_packet.ethernet_header);
  if (l <= 0 ||
      ethertap_packet.ethernet_header[14] != 0x08 ||
      ethertap_packet.ethernet_header[15] != 0x00) {
    goto Fail;
  }
  bp = qdata(ethertap_packet.data, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

int ethertap_attach(int argc, char *argv[], void *p)
{

  char *ifname;
  char devname[1024];
  int fd;
  struct edv_t *edv;
  struct iface *ifp;

  ifname = argv[1];
  if (if_lookup(ifname)) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }
  strcpy(devname, "/dev/");
  strcat(devname, ifname);
  fd = open(devname, O_RDWR);
  if (fd < 0) {
    printf("%s: %s\n", devname, strerror(errno));
    return -1;
  }
  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->mtu = 0xffff;
  setencap(ifp, "None");
  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->fd = fd;
  ifp->edv = edv;
  ifp->send = ethertap_send;
  on_read(fd, ethertap_recv, (void *) ifp);
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}
