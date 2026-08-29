/* BPQether on a REAL ethernet interface: AX.25 frames inside ethernet ones,
 * protocol 0x08FF.
 *
 * "attach ethertap" has spoken this for years, but only towards a tap device,
 * which means towards the kernel's own AX.25 stack on the same machine.  This
 * is the other half: hang the node on a segment and let everything on it be a
 * neighbour.  A switch then does what a channel does, one collision domain per
 * VLAN, and a node reaches a whole set of partners without a radio between.
 *
 * THE FRAME, which is the kernel's (drivers/net/hamradio/bpqether.c) and not
 * ours to change:
 *
 *     ethernet header, type 0x08FF
 *     2 octets length, LITTLE endian, and it counts the AX.25 frame PLUS 5
 *     the AX.25 frame, address field first - no KISS byte, no CRC
 *
 * The +5 is a leftover of the DOS BPQ this grew from.  It is not a mistake to
 * be corrected; it is the format, and a receiver that computes anything else
 * talks to nobody.
 *
 * WHY WE BIND ETH_P_ALL AND FILTER IN THE KERNEL, rather than binding
 * ETH_P_BPQ and being done.  Linux delivers in this order
 * (net/core/dev.c, __netif_receive_skb_core):
 *
 *     ptype_all          ETH_P_ALL sockets           <- before everything
 *     vlan_do_receive()  a tagged frame is re-delivered on eth0.<n>
 *     rx_handler         bridge, bonding, macvlan
 *     ptype_base         ETH_P_BPQ sockets           <- only here
 *
 * So a socket bound to ETH_P_BPQ on a BRIDGE PORT can see nothing at all: the
 * bridge's rx_handler consumes the frame two steps earlier.  The same happens
 * when a VLAN device exists for the tag.  Binding ETH_P_ALL puts us in front
 * of both, and SO_ATTACH_FILTER makes the kernel throw the rest away before it
 * costs us anything - which is exactly what BIOCSETF does on the BSD side, so
 * both systems end up with the same shape and the same filter program.
 *
 * OUR OWN FRAMES COME BACK on such a socket, and that has to be dropped or the
 * node talks to itself: PACKET_OUTGOING here, BIOCSSEESENT there.
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <net/if.h>

#ifdef	linux
#include <netpacket/packet.h>
#include <net/ethernet.h>
#include <linux/filter.h>
#include <sys/ioctl.h>
#endif

#if defined __MACOSX__ || defined __FreeBSD__
#include <net/bpf.h>
#include <net/if_dl.h>
#include <net/if_types.h>
#include <net/route.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#endif

#include "global.h"
#include "hpux.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "netuser.h"
#include "trace.h"
#include "bpqether.h"

#ifndef	ETH_P_BPQ
#define	ETH_P_BPQ	0x08ff
#endif

#define BPQ_HDRLEN	14		/* dst, src, type */
#define BPQ_LENLEN	2		/* the little endian length in front */
#define BPQ_EXTRA	5		/* what that length counts on top */
#define BPQ_MTU		256
#define BPQ_MTU_MAX	(1500 - BPQ_LENLEN)

static const uint8 Ether_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

struct bpq_edv {
  int fd;
  char ifname[IFNAMSIZ];		/* the host's name for the port */
  uint8 hwaddr[6];			/* our own MAC on it */
  int vlan;				/* -1: untagged */
  int index;				/* linux: the ifindex to send from */
  unsigned buflen;			/* bsd: what BIOCGBLEN asked for */
  char *buf;				/* bsd: a whole read at a time */
};

