/* @(#) $Id: tun.c,v 1.6 2006/02/12 17:49:57 dl9sau Exp $ */

/*
   Interface to FreeBSD's tun device - Olaf Erb, dc1ik 960728
   parts and idea taken from FreeBSD's ppp implementation
 */

/* Three ways to the same interface, and the command is the same everywhere:
 * FreeBSD opens /dev/tunN, macOS has no such device and uses a kernel control
 * socket (utun), Linux opens /dev/net/tun and asks for the mode it wants.
 * What differs beyond opening is only what sits in front of a packet.
 */

#if defined __FreeBSD__ || defined __MACOSX__ || defined __APPLE__ \
    || defined linux

#include "global.h"
#undef  hiword
#undef  loword
#undef  hibyte
#undef  lobyte

#include <sys/types.h>

#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/select.h>

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#ifdef	__FreeBSD__
#include <net/if_tun.h>
#endif
#ifndef	linux
#include <net/route.h>          /* BSD only, and nothing here needs it */
#endif
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if defined __MACOSX__ || defined __APPLE__
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>
#include <string.h>
#endif

#ifdef	linux
#include <linux/if_tun.h>
#include <string.h>
#endif

#include "strerror.h"

#include "mbuf.h"
#include "iface.h"
#include "netuser.h"
#include "trace.h"
#include "hpux.h"

#define MAX_FRAME       2048
#define MAX_TUN         256

#if defined __MACOSX__ || defined __APPLE__

/* macOS has no tunnel character device.  It has had utun since 10.6.4, which
 * is the same thing reached differently: a kernel control socket rather than
 * /dev/tunN.  The tuntaposx kext that provided the device has been unmaintained
 * since 2015 and does not load at all on Big Sur or on Apple silicon, so the
 * old path here cannot work on any Mac somebody would run this on today.
 *
 * Nothing exotic is needed for it - no entitlement, no NetworkExtension.  Root
 * is enough, and creating an interface needs that anyway.  (NetworkExtension is
 * what a TAP device would need, and macOS has none: only tun.)
 *
 * Every read and write carries one packet with a four byte address family in
 * front of it, in network byte order.  That is the same shape the tun_packet
 * struct below was reaching for.
 */

static int utun_open(char *ifname, size_t ifnamelen)
{
  int fd;
  socklen_t namelen;
  struct ctl_info ci;
  struct sockaddr_ctl sc;
  unsigned unit;

  if ((fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL)) < 0) {
    perror("utun: socket");
    return -1;
  }
  memset(&ci, 0, sizeof(ci));
  strncpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof(ci.ctl_name) - 1);
  if (ioctl(fd, CTLIOCGINFO, &ci) < 0) {
    perror("utun: CTLIOCGINFO");
    close(fd);
    return -1;
  }

  /* sc_unit is the interface number plus one; zero means "any free one",
   * which is what we want, but older systems want to be asked one at a
   * time - so ask for any first and walk if that is refused.
   */
  for (unit = 0; unit <= MAX_TUN; unit++) {
    memset(&sc, 0, sizeof(sc));
    sc.sc_len = sizeof(sc);
    sc.sc_family = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_id = ci.ctl_id;
    sc.sc_unit = unit;          /* 0: let the kernel choose */
    if (connect(fd, (struct sockaddr *) &sc, sizeof(sc)) == 0)
      break;
    if (unit == 0 && errno != EBUSY && errno != EADDRINUSE) {
      perror("utun: connect");
      close(fd);
      return -1;
    }
  }
  if (unit > MAX_TUN) {
    fprintf(stderr, "utun: no free unit\n");
    close(fd);
    return -1;
  }

  /* Which one did we get?  The kernel knows, and the address has to be put
   * on it by name.
   */
  namelen = (socklen_t) ifnamelen;
  if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME,
		 ifname, &namelen) < 0) {
    perror("utun: UTUN_OPT_IFNAME");
    close(fd);
    return -1;
  }
  return fd;
}

#endif /* __MACOSX__ || __APPLE__ */

#ifdef	linux

