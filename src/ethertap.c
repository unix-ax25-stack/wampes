/* @(#) $Id: ethertap.c,v 1.4 2002/06/19 12:05:55 dl9sau Exp $ */

/* the ethertap device. now with TUN/TAP support (by dl9sau) */

/*
 * Changes:
 *
 * 20020619 dl9sau:
 * - now with the generic TUN/TAP driver support for kernel >= 2.4.x
 *   linux kernel doku: the classic ethertap device (/dev/tapX)
 *   is obsolete and will be removed soon. tun/tap uses /dev/net/tun
 * - now supports rx/tx BPQether protocol (ax25-over-ethernet)
 *   the bpqether device generated automaticaly:
 *   "attach ethertap linux" becomes linux and linuxBPQ
 *   this is a more efficient way than attaching an mkiss device
 *   to a pseudo-tty for the link between kernel-ax25 and wampes.
 * - cave MTU: there seems to be a bug. mtu = 1500 has a throughput
 *   of 9k6 bit/s. mtu = 1024 makes ca. 1Mbit -> you may use the new mtu
 *   arg when attaching an ethertap device
 * - on attach, we now probe the hardware-address (MAC) of the device.
 *   this is needed because tun/tap accepts only correctly addressed
 *   packets.
 */


/* the ethertap device. now with TUN/TAP support (by dl9sau) */

/*
 * Changes:
 *
 * 20020619 dl9sau:
 * - now with the generic TUN/TAP driver support for kernel >= 2.4.x
 *   linux kernel doku: the classic ethertap device (/dev/tapX)
 *   is obsolete and will be removed soon. tun/tap uses /dev/net/tun
 * - now supports rx/tx BPQether protocol (ax25-over-ethernet)
 *   the bpqether device generated automaticaly:
 *   "attach ethertap linux" becomes linux and linuxBPQ
 *   this is a more efficient way than attaching an mkiss device
 *   to a pseudo-tty for the link between kernel-ax25 and wampes.
 * - cave MTU: there seems to be a bug. mtu = 1500 has a throughput
 *   of 9k6 bit/s. mtu = 1024 makes ca. 1Mbit -> you may use the new mtu
 *   arg when attaching an ethertap device
 * - on attach, we now probe the hardware-address (MAC) of the device.
 *   this is needed because tun/tap accepts only correctly addressed
 *   packets.
 */


#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef	linux
#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif
#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(2,4,0)-1
#endif

#include <net/if.h>

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#define TRY_TUNTAP 1
#include <linux/if_tun.h>
#endif

#endif /* linux */

#include "global.h"
#include "hpux.h"
#include "iface.h"
#include "mbuf.h"
#include "netuser.h"
#include "strerror.h"
#include "ax25.h"
#include "devparam.h"
#include "trace.h"

struct edv_t {
  int fd;
  int version;
  uint8 hwaddr_remote[6];
};

#define ETAP_MTU_MIN	256
#define ETAP_MTU	1024
#define ETAP_MTU_MAX	1500
#define	MAX_FRAME	2048

struct ethertap_packet {
  char ethernet_header[18];
  char data[MAX_FRAME];
};

/*---------------------------------------------------------------------------*/

static int ethertap_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;
  void *addr = &ethertap_packet;
  int offset = 0;

  static const unsigned char ethernet_header[18] = {
    0x00, 0x00, 0x08, 0x00,                     /* ??? ??? ETH_P_AX25 (16bit) */
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
  memcpy(ethertap_packet.ethernet_header + 4, edv->hwaddr_remote, 6);
  memcpy(ethertap_packet.ethernet_header + 4 + 6 + 2, edv->hwaddr_remote +2, 6 -2);
  if (!edv->version) {
    offset = 2;
    addr += offset;
  }
  write(edv->fd, addr, l + sizeof(ethertap_packet.ethernet_header) - offset);
  return l;
}

/*---------------------------------------------------------------------------*/