/* THE VLAN, AND WHY IT IS A WORD HERE RATHER THAN A SECOND INTERFACE.
 *
 * One could leave it to the system - "eth0.70" on linux, "vlan0 create vlan
 * 70 vlandev en0" on macOS - and attach that.  Then this file would need no
 * line at all.  Against it stands practice (Thomas): a sysop who is not deep
 * in it has to touch TWO places, the system's network configuration and
 * net.rc, and get them to agree.  "attach bpqether eth0 vlan 70" says the
 * whole thing in one line, at the place where the rest of the port is
 * described.  Both ways keep working - naming eth0.70 is still allowed and
 * then simply carries no "vlan" word.
 *
 * A TAG ARRIVES IN TWO SHAPES, and that is the whole difficulty:
 *
 *   in the frame   dst src 81 00 <tci> 08 ff <len> ...   four octets more,
 *                  and everything behind them moves along
 *   pulled out     dst src 08 ff <len> ...               the kernel has put
 *                  the tag in a field of its own, and the frame LOOKS
 *                  untagged.  Only PACKET_AUXDATA still says which VLAN it
 *                  was (linux; on BSD there is no equivalent)
 *
 * So the untagged shape is not proof of an untagged frame.  A port that
 * carries no "vlan" word therefore also has to ask, or it would answer for
 * every VLAN on the wire - and with linkpartners separated by VLAN, which is
 * the point of the exercise, that is precisely the wrong thing.
 */

#define BPQ_VLAN_NONE	(-1)
#define BPQ_TAGLEN	4		/* 81 00 and the two TCI octets */

static void bpqether_learn(struct iface *ifp, const uint8 *ether_src,
			   const uint8 *ax, int len);

/*---------------------------------------------------------------------------*/

/* Which VLAN a received frame belongs to, and where the BPQ part starts.
 *
 * <aux> is what the system told us out of band, or BPQ_VLAN_NONE when it told
 * us nothing.  A tag inside the frame wins over it: it is the frame itself
 * speaking, while the out of band value only describes what the kernel took
 * out - and the two cannot both be there.
 *
 * Returns the VLAN, or BPQ_VLAN_NONE for an untagged frame, and sets *offp to
 * the number of octets the tag added (0 or 4).
 */

static int bpqether_vlan_of(const uint8 *buf, int len, int aux, int *offp)
{
  *offp = 0;
  if (len >= 16 && buf[12] == 0x81 && buf[13] == 0x00) {
    *offp = BPQ_TAGLEN;
    return ((buf[14] & 0x0f) << 8) | buf[15];
  }
  return aux;
}

/*---------------------------------------------------------------------------*/

/* One filter program for both systems.  Classic BPF, and struct sock_filter
 * on Linux has the same four fields in the same order as struct bpf_insn on
 * BSD - so the table is written once with plain numbers and each side only
 * wraps it.
 *
 *     ldh  [12]                  the ethertype
 *     jeq  0x08ff  -> take it
 *     jeq  0x8100  -> tagged, look behind the tag
 *     ldh  [16]
 *     jeq  0x08ff  -> take it
 *     ret  0                     everything else
 *
 * The 802.1Q branch is there for the case where the tag is still IN the
 * frame.  Where the kernel has already pulled it into its own field the
 * ethertype at 12 is 0x08FF anyway and the first test matches.
 */

#define BPF_LDH_ABS	0x28
#define BPF_JEQ_K	0x15
#define BPF_RET_K	0x06

struct bpq_insn {
  unsigned short code;
  unsigned char jt;
  unsigned char jf;
  unsigned int k;
};

static const struct bpq_insn Bpq_filter[] = {
  { BPF_LDH_ABS, 0, 0, 12 },
  { BPF_JEQ_K,   3, 0, ETH_P_BPQ },
  { BPF_JEQ_K,   0, 3, 0x8100 },
  { BPF_LDH_ABS, 0, 0, 16 },
  { BPF_JEQ_K,   0, 1, ETH_P_BPQ },
  { BPF_RET_K,   0, 0, 0xffffffffU },
  { BPF_RET_K,   0, 0, 0 }
};

#define BPQ_FILTER_LEN	(sizeof(Bpq_filter) / sizeof(Bpq_filter[0]))

/*---------------------------------------------------------------------------*/

