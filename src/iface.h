/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iface.h,v 1.2 1990/08/23 17:33:06 deyke Exp $ */

#ifndef NULLIF
#include <stdio.h>
#include "global.h"

/* Interface control structure */
struct iface {
	struct iface *next;     /* Linked list pointer */
	char *name;             /* Ascii string with interface name */
	int (*ioctl)();         /* Function to handle device control */
	int (*send)();          /* Routine to send an IP datagram */
	int (*output)();        /* Routine to send link packet */
	int (*raw)();           /* Routine to call to send raw packet */
	int (*stop)();          /* Routine to call before detaching */
	void (*recv)();         /* Routine to kick to process input */
	int16 mtu;              /* Maximum transmission unit size */
	int dev;                /* Subdevice number to pass to send */
	int16 flags;            /* Configuration flags */
#define DATAGRAM_MODE   0       /* Send datagrams in raw link frames */
#define CONNECT_MODE    1       /* Send datagrams in connected mode */
	int16 trace;            /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN     0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
	char *hwaddr;           /* Device hardware address, if any */
	struct iface *forw;     /* Forwarding interface for output, if rx only */
};
#define NULLIF  (struct iface *)0
extern struct iface *Ifaces;    /* Head of interface list */

/* In iface.c: */
struct iface *if_lookup __ARGS((char *name));
struct iface *ismyaddr __ARGS((int32 addr));
int if_detach __ARGS((struct iface *ifp));
int setencap __ARGS((struct iface *ifp,char *mode));

#endif  /* NULLIF */