static int ethertap_send_bpq(struct iface *ifp, struct mbuf **bpp)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;
  void *addr = &ethertap_packet;
  int offset = 0;

  static const unsigned char ethernet_header[18] = {
    0x00, 0x00, 0x00, 0x02,                     /* ??? ??? ETH_P_AX25 (16bit) */
    0xfe, 0xfd, 0x00, 0x00, 0x00, 0x00,         /* Destination address (kernel ethertap module) */
    0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00,         /* Source address (WAMPES ethertap module) */
    0x08, 0xff                                  /* Protocol (bpqether) */
  };

  edv = (struct edv_t *) ifp->edv;
  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();
  if (ifp->trace & IF_TRACE_RAW) {
    raw_dump(ifp, -1, *bpp);
  }
  l = pullup(bpp, ethertap_packet.data + 2, sizeof(ethertap_packet.data) -2);
  if (l <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }
  memcpy(ethertap_packet.ethernet_header, (const char *) ethernet_header, sizeof(ethernet_header));
  memcpy(ethertap_packet.ethernet_header + 4, edv->hwaddr_remote, 6);
  memcpy(ethertap_packet.ethernet_header + 4 + 6 + 2, edv->hwaddr_remote +2, 6 -2);
  ethertap_packet.data[0] = (l + 5) % 256;
  ethertap_packet.data[1] = (l + 5) / 256;
  l += 2;
  if (!edv->version) {
    offset = 2;
    addr += offset;
  }
  write(edv->fd, addr, l + sizeof(ethertap_packet.ethernet_header) - offset);
  return l;
}

/*---------------------------------------------------------------------------*/

static void ethertap_recv(void *argp)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;
  struct iface *ifp, *ifp_bpq;
  struct mbuf *bp;
  int offset = 0;
  void *addr = &ethertap_packet;

  ifp = (struct iface *) argp;
  edv = (struct edv_t *) ifp->edv;
  if (!edv->version) {
    offset = 2;
    addr += offset;
  }
  l = read(edv->fd, addr, sizeof(ethertap_packet) - offset) - sizeof(ethertap_packet.ethernet_header) + offset;

  offset = 0;
  if (l <= 0 || ethertap_packet.ethernet_header[16] != 0x08)
    goto Fail;
  switch (ethertap_packet.ethernet_header[17] & 0xff) {
  case 0x0:
    break;
  case 0xff:
    // bpqether signature (0x80 0xff)
    offset = 2;
    l -= offset;
    if (l <= 0 || (ethertap_packet.data[0] & 0xff) + (ethertap_packet.data[1] & 0xff) * 256 - 5 != l) {
      goto Fail;
    }
    // we share the same filedescriptor on read
    for (ifp_bpq = Ifaces; ifp_bpq; ifp_bpq = ifp_bpq->next)
      if (ifp != ifp_bpq && ifp->edv == ifp_bpq->edv)
        break;
    if (!(ifp = ifp_bpq))
      goto Fail;
    break;
  default:
    goto Fail;
  }
  bp = qdata(ethertap_packet.data + offset, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/
/* TUN/TAP support for linux. ethertap is obsolete */

#ifdef	TRY_TUNTAP
int tun_alloc(char *dev)
{
    struct ifreq ifr;
    int fd, err;

    if ((fd = open("/dev/net/tun", O_RDWR)) < 0)
       return -1;

    memset(&ifr, 0, sizeof(ifr));

    /* Flags: IFF_TUN   - TUN device (no Ethernet headers)
     *        IFF_TAP   - TAP device
     *
     *        IFF_NO_PI - Do not provide packet information
     */
    ifr.ifr_flags = IFF_TAP;
    if (*dev) {
       strncpy(ifr.ifr_name, dev, IFNAMSIZ);
       ifr.ifr_name[IFNAMSIZ-1] = 0;
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *) &ifr)) < 0 ) {
       close(fd);
       return err;
    }
    strcpy(dev, ifr.ifr_name);

    /* persist mode */
    //if (ioctl(fd, TUNSETPERSIST, 1) < 0)
      //perror("TUNSETPERSIST");

    /* don't checksum */
    //if (ioctl(fd, TUNSETNOCSUM, 1) < 0)
      //perror("TUNSETNOCSUM");

    return fd;
}              
#endif