/* Our own MAC on that port.  Without it we would have to send under somebody
 * else's, and a switch that does port security drops those - besides, the far
 * end learns us from it and that is how it will answer once it may.
 */

static int bpqether_hwaddr(const char *ifname, uint8 *hwaddr)
{

#ifdef	linux
  int s;
  struct ifreq ifr;

  if ((s = socket(AF_INET, SOCK_DGRAM, 0)) < 0) return -1;
  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
  if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
    close(s);
    return -1;
  }
  memcpy(hwaddr, ifr.ifr_hwaddr.sa_data, 6);
  close(s);
  return 0;
#endif

#if defined __MACOSX__ || defined __FreeBSD__
  int mib[6] = { CTL_NET, AF_ROUTE, 0, AF_LINK, NET_RT_IFLIST, 0 };
  size_t len = 0;
  unsigned char *buf, *p;

  if (sysctl(mib, 6, NULL, &len, NULL, 0) != 0) return -1;
  if (!(buf = (unsigned char *) malloc(len))) return -1;
  if (sysctl(mib, 6, buf, &len, NULL, 0) != 0) {
    free(buf);
    return -1;
  }
  for (p = buf; p < buf + len; ) {
    struct if_msghdr *ifm = (struct if_msghdr *) p;
    struct sockaddr_dl *sdl = (struct sockaddr_dl *) (ifm + 1);

    p += ifm->ifm_msglen;
    if (ifm->ifm_type != RTM_IFINFO || !(ifm->ifm_addrs & RTA_IFP)) continue;
    if (sdl->sdl_family != AF_LINK || sdl->sdl_alen != 6) continue;
    if (sdl->sdl_nlen == 0 || strncmp(sdl->sdl_data, ifname, sdl->sdl_nlen)
	|| ifname[sdl->sdl_nlen]) continue;
    memcpy(hwaddr, LLADDR(sdl), 6);
    free(buf);
    return 0;
  }
  free(buf);
  return -1;
#endif
}

/*---------------------------------------------------------------------------*/

#ifdef	linux

static int bpqether_open(struct bpq_edv *edv, int promisc)
{

  int fd;
  struct ifreq ifr;
  struct sock_fprog prog;
  struct sockaddr_ll sll;

  if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
    perror("bpqether: socket(AF_PACKET)");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, edv->ifname, IFNAMSIZ - 1);
  if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) {
    perror("bpqether: SIOCGIFINDEX");
    close(fd);
    return -1;
  }
  edv->index = ifr.ifr_ifindex;

  /* Attach the filter BEFORE the bind, so that not one unwanted frame is
   * queued in the window between the two.
   */
  prog.len = BPQ_FILTER_LEN;
  prog.filter = (struct sock_filter *) Bpq_filter;
  if (setsockopt(fd, SOL_SOCKET, SO_ATTACH_FILTER, &prog, sizeof(prog)) < 0)
    perror("bpqether: SO_ATTACH_FILTER");

  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_ALL);
  sll.sll_ifindex = edv->index;
  if (bind(fd, (struct sockaddr *) &sll, sizeof(sll)) < 0) {
    perror("bpqether: bind");
    close(fd);
    return -1;
  }

  /* Ask for the tag the kernel took out of the frame.  Without it a tagged
   * frame that arrived through hardware offload looks untagged, and a port
   * could not tell VLAN 70 from VLAN 80 - see the note beside bpq_edv.
   */
  {
    int on = 1;

    if (setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &on, sizeof(on)) < 0)
      perror("bpqether: PACKET_AUXDATA");
  }

  if (promisc) {
    struct packet_mreq mr;

    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = edv->index;
    mr.mr_type = PACKET_MR_PROMISC;
    if (setsockopt(fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr,
		   sizeof(mr)) < 0)
      perror("bpqether: PACKET_MR_PROMISC");
  }
  return fd;
}

/*---------------------------------------------------------------------------*/

