/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/nrs.h,v 1.6 1992/05/28 13:50:27 deyke Exp $ */

#ifndef _NRS_H
#define _NRS_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#define NRS_MAX 16              /* Maximum number of Nrs channels */

/* SLIP definitions */
#define NRS_ALLOC       512     /* Receiver allocation increment */

#define STX     0x02            /* frame start */
#define ETX 0x03                /* frame end */
#define DLE     0x10            /* data link escape */
#define NUL 0x0                 /* null character */

/* packet unstuffing state machine */
#define NRS_INTER       0               /* in between packets */
#define NRS_INPACK      1               /* we've seen STX, and are in a the packet */
#define NRS_ESCAPE      2               /* we've seen a DLE while in NRS_INPACK */
#define NRS_CSUM        3               /* we've seen an ETX, and are waiting for the checksum */

/* net/rom serial protocol control structure */
struct nrs {
	char state;             /* Receiver State control flag */
	unsigned char csum;     /* Accumulating checksum */
	struct mbuf *rbp;       /* Head of mbuf chain being filled */
	struct mbuf *rbp1;      /* Pointer to mbuf currently being written */
	char *rcp;              /* Write pointer */
	int16 rcnt;             /* Length of mbuf chain */
	struct mbuf *tbp;       /* Transmit mbuf being sent */
	long errors;            /* Checksum errors detected */
	long packets ;          /* Number of packets received successfully */
	struct iface *iface ;   /* Associated interface structure */
	int (*send) __ARGS((int,struct mbuf *));/* Routine to send mbufs */
	int (*get) __ARGS((int,char *,int));/* Routine to fetch input chars */
};

extern struct nrs Nrs[];
/* In nrs.c: */
int nrs_free __ARGS((struct iface *ifp));
int nrs_init __ARGS((struct iface *ifp,int vj));
int nrs_raw __ARGS((struct iface *iface,struct mbuf *bp));
void nrs_recv __ARGS((struct iface *iface));

#endif  /* _NRS_H */
