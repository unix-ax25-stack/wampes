/* @(#) $Id: iface.h,v 1.28 2002/09/19 19:11:44 dl9sau Exp $ */

#ifndef _IFACE_H
#define _IFACE_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#include <stdio.h>

/* Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary
 * to attach a device.
 */
struct iface;   /* Defined later */
struct iftype {
	char *name;             /* Name of encapsulation technique */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
				/* Routine to send an IP datagram */
	int (*output)(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
				/* Routine to send link packet */
	char *(*format)(char *,const uint8 *);
				/* Function that formats addresses */
	int (*scan)(uint8 *,const char *);
				/* Reverse of format */
	int type;               /* Type field for network process */
	int hwalen;             /* Length of hardware address, if any */
	void (*rcvf)(struct iface *,struct mbuf **);
				/* Function that handles incoming packets */
	int (*addrtest)(struct iface *,struct mbuf *);
				/* Function that tests incoming addresses */
	void (*trace)(FILE *,struct mbuf **,int);
				/* Function that decodes protocol headers */
	int (*dinit)(struct iface *,int32,int,char **);
				/* Function to initialize demand dialing */
	int (*dstat)(struct iface *);
				/* Function to display dialer status */
        // dl9sau: patch for ARP requests (to QST) via multible digipeaters
        // for an extended "collision domain"
#define	AX_MCAST_DIGIS_MAX	8
	uint8 *ax_mcast_digis[AX_MCAST_DIGIS_MAX];
				/* possible multicast digis for ax25 ARP to QST-0 */
};
extern struct iftype Iftypes[];

/* Interface control structure */
struct iface {
	struct iface *next;     /* Linked list pointer */
	char *name;             /* Ascii string with interface name */

	int32 addr;             /* IP address */
	int32 broadcast;        /* Broadcast address */
	int32 netmask;          /* Network mask */

	uint mtu;               /* Maximum transmission unit size */

	uint trace;             /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN     0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
#define IF_TRACE_NOBC   0x1000  /* Suppress broadcasts */
#define IF_TRACE_RAW    0x2000  /* Raw dump, if supported */
	FILE *trfp;             /* Stream to trace to */

	struct iface *forw;     /* Forwarding interface for output, if rx only */

	void (*rxproc)(void *); /* Receiver process, if any */
	struct proc *txproc;    /* IP send process */
	struct proc *supv;      /* Supervisory process, if any */

	struct mbuf *outq;      /* IP datagram transmission queue */
	int outlim;             /* Limit on outq length */
	int txbusy;             /* Transmitter is busy */

	void *dstate;           /* Demand dialer link state, if any */
	int (*dtickle)(struct iface *);
				/* Function to tickle dialer, if any */
	void (*dstatus)(struct iface *);
				/* Function to display dialer state, if any */

	/* Device dependent */
	int dev;                /* Subdevice number to pass to send */
				/* To device -- control */
	int32 (*ioctl)(struct iface *,int cmd,int set,int32 val);
				/* From device -- when status changes */
	int (*iostatus)(struct iface *,int cmd,int32 val);
				/* Call before detaching */
	int (*stop)(struct iface *);
	uint8 *hwaddr;          /* Device hardware address, if any */

	/* Encapsulation dependent */
	void *edv;              /* Pointer to protocol extension block, if any */
	int xdev;               /* Associated Slip or Nrs channel, if any */
	struct iftype *iftype;  /* Pointer to appropriate iftype entry */

				/* Routine to send an IP datagram */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
			/* Encapsulate any link packet */
	int (*output)(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
			/* Send raw packet */
	int (*raw)(struct iface *,struct mbuf **);
			/* Display status */
	void (*show)(struct iface *);

	int (*discard)(struct iface *,struct mbuf **);
	int (*echo)(struct iface *,struct mbuf **);

	/* Counters */
	int32 ipsndcnt;         /* IP datagrams sent */
	int32 rawsndcnt;        /* Raw packets sent */
	int32 iprecvcnt;        /* IP datagrams received */
	int32 rawrecvcnt;       /* Raw packets received */
	int32 lastsent;         /* Clock time of last send */
	int32 lastrecv;         /* Clock time of last receive */

	int crccontrol;         /* CRC send control */
	int crcfixed;           /* Set by "ifconfig <if> crc": stop autodetecting */
#define CRC_OFF         0       /* Don't send CRC packets */
#define CRC_TEST_16     1       /* Send a single CRC_16 packet, then switch to CRC_TEST_RMNC */
#define CRC_TEST_RMNC   2       /* Send a single CRC_RMNC packet, then switch to CRC_OFF */
#define CRC_16          3       /* Send CRC_16 packets */
#define CRC_RMNC        4       /* Send CRC_RMNC packets */
#define CRC_CCITT       5       /* Send CRC_CCITT packets */
	/* DAMA, see dama.c.  The role is what the sysop asked for; whether it
	 * is in force depends on a master actually being heard, which is what
	 * dama_heard records.
	 */
	int dama;               /* DAMA_OFF or DAMA_SLAVE */
	int dama_watchdog;      /* Seconds of silence before we stop following
				 * a master; 0 means the built-in default */
	int32 dama_heard;       /* When a DAMA marked frame was last seen here,
				 * 0 = not following anybody */
	int32 dama_entered;     /* Times a master was found */
	int32 dama_lost;        /* Times one went away again */
	int32 dama_polls;       /* Polls answered */
	int dama_window;        /* Set only while lapb_input() handles a poll
				 * on this port - the whole of a slave's
				 * permission to transmit, and it belongs to
				 * the station, not to one connection */
	int dama_turn;          /* Which link gets the next turn */
	uint8 dama_master[7];   /* Whose polls count.  On a DAMA channel one
				 * station decides who transmits, so a poll
				 * from anybody else is not a poll. */

	int32 crcerrors;        /* Packets received with CRC errors */
	int32 ax25errors;       /* Packets received with bad ax25 header */
	uint flags;             /* Configuration flags */
#define NO_RT_ADD       1       /* Don't call rt_add in ip_route */
};
extern struct iface *Ifaces;    /* Head of interface list */
extern struct iface  Loopback;  /* Optional loopback interface */
extern struct iface  Encap;     /* IP-in-IP pseudo interface */

/* Header put on front of each packet sent to an interface */
#ifdef ibm032
#define qhdr Xqhdr      /* Resolve name conflict */
#endif
struct qhdr {
	uint8 tos;
	int32 gateway;
};

extern char Noipaddr[];
extern struct mbuf *Hopper;

/* In iface.c: */
int bitbucket(struct iface *ifp,struct mbuf **bp);
int if_detach(struct iface *ifp);
struct iface *if_lookup(char *name);
char *if_name(struct iface *ifp,char *comment);
void if_tx(int dev,void *arg1,void *unused);
struct iface *ismyaddr(int32 addr);
void network(int i,void *v1,void *v2);
int nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos);
int nu_output(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
int setencap(struct iface *ifp,char *mode);

/* In config.c: */
int net_route(struct iface *ifp,struct mbuf **bpp);

/* Smallest MTU IP can work with - RFC 791.  See mtu_ok() in iface.c. */
#define MTU_MIN 68

int mtu_ok(const char *who,long mtu);

#endif  /* _IFACE_H */