static void bpqether_recv(void *argp)
{

  int aux = BPQ_VLAN_NONE;
  int l;
  int off;
  struct bpq_edv *edv;
  struct cmsghdr *cm;
  struct iface *ifp;
  struct iovec iov;
  struct mbuf *bp;
  struct msghdr msg;
  struct sockaddr_ll from;
  uint8 buf[BPQ_HDRLEN + BPQ_TAGLEN + BPQ_LENLEN + BPQ_MTU_MAX];
  union {
    char buf[CMSG_SPACE(sizeof(struct tpacket_auxdata))];
    struct cmsghdr align;
  } control;
  unsigned len;

  ifp = (struct iface *) argp;
  edv = (struct bpq_edv *) ifp->edv;

  memset(&msg, 0, sizeof(msg));
  iov.iov_base = buf;
  iov.iov_len = sizeof(buf);
  msg.msg_name = &from;
  msg.msg_namelen = sizeof(from);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control.buf;
  msg.msg_controllen = sizeof(control.buf);

  l = recvmsg(edv->fd, &msg, 0);
  if (l <= BPQ_HDRLEN + BPQ_LENLEN) goto Fail;

  /* Our own frames come back here.  Without this the node answers itself,
   * and every link it opens is with a station that shares its callsign.
   */
  if (from.sll_pkttype == PACKET_OUTGOING) return;

  for (cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm))
    if (cm->cmsg_level == SOL_PACKET && cm->cmsg_type == PACKET_AUXDATA) {
      struct tpacket_auxdata aux_d;

      memcpy(&aux_d, CMSG_DATA(cm), sizeof(aux_d));
      if (aux_d.tp_status & TP_STATUS_VLAN_VALID)
	aux = aux_d.tp_vlan_tci & 0x0fff;
    }

  if (bpqether_vlan_of(buf, l, aux, &off) != edv->vlan) return;

  len = buf[BPQ_HDRLEN + off] + buf[BPQ_HDRLEN + off + 1] * 256;
  if (len < BPQ_EXTRA || len - BPQ_EXTRA !=
      (unsigned) (l - BPQ_HDRLEN - off - BPQ_LENLEN)) goto Fail;

  bp = qdata(buf + BPQ_HDRLEN + off + BPQ_LENLEN, len - BPQ_EXTRA);
  net_route(ifp, &bp);
  /* AFTER, not before - see bpqether_learn(). */
  bpqether_learn(ifp, buf + 6, buf + BPQ_HDRLEN + off + BPQ_LENLEN,
		 (int) (len - BPQ_EXTRA));
  return;

Fail:
  ifp->crcerrors++;
}

/*---------------------------------------------------------------------------*/

static int bpqether_write(struct bpq_edv *edv, const uint8 *frame, int len)
{

  struct sockaddr_ll sll;

  memset(&sll, 0, sizeof(sll));
  sll.sll_family = AF_PACKET;
  sll.sll_protocol = htons(ETH_P_BPQ);
  sll.sll_ifindex = edv->index;
  sll.sll_halen = 6;
  memcpy(sll.sll_addr, frame, 6);       /* the destination we just wrote */
  return sendto(edv->fd, frame, (size_t) len, 0, (struct sockaddr *) &sll,
		sizeof(sll));
}

#endif	/* linux */

/*---------------------------------------------------------------------------*/

#if defined __MACOSX__ || defined __FreeBSD__