/* Linux has one device for both kinds and is told which one it is to be.
 *
 * IFF_TUN is the point of this driver: IP with no ethernet header in front of
 * it.  "attach ethertap" opens the same /dev/net/tun and asks for IFF_TAP,
 * because what it carries is BPQether - AX.25 inside ethernet frames - and
 * that needs the header.  For plain IP the header is nothing but ballast, and
 * a route through it would have to be given a MAC address that means nothing.
 *
 * IFF_NO_PI drops the two flag words the kernel would otherwise put in front
 * of every packet.  Without it every read and write would have to step over
 * them, which is exactly the sort of thing the other two backends already do
 * differently enough.
 *
 * The label is asked for as the device name, the way "attach ethertap" does
 * it: then "attach tun tun0 1500" is followed by "ifconfig tun0" on the shell
 * and there is nothing to look up.  Where the kernel refuses the name - taken
 * already, or too long - it picks the next free tunN and says which, so the
 * attach still works.  (macOS cannot be asked at all; utun numbers itself.)
 */

static int tun_open_linux(const char *want, char *ifname, size_t ifnamelen)
{
  int fd;
  struct ifreq ifr;

  if ((fd = open("/dev/net/tun", O_RDWR)) < 0) {
    perror("tun: /dev/net/tun");
    return -1;
  }
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (want != NULL && *want != '\0' && strlen(want) < IFNAMSIZ)
    strncpy(ifr.ifr_name, want, IFNAMSIZ - 1);
  if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
    if (ifr.ifr_name[0] == '\0') {
      perror("tun: TUNSETIFF");
      close(fd);
      return -1;
    }
    /* A name was asked for, and it is the only thing that can have been
     * refused - taken already, or not a name the kernel will take.  Ask again
     * without one rather than give up on the interface.
     */
    perror("tun: TUNSETIFF with that name, taking any");
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (ioctl(fd, TUNSETIFF, (void *) &ifr) < 0) {
      perror("tun: TUNSETIFF");
      close(fd);
      return -1;
    }
  }
  strncpy(ifname, ifr.ifr_name, ifnamelen - 1);
  ifname[ifnamelen - 1] = '\0';
  return fd;
}

#endif /* linux */

struct edv_t {
  int fd;
};

struct tun_packet {
  char data[MAX_FRAME];
  struct sockaddr addr;
};

static char *IfDevName;
#ifndef	linux
static int IfIndex;
static struct ifaliasreq ifra;  /* BSD; set up here and never read */
#endif
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

#if defined __MACOSX__ || defined __APPLE__
  {
    /* One write is one packet, with the address family in front of it. */
    struct iovec iov[2];
    uint32 af = htonl(AF_INET);

    iov[0].iov_base = &af;
    iov[0].iov_len = sizeof(af);
    iov[1].iov_base = tun_packet.data;
    iov[1].iov_len = (size_t) l;
    if (writev(edv->fd, iov, 2) < 0)
      return -1;
  }
#elif defined linux
  /* Nothing in front of it: IFF_NO_PI, and IFF_TUN has no header of its own.
   * What goes out is the IP datagram and nothing else.
   */
  if (write(edv->fd, tun_packet.data, (size_t) l) < 0)
    return -1;
#else
  tun_packet.addr.sa_family = AF_INET;

  write(edv->fd, &tun_packet, l + sizeof(tun_packet.addr));
#endif

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
#if defined __MACOSX__ || defined __APPLE__
  {
    /* ... and one read is one packet, with the same four bytes in front.
     * Anything that is not IPv4 is not ours to route here.
     */
    struct iovec iov[2];
    uint32 af;

    iov[0].iov_base = &af;
    iov[0].iov_len = sizeof(af);
    iov[1].iov_base = tun_packet.data;
    iov[1].iov_len = sizeof(tun_packet.data);
    l = (int) readv(edv->fd, iov, 2);
    if (l <= (int) sizeof(af))
      goto Fail;
    l -= (int) sizeof(af);
    if (ntohl(af) != AF_INET)
      return;
  }