/*---------------------------------------------------------------------------*/

int ethertap_attach(int argc, char *argv[], void *p)
{

  char *ifname;
  char devname[1024];
  uint8 hwaddr[6];
  int fd;
  struct edv_t *edv;
  struct iface *ifp;
  struct ifreq ifr;
  struct stat statbuf;
  int version = 0;
  int ifp_mtu = 0;
  int skfd;

  ifname = argv[1];
  if (if_lookup(ifname)) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }

  strcpy(devname, "/dev/");
  strcat(devname, ifname);
  // version 0: original ethertap. works on a real character device
  if (!stat(devname, &statbuf)) {
    fd = open(devname, O_RDWR);
    if (fd < 0) {
      printf("%s: %s\n", devname, strerror(errno));
      return -1;
    }
#ifdef	TRY_TUNTAP
  } else {
    strcpy(devname, ifname);
    if ((fd = tun_alloc(devname)) < 0) {
      printf("%s: %s\n", devname, strerror(errno));
      return -1;
    }
    ifname = devname;
    version = 1;
#endif
  }

  if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket()");
    close(fd);
    return;
  }

  memset(&ifr, 0, sizeof(ifr));

  strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ-1] = 0;
  if (ioctl(skfd, SIOCGIFFLAGS, &ifr) < 0)
    perror("SIOCGIFFLAGS");

  ifr.ifr_flags |= (IFF_UP | IFF_NOARP);
  if (ioctl(skfd, SIOCSIFFLAGS, &ifr) < 0)
    perror("SIOCSIFFLAGS");

  strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ-1] = 0;
  if (ioctl(skfd, SIOCGIFHWADDR, &ifr) < 0)
    perror("SIOCGIFHWADDR");
  else
    memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);

  if (argc > 2) {
    ifp_mtu = atoi(argv[2]);
  } else {
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
    ifr.ifr_name[IFNAMSIZ-1] = 0;
    strcpy(ifr.ifr_name, ifname);
    if (ioctl(skfd, SIOCGIFMTU, &ifr) < 0)
      perror("SIOCGIFMTU");
    else
      ifp_mtu = ifr.ifr_mtu;
  }
  if (ifp_mtu < ETAP_MTU_MIN || ifp_mtu > ETAP_MTU_MAX)
    ifp_mtu = ETAP_MTU;
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ);
  ifr.ifr_name[IFNAMSIZ-1] = 0;
  ifr.ifr_mtu = ifp_mtu;
  if (ioctl(skfd, SIOCSIFMTU, &ifr) < 0)
    perror("SIOSGIFMTU");

  close(skfd);

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(ifname);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->mtu = ifp_mtu;
  setencap(ifp, "None");
  edv = (struct edv_t *) malloc(sizeof(struct edv_t));
  edv->fd = fd;
  edv->version = version;
  memcpy(edv->hwaddr_remote, hwaddr, 6);
  ifp->edv = edv;
  ifp->send = ethertap_send;
  on_read(fd, ethertap_recv, (void *) ifp);
  ifp->next = Ifaces;
  Ifaces = ifp;
  ethertap_attach_bpq(ifp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int ethertap_attach_bpq(struct iface *to_ifp)
{

  char *ifname;
  struct iface *ifp;
  char *appendix = "BPQ";

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = malloc(strlen(to_ifp->name) + strlen(appendix) +1);
  sprintf(ifp->name, "%s%s", to_ifp->name, appendix);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  ifp->mtu = 256;
  setencap(ifp, "AX25UI");
  ifp->edv = to_ifp->edv;
  ifp->send = axui_send;
  ifp->raw = ethertap_send_bpq;
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}