static int bpqether_open(struct bpq_edv *edv, int promisc)
{

  char path[32];
  int fd = -1;
  int i;
  int on = 1;
  struct bpf_program prog;
  struct ifreq ifr;
  unsigned len;

  /* /dev/bpf may be there as a cloner; where it is not, walk the numbered
   * ones.  Whoever holds one has it exclusively, so a busy node is a normal
   * finding and not an error.
   */
  if ((fd = open("/dev/bpf", O_RDWR)) < 0)
    for (i = 0; i < 256 && fd < 0; i++) {
      snprintf(path, sizeof(path), "/dev/bpf%d", i);
      fd = open(path, O_RDWR);
    }
  if (fd < 0) {
    perror("bpqether: /dev/bpf");
    return -1;
  }

  memset(&ifr, 0, sizeof(ifr));
  strncpy(ifr.ifr_name, edv->ifname, IFNAMSIZ - 1);
  if (ioctl(fd, BIOCSETIF, &ifr) < 0) {
    perror("bpqether: BIOCSETIF");
    close(fd);
    return -1;
  }

  /* Hand each frame over as it arrives.  Without this bpf holds them until
   * its buffer is full, which on a quiet segment is minutes.
   */
  if (ioctl(fd, BIOCIMMEDIATE, &on) < 0) perror("bpqether: BIOCIMMEDIATE");

  /* We write whole ethernet headers ourselves, source address included. */
  if (ioctl(fd, BIOCSHDRCMPLT, &on) < 0) perror("bpqether: BIOCSHDRCMPLT");

  /* And we do not want to read them back - see the note at the top. */
  {
    int off = 0;

    if (ioctl(fd, BIOCSSEESENT, &off) < 0) perror("bpqether: BIOCSSEESENT");
  }

  if (promisc && ioctl(fd, BIOCPROMISC, NULL) < 0)
    perror("bpqether: BIOCPROMISC");

  prog.bf_len = BPQ_FILTER_LEN;
  prog.bf_insns = (struct bpf_insn *) Bpq_filter;
  if (ioctl(fd, BIOCSETF, &prog) < 0) perror("bpqether: BIOCSETF");

  /* bpf reads in whole buffers of its own size, never less - so ask what it
   * is rather than guessing, and keep one.
   */
  if (ioctl(fd, BIOCGBLEN, &len) < 0) len = 32768;
  if (!(edv->buf = (char *) malloc(len))) {
    close(fd);
    return -1;
  }
  edv->buflen = len;
  return fd;
}

/*---------------------------------------------------------------------------*/

static void bpqether_recv(void *argp)
{

  int l;
  int off;
  char *p, *end;
  struct bpq_edv *edv;
  struct iface *ifp;
  struct mbuf *bp;

  ifp = (struct iface *) argp;
  edv = (struct bpq_edv *) ifp->edv;

  if ((l = read(edv->fd, edv->buf, edv->buflen)) <= 0) return;

  /* One read holds SEVERAL frames, each behind its own bpf_hdr and each
   * aligned onwards with BPF_WORDALIGN.  Taking only the first would lose
   * every frame that arrived while we were not looking.
   */
  for (p = edv->buf, end = edv->buf + l; p + sizeof(struct bpf_hdr) <= end; ) {
    struct bpf_hdr *hdr = (struct bpf_hdr *) p;
    uint8 *frame = (uint8 *) p + hdr->bh_hdrlen;
    unsigned len;

    if (p + hdr->bh_hdrlen + hdr->bh_caplen > end) break;
    if (hdr->bh_caplen < BPQ_HDRLEN + BPQ_LENLEN ||
	hdr->bh_caplen != hdr->bh_datalen)
      goto next;                        /* truncated: not ours to guess at */

    /* No PACKET_AUXDATA here: bpf hands over what is on the wire and says
     * nothing beside it.  A tag the driver has already stripped is therefore
     * invisible, and such a frame reads as untagged.  Where that matters, the
     * system's own vlan device is the way - "vlan0 create vlan 70 vlandev
     * en0" - and this port is then attached to THAT, without a "vlan" word.
     */
    if (bpqether_vlan_of(frame, (int) hdr->bh_caplen, BPQ_VLAN_NONE, &off)
	!= edv->vlan)
      goto next;

    len = frame[BPQ_HDRLEN + off] + frame[BPQ_HDRLEN + off + 1] * 256;
    if (len < BPQ_EXTRA ||
	len - BPQ_EXTRA != hdr->bh_caplen - BPQ_HDRLEN - off - BPQ_LENLEN) {
      ifp->crcerrors++;
      goto next;
    }
    bp = qdata(frame + BPQ_HDRLEN + off + BPQ_LENLEN, len - BPQ_EXTRA);
    net_route(ifp, &bp);
    /* AFTER, not before - see bpqether_learn(). */
    bpqether_learn(ifp, frame + 6, frame + BPQ_HDRLEN + off + BPQ_LENLEN,
		   (int) (len - BPQ_EXTRA));
next:
    p += BPF_WORDALIGN(hdr->bh_hdrlen + hdr->bh_caplen);
  }
}

