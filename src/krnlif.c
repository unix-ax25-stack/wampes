/* @(#) $Id: krnlif.c,v 1.12 2002/10/19 12:16:36 dl9sau Exp $ */

#if defined linux

/*****************************************************************************/

/*
 *      krnlif.c  -- directly attach a linux kernel interface.
 *
 *      Copyright (C) 1996  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * What does it?
 *  This module allows WAMPES to directly attach to a Linux Kernel
 *  AX.25 driver.
 * Why?
 *  Many drivers for Packet Radio Hardware are nowadays only provided
 *  as Linux Kernel network drivers. Character KISS drivers are dying out,
 *  simply because the network driver interface is much cleaner, simpler
 *  and faster than the complicated line discipline stuff needed by a
 *  character driver.
 *  To use such an interface in Wampes, one could use the net2kiss program
 *  from the ax25 utilities, that would convert such interface packets
 *  to a KISS data stream on a pseudo tty. Wampes could then attach the
 *  respective slave tty. This is complicated to set up, and involves
 *  some overhead.
 *  This module now allows Wampes to directly attach such Linux Kernel
 *  interfaces.
 * Example:
 *  To set up Wampes with a Baycom SER12 modem, the following steps are
 *  necessary.
 *  In a shell:
 *    insmod hdlcdrv.o
 *    insmod baycom.o modem=1 iobase=0x3f8 irq=4 options=1
 *  From within Wampes (or cnet):
 *    attach kernel bc0
 * Note:
 *  the attach command automatically brings the interface into the
 *  running and promiscious state. The MTU is taken from the Linux
 *  interface settings, but may be changed. attach remembers the interface
 *  state and restores it at detach, i.e. if you quit wampes.
 *  AX.25 kernel interfaces may be loaded into memory even if Kernel
 *  AX.25 is disabled.
 *
 * This module is Linux specific. SOCK_PACKET is the Linux way to get
 * packets at the raw interface level.
 *
 * -D USE_OBSOLETE_SOCK_PACKET because of incompatibilties with kernel 2.2.x
 *    by thomas <dl9sau>
 */

#include <linux/version.h>
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))
#endif
#ifndef LINUX_VERSION_CODE
#define	LINUX_VERSION_CODE KERNEL_VERSION(2,0,0)-1
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,1,0)
#define USE_OBSOLETE_SOCK_PACKET 1
#endif

#include <sys/types.h>

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 91)
#include <sys/socket.h>
#endif

#include <linux/socket.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/in.h>
#ifndef	USE_OBSOLETE_SOCK_PACKET
#include <features.h>    /* for the glibc version number */
#if __GLIBC__ >= 2 && __GLIBC_MINOR >= 1
#include <netpacket/packet.h>
#include <net/ethernet.h>     /* the L2 protocols */
#else
#include <asm/types.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>   /* The L2 protocols */
#endif
#endif
#include <unistd.h>
#include <fcntl.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "proc.h"
#include "iface.h"
#include "netuser.h"
#include "commands.h"
#include "devparam.h"
#include "trace.h"
#include "hpux.h"
#include "ax25.h"

#define KRNLIF_MAX 16

/* kernel interface control block */
struct krnlif {
	struct iface *iface;

	int fd;                 /* File descriptor */

	struct mbuf *sndq;      /* Transmit queue */

	short oldflags;         /* used to restore the interrupt flags */
#ifndef	USE_OBSOLETE_SOCK_PACKET
	int ifindex;	        /* interface index (ifr_ifru.ifru_ivalue) */
#endif
	int proto;              /* protocol to listen for */
	int promisc;            /* set interface to promiscious mode */

	long rxpkts;            /* receive packets */
	long txpkts;            /* transmit packets */
	long rxchar;            /* Received characters */
	long txchar;            /* Transmitted characters */
};

static struct krnlif KrnlIf[KRNLIF_MAX];

/*---------------------------------------------------------------------------*/

