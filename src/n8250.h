/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/n8250.h,v 1.3 1991/05/21 19:08:19 deyke Exp $ */

/* Various I/O definitions specific to asynch I/O */
#ifndef _8250_H
#define _8250_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* Asynch controller control block */
struct asy {
	struct iface *iface;
	char *ipc_socket;       /* Host:port of ipc destination */
	int fd;                 /* File descriptor */
	struct mbuf *sndq;      /* Transmit queue */
	int speed;              /* Line speed in bits per second */
	long rxints;            /* receive interrupts */
	long txints;            /* transmit interrupts */
	long rxchar;            /* Received characters */
	long txchar;            /* Transmitted characters */
	long rxhiwat;           /* High water mark on hardware rx fifo */
};
extern struct asy Asy[];

#endif  /* _8250_H */