#elif defined linux
  l = (int) read(edv->fd, tun_packet.data, sizeof(tun_packet.data));
  if (l <= 0)
    goto Fail;
#else
  l = read(edv->fd, &tun_packet, sizeof(tun_packet));
  if (l <= 0)
    goto Fail;
#endif

  bp = qdata(tun_packet.data, l);
  net_route(ifp, &bp);
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

#ifndef	linux                   /* counts AF_LINK entries, which Linux has not */

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

#endif /* !linux */

/*---------------------------------------------------------------------------*/

int tun_attach(int argc, char *argv[], void *p)
{

  char devname[14];             /* sufficient room for "/dev/tun65535" */
  char ifname[IFNAMSIZ];
  char *ifnamew;
  /* ZWEI NAMEN FUER ZWEI SEITEN.  argv[1] ist der, um den wir den Kernel
   * bitten - er steht danach in "ip link" und sagt dem Linux-Host, wohin es
   * geht.  Das optionale Label ist unser eigener, und der sagt das
   * Gegenteil: von hier aus geht es zum Host.  Auf db0fhn heisst das Geraet
   * linuxseitig "ax25", weil dahinter die AX.25-Welt liegt - bei uns waere
   * derselbe Name irrefuehrend, denn wir SIND diese Welt.
   *
   * "attach kernel" trennt die beiden seit jeher (krnlif.c), hier fehlte es.
   */
  char *label;
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
  label = (argc > 3) ? argv[3] : ifnamew;

  if (if_lookup(label) != NULL) {
    printf("Interface %s already exists\n", label);
    return -1;
  }

#if defined __MACOSX__ || defined __APPLE__
  (void) devname;
  (void) enoentcount;
  if ((fd = utun_open(ifname, sizeof(ifname))) < 0)
    return -1;
#elif defined linux
  (void) devname;
  (void) enoentcount;
  if ((fd = tun_open_linux(ifnamew, ifname, sizeof(ifname))) < 0)
    return -1;
#else
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
#endif

#ifndef	linux
  bzero((char *) &ifra, sizeof(ifra));
#endif
  bzero((char *) &ifrq, sizeof(ifrq));

  strncpy(ifrq.ifr_name, ifname, IFNAMSIZ);
#ifndef	linux
  strncpy(ifra.ifra_name, ifname, IFNAMSIZ);
#endif

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

#if defined linux || defined __MACOSX__ || defined __APPLE__
  /* The same length on both sides of the device, which is what FreeBSD has
   * always done here through TUNSIFINFO.  Not a necessity - the kernel's mtu
   * only bounds what it hands US, and anything longer than our own ports take
   * gets fragmented on the way out - but the symmetric one is the answer that
   * needs no explaining.  Whoever wants them different says so on the unix
   * side afterwards, where the address has to be set anyway.
   */
  ifrq.ifr_mtu = ifmtu;
  if (ioctl(s, SIOCSIFMTU, &ifrq) < 0)
    perror("SIOCSIFMTU");
#endif

#if defined __MACOSX__ || defined __APPLE__ || defined linux
  IfDevName = ifname;           /* the kernel told us which one we got */
#else
  IfDevName = devname + 5;
#endif
#if defined __MACOSX__ || defined __APPLE__ || defined linux
  /* Not asked for here.  GetIfIndex() counts AF_LINK entries out of
   * SIOCGIFCONF, which a freshly made utun with no address yet does not
   * appear among - and its buffer holds 32 interfaces where this machine has
   * twenty before we add one.  It would abort the attach for nothing: the
   * index it computes is stored in IfIndex and never read, by anything.  The
   * name, which is what is actually needed, came from UTUN_OPT_IFNAME and is
   * not a guess.
   *
   * On Linux the question does not arise at all - there is no AF_LINK, and
   * TUNSETIFF hands back the name for the same reason.
   */
#else
  if (GetIfIndex(IfDevName) < 0) {
    fprintf(stderr, "can't find ifindex.\n");
    close(s);
    return -1;
  }
#endif
  printf("Using interface: %s\r\n", IfDevName);
  close(s);

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(label);
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