static int krnlif_up(struct krnlif *ki)
{
	struct ifreq ifr;
#ifdef USE_OBSOLETE_SOCK_PACKET
	struct sockaddr sa;
#else
	struct sockaddr_ll sll;
	struct packet_mreq mr;
#endif

	if (ki->fd >= 0)        /* Already UP */
		return 0;
#ifdef	USE_OBSOLETE_SOCK_PACKET
	if ((ki->fd = socket(PF_INET, SOCK_PACKET, ki->proto)) < 0) {
#else
	if ((ki->fd = socket(PF_PACKET, SOCK_RAW, ki->proto)) < 0) {
#endif
		printf("error in krnlif_up: socket() for %s failed. this should never happen\n", ki->iface->name);
		goto Fail;
	}
	strcpy(ifr.ifr_name, ki->iface->name);
	if (ioctl(ki->fd, SIOCGIFFLAGS, &ifr) < 0) {
		printf("error in krnlif_up: ioctl(SIOCGIFFLAGS) for %s failed. this should never happen\n", ki->iface->name);
		goto Fail;
	}
	ki->oldflags = ifr.ifr_flags;
	ifr.ifr_flags |= IFF_UP;
	if (ki->promisc)
		ifr.ifr_flags |= IFF_PROMISC;
	if (ioctl(ki->fd, SIOCSIFFLAGS, &ifr) < 0)
                goto Fail;
#ifdef	USE_OBSOLETE_SOCK_PACKET
        strcpy(sa.sa_data, ki->iface->name);
        sa.sa_family = AF_INET;
        if (bind(ki->fd, &sa, sizeof(struct sockaddr)) < 0)
                goto Fail;
#else
	memset(&sll, 0, sizeof(sll));
	sll.sll_ifindex = ki->ifindex;
	sll.sll_protocol = ki->proto;
	sll.sll_family = AF_PACKET;
	if (bind(ki->fd, (struct sockaddr *) &sll, sizeof(sll)) < 0) {
		printf("error in krnlif_up: bind() for %s failed. this should never happen.\ndebug: Error %s (%i)\n", ki->iface->name, strerror(errno), errno);
		goto Fail;
	}
	memset(&mr, 0, sizeof(&mr));
	mr.mr_ifindex = sll.sll_ifindex;
	if (ki->promisc) {
	  mr.mr_type = PACKET_MR_PROMISC;
	}
	if (setsockopt(ki->fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, (char *) &mr, sizeof(mr)) < 0) {
		perror("PACKET_ADD_MEMBERSHIP");
		printf("error in krnlif_up: setsockopt(SOL_PACKET, PACKET_ADD_MEMBERSHIP) for %s (%d) failed: %s (%i). this should never happen\n", ki->iface->name, ki->ifindex, strerror(errno), errno);
			goto Fail;
	}
	fcntl(ki->fd, F_SETFL, fcntl(ki->fd, F_GETFL, 0) | O_NONBLOCK);
#endif

	on_read(ki->fd, (void (*)(void *)) ki->iface->rxproc, ki->iface);
	return 0;

 Fail:
	if (ki->fd >= 0) {
		close(ki->fd);
		ki->fd = -1;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

static int krnlif_down(struct krnlif *ki)
{
	struct ifreq ifr;

	if (ki->fd < 0)         /* Already DOWN */
		return 0;

	off_read(ki->fd);
	off_write(ki->fd);
	free_q(&ki->sndq);
	strcpy(ifr.ifr_name, ki->iface->name);
	ifr.ifr_flags = ki->oldflags;
	ioctl(ki->fd, SIOCSIFFLAGS, &ifr);
	close(ki->fd);
	ki->fd = -1;
	return 0;
}

/*---------------------------------------------------------------------------*/

static void krnlif_tx(struct krnlif *ki)
{
#ifdef	USE_OBSOLETE_SOCK_PACKET
	struct sockaddr to;
#endif
	uint8 buf[4096]; /* should be enough */
	uint8 *bufp = buf;
	int cnt = 0;
	struct mbuf *bp;
	int i;

	if (ki->sndq == NULL) {
		off_write(ki->fd);
		//printf("debug: queue empty\n");
		return;
	}
	
        //printf("debug: next packet\n");
	for (bp = ki->sndq; bp; bp = bp->next) {
		//printf("debug: got %d bytes, first 0x%02x\n", bp->cnt, bp->data[0]);
		cnt += bp->cnt;
		if (cnt > sizeof(buf)) {
			/* packet too large. drop.
			 * dl9sau bugfix: free_p() to free this packet,
			 * not free_mbuf(), because ki->sndq must point to the
			 * next packet (anext) or point to 0 when no packet
			 * is left in the sendqueue.
			 */
			ki->sndq = free_p(&ki->sndq);
			return;
		}
		memcpy(bufp, bp->data, bp->cnt);
		bufp += bp->cnt;
		/* we send only one packet in this run */
		if (bp->next == bp->anext)
			break;
	}
#ifdef	USE_OBSOLETE_SOCK_PACKET
	strncpy(to.sa_data, ki->iface->name, sizeof(to.sa_data));
	i = sendto(ki->fd, buf, cnt, 0, &to, sizeof(to));
#else
	/* puh.. thanks to sockaddr_ll, the socket knows about the iface,
	 * and NULL is enough..
	 */
	i = sendto(ki->fd, buf, cnt, 0, NULL, 0);
#endif
	if (i >= 0) {
		ki->txpkts++;
		ki->txchar += cnt;
		// free packet. will go to next
		ki->sndq = free_p(&ki->sndq);
		return;
	}
	perror("krnlif_tx(): sendto()");
	printf("debug: krnlif_tx: sendto() returned %d while sending %d bytes\n", i, cnt);
	if (errno == EMSGSIZE) {
		// drop packet
		free_p(&ki->sndq);
		return;
	}
	if (errno == EWOULDBLOCK)
		return;
	krnlif_down(ki);
}

/*---------------------------------------------------------------------------*/

/* Asynchronous line I/O control */

static int32 krnlif_ioctl(struct iface *ifp, int cmd, int set, int32 val)
{
	struct krnlif *ki;
	struct mbuf *bp;

	if (!ifp || ifp->dev < 0 || ifp->dev >= KRNLIF_MAX)
		return -1;
	ki = KrnlIf + ifp->dev;

	switch(cmd){
	case PARAM_DOWN:
		return krnlif_down(ki) ? 0 : 1;
	case PARAM_UP:
		return krnlif_up(ki) ? 0 : 1;
	case PARAM_RETURN:
                return 0;
	case PARAM_TXDELAY:
	case PARAM_PERSIST:
	case PARAM_SLOTTIME:
	case PARAM_TXTAIL:
	case PARAM_FULLDUP:
	case PARAM_HW:
	case 12:                /* echo */
	case 13:                /* rxdelay */
		if (!set)
			return -1;      /* Can't read back */
		if (ki->iface == NULL || ki->fd < 0)
			return -1;
		if (!(bp = alloc_mbuf(2)))
			return -1;
		bp->data[0] = cmd;
		bp->data[1] = (unsigned char) val;
		bp->cnt = 2;
        	dump(ifp,IF_TRACE_OUT,bp);
        	ifp->rawsndcnt++;
        	ifp->lastsent = secclock();
        	if (ifp->trace & IF_TRACE_RAW)
                	raw_dump(ifp,-1,bp);
		enqueue(&ki->sndq, &bp);
		on_write(ki->fd, (void (*)(void *)) krnlif_tx, ki);
		return 1;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

static int krnlif_raw(struct iface *iface, struct mbuf **bpp)
{
	struct krnlif *ki;

	//printf("debug: queing %d bytes, first 0x%02x\n", (*bpp)->cnt, (*bpp)->data[0]);
	pushdown(bpp, NULL, 1);
	(*bpp)->data[0] = PARAM_DATA;
	dump(iface, IF_TRACE_OUT, *bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	if (iface->trace & IF_TRACE_RAW)
		raw_dump(iface, -1, *bpp);
	if (iface->dev < 0 || iface->dev >= KRNLIF_MAX){
		free_p(bpp);
		return -1;
	}
	ki = KrnlIf + iface->dev;

	if (ki->iface == NULL || ki->fd < 0)
		free_p(bpp);
	else {
		enqueue(&ki->sndq, bpp);
		on_write(ki->fd, (void (*)(void *)) krnlif_tx, ki);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static void krnlif_rx(struct iface *iface)
{
	struct krnlif *ki;
	struct sockaddr from;
#ifdef	USE_OBSOLETE_SOCK_PACKET
	int from_len = sizeof(from);
#endif
	int i;
	struct mbuf *bp;
	int j;

	if (!iface || iface->dev < 0 || iface->dev >= KRNLIF_MAX)
		return;
	ki = KrnlIf + iface->dev;
	if (!(bp = alloc_mbuf(ki->iface->mtu+16)))
		return;
#ifdef	USE_OBSOLETE_SOCK_PACKET
	i = recvfrom(ki->fd, bp->data, bp->size, 0, &from, &from_len);
#else
	i = recvfrom(ki->fd, bp->data, bp->size, 0, NULL, 0);
#endif
	if (i >= 0 && i < 1+AXALEN*2+1) {
		/* kiss-byte + AX25 frame: is minimal 1+(6call+1pid)*2 + Control-Byte */
#ifdef	notdef
		// debug: dump non ax25-kiss frames
	  	printf("frame dropped: read %d bytes - ", i);
		printf("bp->data: >");
		for (j = 0; j < i; j++) {
			printf("%d: 0x%02x", j, bp->data[j]);
			if (j < i-1)
				printf(", ");
		}
		printf("<\n");
#else
		// discard silently
	  	// debug: printf("warning: read %d bytes. should never happen\n", i);
#endif
	  	free_mbuf(&bp);
	  	return;
	}

	if (i <= 0) {
		free_mbuf(&bp);
		if (i < 0 || errno != EWOULDBLOCK) {
			printf("warning: read %d bytes from %s, cause %s (%i)\n", i, ki->iface->name, strerror(errno), errno);
			krnlif_down(ki);
		}
		return;
	}
	bp->cnt = i;
	ki->rxpkts++;
	ki->rxchar += i;
	if (ki->iface->trace & IF_TRACE_RAW)
		raw_dump(ki->iface, IF_TRACE_IN, bp);
	net_route(ki->iface, &bp);
}

/*---------------------------------------------------------------------------*/

static void krnlif_status(struct iface *iface)
{
	struct krnlif *ki;

	if (!iface || iface->dev < 0 || iface->dev >= KRNLIF_MAX)
		return;
	ki = KrnlIf + iface->dev;
	if (ki->iface != iface)
		return;

	printf("           Received:    Packets %8ld Chars %8ld\n"
	       "           Transmitted: Packets %8ld Chars %8ld\n",
	       ki->rxpkts, ki->rxchar, ki->txpkts, ki->txchar);
}

/*---------------------------------------------------------------------------*/

static int krnlif_detach(struct iface *ifp)
{
	struct krnlif *ki;

	if (ifp == NULL)
		return -1;
	if (ifp->dev < 0 || ifp->dev >= KRNLIF_MAX)
		return -1;
	ki = KrnlIf + ifp->dev;
	if(ki->iface == NULL)
		return -1;      /* Not allocated */
	krnlif_down(ki);
	ki->iface = NULL;
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct hwencap {
	unsigned short family;
	char *encap;
} hwencap[] = {
	{ ARPHRD_NETROM,        "NETROM" },
	{ ARPHRD_AX25,          "KISSI" },
	{ 0,                    0 }
};

int krnlif_attach(int argc, char *argv[], void *p)
{
	struct iface *ifp;
	int dev;
	struct krnlif *ki;
	struct ifreq ifr;
	struct sockaddr hw;
	struct sockaddr_in in;
#ifndef	USE_OBSOLETE_SOCK_PACKET
	struct ifreq ifr_h;
#endif
	struct hwencap *hwe = hwencap;
	int fd;

	if (if_lookup(argv[1]) != NULL) {
		printf("Interface %s already exists\n",argv[1]);
		return -1;
	}
	/*
	 * get parameters of the interface
	 */
#ifdef	USE_OBSOLETE_SOCK_PACKET
	if ((fd = socket(PF_INET, SOCK_PACKET, htons(ETH_P_ALL))) < 0) {
#else
	if ((fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_AX25))) < 0) {
#endif
		perror("socket");
		return -1;
	}

	strncpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
	ifr.ifr_addr.sa_family = AF_INET;
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
		perror("ioctl (SIOCGIFADDR)");
		printf("cannot get inet addr for interface %s\n", argv[1]);
		close(fd);
		return -1;
	}
	if (ifr.ifr_addr.sa_family != AF_INET) {
		printf("Interface %s: not AF_INET, %d\n", argv[1],
		       in.sin_family);
		close(fd);
		return -1;
	}
#ifdef	USE_OBSOLETE_SOCK_PACKET
	memcpy(&in, &ifr.ifr_addr, sizeof(in));
	ifr.ifr_addr.sa_family = AF_UNSPEC;
	if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
		perror("ioctl (SIOCGIFADDR)");
		printf("cannot get hw addr for interface %s\n", argv[1]);
		close(fd);
		return -1;
	}
#endif
#ifndef	USE_OBSOLETE_SOCK_PACKET
	strncpy(ifr_h.ifr_name, argv[1], sizeof(ifr_h.ifr_name));
	if (ioctl(fd, SIOCGIFINDEX, &ifr_h) < 0) {
		perror("ioctl (SIOCGIFINDEX)");
		printf("cannot get index of interface %s\n", argv[1]);
		close(fd);
		return -1;
	}
	strncpy(ifr.ifr_name, argv[1], sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
		perror("ioctl (SIOCGIHWADDR)");
		printf("cannot get hw addr for interface %s\n", argv[1]);
		close(fd);
		return -1;
	}
#endif
	hw = ifr.ifr_hwaddr;
	for (; (hwe->family != hw.sa_family) && (hwe->encap != NULL); hwe++);
	if (hwe->family != hw.sa_family) {
		printf("Interface %s: invalid ARP type %d\n", argv[1],
		       hw.sa_family);
		return -1;
	}
	if (ioctl(fd, SIOCGIFMTU, &ifr) < 0) {
		perror("ioctl (SIOCGIFMTU)");
		printf("cannot get mtu for interface %s\n", argv[1]);
		close(fd);
		return -1;
	}

	/* Find unused krnlif control block */
	for (dev=0; (dev < KRNLIF_MAX) && (KrnlIf[dev].iface); dev++);
	if (dev >= KRNLIF_MAX){
		printf("Too many kernel interfaces\n");
		return -1;
	}

	ki = KrnlIf+dev;
	/* Create interface structure and fill in details */
	ifp = (struct iface *)callocw(1,sizeof(struct iface));
	ifp->addr = Ip_addr /*in.sin_addr.s_addr*/;
	ifp->name = strdup(argv[1]);
	ifp->mtu = ifr.ifr_mtu;
	ifp->dev = dev;
	ifp->stop = krnlif_detach;
	/* set encapsulation */
	setencap(ifp, hwe->encap);
	ifp->ioctl = krnlif_ioctl;
	ifp->raw = krnlif_raw;
	ifp->show = krnlif_status;
	ifp->xdev = dev;
	ifp->rxproc = krnlif_rx;
	ifp->crccontrol = CRC_OFF;
	/* set hwaddr */
	if (hw.sa_family == ARPHRD_AX25) {
		if (ifp->hwaddr == NULL)
			ifp->hwaddr = (uint8 *) mallocw(AXALEN);
		memcpy(ifp->hwaddr, Mycall, AXALEN);
	}

	ki->fd = -1;
	ki->iface = ifp;
#ifndef	USE_OBSOLETE_SOCK_PACKET
	ki->ifindex = ifr_h.ifr_ifindex;
#endif
	ki->proto = htons(ETH_P_AX25);
	ki->promisc = !((argc >= 3) && !strcmp(argv[2], "nopromisc"));;

	/* Link in the interface */
	ifp->next = Ifaces;
	Ifaces = ifp;

	return krnlif_up(ki);
}

/*---------------------------------------------------------------------------*/

#else

int krnlif_attach(int argc, char *argv[], void *p)
{
	printf("kernel interface not implemented.\n");
	return -1;
}

#endif
