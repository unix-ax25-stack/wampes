/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/n8250.h,v 1.1 1991/02/24 20:18:29 deyke Exp $ */

/* Various I/O definitions specific to asynch I/O */
#ifndef _8250_H
#define _8250_H

#ifndef _IFACE_H
#include "iface.h"
#endif

/* Asynch controller control block */
struct asy {
	struct iface *iface;    /* Associated interface structure */
	int  fd;                /* File descriptor */
	int  speed;             /* Line speed */
	char  *ipc_socket;      /* Host:port of ipc destination */
	long  rxints;           /* receive interrupts */
	long  txints;           /* transmit interrupts */
	long  rxchar;           /* Received characters */
	long  txchar;           /* Transmitted characters */
	long  rxhiwat;          /* High water mark on rx fifo */
};
extern struct asy Asy[];

#endif  /* _8250_H */
