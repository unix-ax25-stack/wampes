/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iface.h,v 1.7 1991/05/09 07:38:22 deyke Exp $ */

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

/* Interface control structure */
struct iface {
	struct iface *next;     /* Linked list pointer */
	char *name;             /* Ascii string with interface name */
	int type;               /* Link header type for phdr */
	struct iftype *iftype;  /* Pointer to appropriate iftype entry */

	int32 addr;             /* IP address */
	int32 broadcast;        /* Broadcast address */
	int32 netmask;          /* Network mask */

				/* To device -- control */
	int32 (*ioctl) __ARGS((struct iface *,int cmd,int set,int32 val));
				/* From device -- when status changes */
	int (*iostatus) __ARGS((struct iface *,int cmd,int32 val));
				/* Encapsulate an IP datagram */
	int (*send) __ARGS((struct mbuf *,struct iface *,int32,int,int,int,int));
				/* Encapsulate any link packet */
	int (*output) __ARGS((struct iface *,char *,char *,int,struct mbuf *));
				/* Send raw packet */
	int (*raw) __ARGS((struct iface *,struct mbuf *));
				/* Call before detaching */
	int (*stop) __ARGS((struct iface *));
				/* Display status */
	int (*status) __ARGS((struct iface *));

	int16 mtu;              /* Maximum transmission unit size */
	int dev;                /* Subdevice number to pass to send */
	int xdev;               /* Associated Slip or Nrs channel, if any */
	int16 flags;            /* Configuration flags */
#define DATAGRAM_MODE   0       /* Send datagrams in raw link frames */
#define CONNECT_MODE    1       /* Send datagrams in connected mode */

	int16 trace;            /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN     0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
#define IF_TRACE_NOBC   0x1000  /* Suppress broadcasts */
#define IF_TRACE_RAW    0x2000  /* Raw dump, if supported */
	char *trfile;           /* Trace file name, if any */
	FILE *trfp;             /* Stream to trace to */

	char *hwaddr;           /* Device hardware address, if any */
	struct iface *forw;     /* Forwarding interface for output, if rx only */

	int32 ipsndcnt;         /* IP datagrams sent */
	int32 rawsndcnt;        /* Raw packets sent */
	int32 iprecvcnt;        /* IP datagrams received */
	int32 rawrecvcnt;       /* Raw packets received */
	int32 lastsent;         /* Clock time of last send */
	int32 lastrecv;         /* Clock time of last receive */

	void (*rxproc) __ARGS((struct iface *)); /* Receiver process, if any */
	struct proc *txproc;    /* Transmitter process, if any */
	struct proc *supv;      /* Supervisory process, if any */
	int sendcrc;            /* Send CRC if true (KISS only) */
	int32 crcerrors;        /* Packets received with CRC errors (KISS only) */

	void *extension;        /* Pointer to protocol extension block, if any */
};
#define NULLIF  (struct iface *)0
extern struct iface *Ifaces;    /* Head of interface list */
extern struct iface Loopback;   /* Optional loopback interface */
extern struct iface Encap;      /* IP-in-IP pseudo interface */

/* Header put on front of each packet in input queue */
struct phdr {
	struct iface *iface;
	unsigned short type;    /* Use pktdrvr "class" values */
};
extern char Noipaddr[];
extern struct mbuf *Hopper;

/* Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary
 * to attach a device.
 */
struct iftype {
	char *name;             /* Name of encapsulation technique */
	int (*send) __ARGS((struct mbuf *,struct iface *,int32,int,int,int,int));
				/* Routine to send an IP datagram */
	int (*output) __ARGS((struct iface *,char *,char *,int,struct mbuf *));
				/* Routine to send link packet */
	char *(*format) __ARGS((char *,char *));
				/* Function that formats addresses */
	int (*scan) __ARGS((char *,char *));
				/* Reverse of format */
	int type;               /* Type field for network process */
	int hwalen;             /* Length of hardware address, if any */
};
#define NULLIFT (struct iftype *)0
extern struct iftype Iftypes[];

/* In iface.c: */
struct iface *if_lookup __ARGS((char *name));
struct iface *ismyaddr __ARGS((int32 addr));
int if_detach __ARGS((struct iface *ifp));
int setencap __ARGS((struct iface *ifp,char *mode));
char *if_name __ARGS((struct iface *ifp,char *comment));
int bitbucket __ARGS((struct iface *ifp,struct mbuf *bp));

/* In config.c: */
int net_route __ARGS((struct iface *ifp,int type,struct mbuf *bp));

#endif  /* _IFACE_H */