/*---------------------------------------------------------------------------*/

static int bpqether_write(struct bpq_edv *edv, const uint8 *frame, int len)
{
  return write(edv->fd, frame, (size_t) len);
}

#endif	/* __MACOSX__ || __FreeBSD__ */

/*---------------------------------------------------------------------------*/

/* Anywhere else: say so at attach time rather than failing to link.  A raw
 * ethernet socket is the one thing here that has no portable spelling.
 */

#if !defined linux && !defined __MACOSX__ && !defined __FreeBSD__

#define BPQETHER_UNSUPPORTED 1

static int bpqether_open(struct bpq_edv *edv, int promisc)
{
  (void) edv;
  (void) promisc;
  return -1;
}

static void bpqether_recv(void *argp) { (void) argp; }

static int bpqether_write(struct bpq_edv *edv, const uint8 *frame, int len)
{
  (void) edv;
  (void) frame;
  (void) len;
  return -1;
}

#endif

/*---------------------------------------------------------------------------*/

/* WHOSE CARD PUT THIS FRAME ON THE WIRE.  Not the source callsign as such:
 * if the frame came through digipeaters, the machine we are looking at is the
 * LAST one that has already repeated it.  Anything in front of that is
 * somebody else's problem and somebody else's card.
 *
 * Returns NULL when the header does not parse, which is the caller's cue to
 * learn nothing rather than to guess.
 */

static const uint8 *bpqether_sender(const uint8 *ax, int len)
{
  const uint8 *last = 0;
  int n;
  int off;

  /* Walked by hand rather than with ntohax25(), which takes an mbuf and eats
   * the header as it goes.  Here the frame has to stay whole - it is on its
   * way upstairs.
   */
  for (n = 0, off = 0; off + AXALEN <= len; n++, off += AXALEN) {
    if (n >= 2 && (ax[off + ALEN] & REPEATED)) last = ax + off;
    if (ax[off + ALEN] & E) {
      n++;
      break;
    }
  }
  if (n < 2) return 0;                  /* not even a destination and source */
  return last ? last : ax + AXALEN;
}

/*---------------------------------------------------------------------------*/

/* Learn where a station sits, from a frame that arrived.
 *
 * WHAT WE LEARN FROM: everything.  Frames to us, obviously - we want to
 * answer to the one card.  But broadcasts as well, and that is the point:
 * NODES and QST are how a neighbour announces himself before anybody has
 * spoken to him, so without them the first connect always goes to the whole
 * segment.  It is the same reason the IP side learns from an ARP it did not
 * ask for.
 *
 * CALLED AFTER THE FRAME HAS GONE UPSTAIRS, and the order is not a detail.
 * axroute_add() runs up there, and it is what MOVES a route onto this port.
 * Asking beforehand meant asking a route that still said "he is on axip" -
 * so on the first frame after a station moved back to the ethernet we
 * learned nothing and answered him by broadcast.  Measured: the route
 * followed him, the card did not.  The frame itself is still flat in the
 * caller's buffer at that point; net_route() got a copy.
 *
 * Nothing depends on the result - a header we cannot read simply teaches us
 * nothing.
 */

