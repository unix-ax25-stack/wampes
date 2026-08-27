/* @(#) $Id: ethertap.c,v 1.9 2006/02/12 17:49:57 dl9sau Exp $ */

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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,0)
#define TRY_TUNTAP 1
#include <net/if.h>
#include <linux/if_tun.h>
#else
#include <linux/if.h>
#endif

#else
#include  <net/if.h>
#endif /* linux */

#if defined __MACOSX__ || defined __FreeBSD__
#include <sys/types.h>
#include <sys/sysctl.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#endif

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

/* Used by ethertap_attach() and defined after it.  Declared here because gcc
 * 14 makes an implicit declaration an error rather than a warning:
 *
 *      ethertap.c:450: implicit declaration of function
 *                      'ethertap_attach_bpq'
 *
 * Only Linux calls it - the BPQ companion interface needs kernel AX.25 - so
 * it is only Linux that noticed.
 */
int ethertap_attach_bpq(struct iface *to_ifp);

/*---------------------------------------------------------------------------*/

static int ethertap_send(struct mbuf **bpp, struct iface *ifp, int32 gateway, uint8 tos)
{

  int l;
  struct edv_t *edv;
  struct ethertap_packet ethertap_packet;
  void *addr = &ethertap_packet;
  int offset = 0;

  static const unsigned char ethernet_header[18] = {
    0x00, 0x00, 0x08, 0x00,                     /* ??? ??? ETH_P_IP (16bit) */
    0xfe, 0xfd, 0x00, 0x00, 0x00, 0x00,         /* Destination address (kernel ethertap module) */
    0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00,         /* Source address (WAMPES ethertap module) */
    0x08, 0x00                                  /* Protocol (IP) (ETH_P_IP) */
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
#if defined	__MACOSX__ || defined __FreeBSD__
    offset = 4;
#else
    offset = 2;
#endif
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
    0xff, 0xfd, 0x00, 0x00, 0x00, 0x00,         /* Destination address (kernel ethertap module) */
    0xfe, 0xfe, 0x00, 0x00, 0x00, 0x00,         /* Source address (WAMPES ethertap module) */
    0x08, 0xff                                  /* Protocol (bpqether) (ETH_P_BPQ) */
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
#if defined	__MACOSX__ || defined	__FreeBSD__
      offset = 4;
#else
      offset = 2;
#endif
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
#if defined	__MACOSX__ || defined __FreeBSD__
    offset = 4;
#else
    offset = 2;
#endif
    addr += offset;
  }
  if ((l = read(edv->fd, addr, sizeof(ethertap_packet) - offset)) <= (sizeof(ethertap_packet.ethernet_header) - offset))
    goto Fail;
  l -= (sizeof(ethertap_packet.ethernet_header) - offset);

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
  char *label;
  char devname[1024];
  uint8 hwaddr[6];
  int fd;
  struct edv_t *edv;
  struct iface *ifp;
  struct ifreq ifr;
#if __MACOSX__ || __FreeBSD__
  int mib[] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };
  size_t mibLen;
#endif
  struct stat statbuf;
  int version = 0;
  int ifp_mtu = 0;
  int skfd;

  /* ZWEI NAMEN FUER ZWEI SEITEN, wie bei "attach tun" und "attach kernel":
   * argv[1] ist der Name, den der Linux-Host sieht, das optionale Label der
   * unsere.  Das Label steht hinter der MTU und nicht davor, weil die MTU
   * schon optional ist - zwei aufeinanderfolgende Kann-Argumente waeren
   * nicht zu unterscheiden.  Wer also ein Label will, gibt die MTU mit an.
   *
   * ifname zeigt weiter unten auf devname und traegt dann den Namen, den der
   * Kernel wirklich vergeben hat; deshalb wird das Label hier getrennt
   * gehalten und nicht in ifname geschrieben.
   */
  ifname = argv[1];
  label = (argc > 3) ? argv[3] : NULL;
  if (if_lookup(label ? label : ifname)) {
    printf("Interface %s already exists\n", label ? label : ifname);
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
  } else {
#ifdef	TRY_TUNTAP
    strcpy(devname, ifname);
    if ((fd = tun_alloc(devname)) < 0) {
      printf("%s: %s\n", devname, strerror(errno));
      return -1;
    }
    ifname = devname;
    version = 1;
#else
    printf("%s: %s\n", devname, strerror(errno));
    return -1;
#endif
  }

  if ((skfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    perror("socket()");
    close(fd);
    return -1;
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
#ifdef	linux
  if (ioctl(skfd, SIOCGIFHWADDR, &ifr) != -1) {
    memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);
    goto behind_dummy_hwaddr;
  }
  perror("SIOCGIFHWADDR");
  goto dummy_hwaddr;
#endif
#if __MACOSX__ || __FreeBSD__
  if (sysctl(mib, 6, NULL, &mibLen, NULL, 0) == 0) {
    unsigned char *p, *buf;
    if ((buf = (u_char *) malloc(mibLen))) {
      if (sysctl(mib, 6, buf, &mibLen, NULL, 0) == 0) {
        struct if_msghdr *ifm;
        for (p = buf; p < buf + mibLen; p += ifm->ifm_msglen) {
	  ifm = (struct if_msghdr *) p;
          struct sockaddr_dl *sdl = (struct sockaddr_dl *) (ifm + 1);
          if (ifm->ifm_type != RTM_IFINFO || (ifm->ifm_addrs & RTA_IFP) == 0)
            continue;
          if (sdl->sdl_family != AF_LINK ||
              sdl->sdl_type != IFT_ETHER ||
              sdl->sdl_alen != 6 ||
              sdl->sdl_nlen == 0 ||
              memcmp(sdl->sdl_data, ifname, sdl->sdl_nlen) ||
              ifname[sdl->sdl_nlen] != 0)
            continue;
          memcpy(hwaddr, LLADDR(sdl), 6);
          free(buf);
          goto behind_dummy_hwaddr;
        }
      }
    }
    free(buf);
  } 
  perror("sysctl()");
  hwaddr[0] = 0x74;
  hwaddr[1] = 0x61;
  hwaddr[2] = 0x70;
  hwaddr[3] = 0x0;
  hwaddr[4] = 0x0;
  hwaddr[5] = 0x0;
  goto behind_dummy_hwaddr;
#endif
  /* goto dummy_hwaddr; */
dummy_hwaddr:
  hwaddr[0] = 0xfe;
  hwaddr[1] = 0xfd;
  hwaddr[2] = 0x0;
  hwaddr[3] = 0x0;
  hwaddr[4] = 0x0;
  hwaddr[5] = 0x0;
behind_dummy_hwaddr:

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
  /* Ohne Label bleibt es beim bisherigen Verhalten: ifname zeigt hier auf
   * den Namen, den der Kernel WIRKLICH vergeben hat, nicht auf den Wunsch
   * aus argv[1].
   */
  ifp->name = strdup(label ? label : ifname);
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
  /* as long as feebsd and macosx don't have kernel support for ax25,
   * we like not to confuse our users with ifaces like tap0BPQ which he
   * can't use:
   */
#ifdef	linux
  ethertap_attach_bpq(ifp);
#endif
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