static void bpqether_learn(struct iface *ifp, const uint8 *ether_src,
			   const uint8 *ax, int len)
{
  const uint8 *call;
  int off;

  if (!(call = bpqether_sender(ax, len))) return;

  /* Where the address field ends the control octet begins, and the PID
   * behind it.  Both are needed for the one question a new entry has to
   * answer - see axroute_learnable().
   */
  for (off = 0; off + AXALEN <= len; off += AXALEN)
    if (ax[off + ALEN] & E) {
      off += AXALEN;
      break;
    }
  if (off >= len) return;

  axroute_mac_learn(ifp, call, ether_src,
		    axroute_learnable(ax[off],
				      off + 1 < len ? ax[off + 1] : -1));
}

/*---------------------------------------------------------------------------*/

/* Where a frame goes on the wire.
 *
 * AN AX.25 BROADCAST IS AN ETHERNET BROADCAST.  QST and NODES are addressed
 * to everybody by definition - that is what Ax25multi[] holds, and the IP
 * side already sends its ARP requests to QST for the same reason.  A station
 * that would only hear such a frame if it were addressed to its card has
 * misunderstood what a broadcast is.
 *
 * Otherwise: the card we learned for that station, or the broadcast when we
 * have none.  The broadcast always works; it merely asks the whole segment to
 * look at something meant for one machine.
 *
 * The DESTINATION decides, not the next digipeater - on a BPQether segment
 * everybody hears everybody, so there is no such thing as a hop towards him.
 */

static const uint8 *bpqether_target(struct iface *ifp, const uint8 *ax, int len)
{
  const uint8 *mac;
  uint8 (*mpp)[AXALEN];

  if (len < AXALEN) return Ether_bcast;
  for (mpp = Ax25multi; (*mpp)[0]; mpp++)
    if (addreq(ax, *mpp)) return Ether_bcast;
  if ((mac = axroute_mac_get(ifp, ax)) != 0) return mac;
  return Ether_bcast;
}

/*---------------------------------------------------------------------------*/

static int bpqether_send(struct iface *ifp, struct mbuf **bpp)
{

  int l;
  int off;
  struct bpq_edv *edv;
  uint8 frame[BPQ_HDRLEN + BPQ_TAGLEN + BPQ_LENLEN + BPQ_MTU_MAX];
  const uint8 *dest;

  edv = (struct bpq_edv *) ifp->edv;
  dump(ifp, IF_TRACE_OUT, *bpp);
  ifp->rawsndcnt++;
  ifp->lastsent = secclock();
  if (ifp->trace & IF_TRACE_RAW) raw_dump(ifp, -1, *bpp);

  /* Laid out flat first, then asked where it goes: the destination sits in
   * the first seven octets, and in an mbuf CHAIN those need not all be in the
   * first buffer.
   */
  off = edv->vlan == BPQ_VLAN_NONE ? 0 : BPQ_TAGLEN;

  l = pullup(bpp, frame + BPQ_HDRLEN + off + BPQ_LENLEN, BPQ_MTU_MAX);
  if (l <= 0 || *bpp) {                 /* longer than we may carry */
    free_p(bpp);
    return -1;
  }
  dest = bpqether_target(ifp, frame + BPQ_HDRLEN + off + BPQ_LENLEN, l);
  memcpy(frame, dest, 6);
  memcpy(frame + 6, edv->hwaddr, 6);
  if (off) {
    /* 802.1Q, written out rather than left to the system: priority 0, no
     * drop eligible, and the VLAN in the low twelve bits.
     */
    frame[12] = 0x81;
    frame[13] = 0x00;
    frame[14] = (edv->vlan >> 8) & 0x0f;
    frame[15] = edv->vlan & 0xff;
  }
  frame[12 + off] = (ETH_P_BPQ >> 8) & 0xff;
  frame[13 + off] = ETH_P_BPQ & 0xff;
  frame[BPQ_HDRLEN + off]     = (l + BPQ_EXTRA) % 256;
  frame[BPQ_HDRLEN + off + 1] = (l + BPQ_EXTRA) / 256;

  return bpqether_write(edv, frame, BPQ_HDRLEN + off + BPQ_LENLEN + l);
}

/*---------------------------------------------------------------------------*/

static void bpqether_show(struct iface *ifp)
{

  struct bpq_edv *edv = (struct bpq_edv *) ifp->edv;

  printf("           bpqether on %s, our mac %02x:%02x:%02x:%02x:%02x:%02x",
	 edv->ifname, edv->hwaddr[0], edv->hwaddr[1], edv->hwaddr[2],
	 edv->hwaddr[3], edv->hwaddr[4], edv->hwaddr[5]);
  if (edv->vlan != BPQ_VLAN_NONE)
    printf(", vlan %d", edv->vlan);
  printf("\n");
}

/*---------------------------------------------------------------------------*/

int bpqether_attach(int argc, char *argv[], void *p)
{

  int mtu = BPQ_MTU;
  int promisc = 1;
  int vlan = BPQ_VLAN_NONE;
  int i;
  char *label = 0;
  struct bpq_edv *edv;
  struct iface *ifp;

  (void) p;

#ifdef	BPQETHER_UNSUPPORTED
  printf("attach bpqether: this system has no raw ethernet socket we know "
	 "of - linux and BSD only\n");
  return -1;
#endif

  /* "attach bpqether <iface> [<label>] [<mtu>] [nopromisc]".  The words after
   * the interface are told apart by what they look like, the way "attach
   * ethertap" does it: a number is the mtu, "nopromisc" is itself, and
   * anything else is our own name for the port.
   */
  for (i = 2; i < argc; i++) {
    if (!strcmp(argv[i], "nopromisc")) { promisc = 0; continue; }
    if (!strcmp(argv[i], "vlan")) {
      /* The number belongs to the word before it and must not be mistaken
       * for the mtu, so it is taken here rather than left to the loop.
       */
      if (++i >= argc) {
	printf("attach bpqether: \"vlan\" wants a number\n");
	return -1;
      }
      vlan = atoi(argv[i]);
      if (vlan < 1 || vlan > 4094) {
	printf("attach bpqether: vlan %s is outside 1..4094 (0 and 4095 are "
	       "reserved)\n", argv[i]);
	return -1;
      }
      continue;
    }
    if (argv[i][0] >= '0' && argv[i][0] <= '9') { mtu = atoi(argv[i]); continue; }
    if (!label) { label = argv[i]; continue; }
    printf("attach bpqether: unexpected \"%s\"\n", argv[i]);
    return -1;
  }
  if (mtu < 64 || mtu > BPQ_MTU_MAX) {
    printf("attach bpqether: mtu %d is outside 64..%d\n", mtu, BPQ_MTU_MAX);
    return -1;
  }
  if (if_lookup(label ? label : argv[1]) != NULL) {
    printf("Interface %s already exists\n", label ? label : argv[1]);
    return -1;
  }

  edv = (struct bpq_edv *) callocw(1, sizeof(struct bpq_edv));
  strncpy(edv->ifname, argv[1], sizeof(edv->ifname) - 1);
  edv->vlan = vlan;

  /* Ask the port for its own address before opening anything: a name that no
   * interface answers to is worth saying plainly, and it is the commonest
   * thing to get wrong in that line.
   */
  if (bpqether_hwaddr(edv->ifname, edv->hwaddr)) {
    printf("attach bpqether: %s has no ethernet address - is that the name "
	   "the system uses?\n", edv->ifname);
    free(edv);
    return -1;
  }

  if ((edv->fd = bpqether_open(edv, promisc)) < 0) {
    free(edv->buf);
    free(edv);
    return -1;
  }

  ifp = (struct iface *) callocw(1, sizeof(struct iface));
  ifp->name = strdup(label ? label : argv[1]);
  ifp->addr = Ip_addr;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  ifp->mtu = mtu;
  setencap(ifp, "AX25UI");
  ifp->edv = edv;
  ifp->send = axui_send;
  ifp->raw = bpqether_send;
  ifp->show = bpqether_show;
  on_read(edv->fd, bpqether_recv, (void *) ifp);
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}
