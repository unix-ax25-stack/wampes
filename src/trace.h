/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/trace.h,v 1.8 1992/06/01 10:34:34 deyke Exp $ */

#ifndef _TRACE_H
#define _TRACE_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#include <stdio.h>

/* Definitions for packet dumping */

/* Table of trace subcommands */
struct tracecmd {
	char *name;     /* Name of trace subcommand */
	int val;        /* New setting for these bits */
	int mask;       /* Mask of bits affected in trace word */
};
extern struct tracecmd Tracecmd[];      /* Defined in trace.c */

/* List of address testing and tracing functions for each interface.
 * Entries are placed in this table by conditional compilation in main.c.
 */
struct trace {
	int (*addrtest) __ARGS((struct iface *iface,struct mbuf *bp));
	void (*tracef) __ARGS((FILE *,struct mbuf **,int));
};

extern struct trace Tracef[];

/* In trace.c: */
void dump __ARGS((struct iface *ifp,int direction,struct mbuf *bp));
void raw_dump __ARGS((struct iface *ifp,int direction, struct mbuf *bp));
void trace_log __ARGS((struct iface *ifp,char *fmt, ...));
void shuttrace __ARGS ((void));
int tprintf __ARGS((struct iface *ifp,char *fmt,...));

/* In arcdump.c: */
void arc_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));
int arc_forus __ARGS((struct iface *iface,struct mbuf *bp));

/* In arpdump.c: */
void arp_dump __ARGS((FILE *fp,struct mbuf **bpp));

/* In ax25dump.c: */
void ax25_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));
int ax_forus __ARGS((struct iface *iface,struct mbuf *bp));

/* In enetdump.c: */
void ether_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));
int ether_forus __ARGS((struct iface *iface,struct mbuf *bp));

/* In icmpdump.c: */
void icmp_dump __ARGS((FILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check));

/* In ipdump.c: */
void ip_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));

/* In kissdump.c: */
void ki_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));
int ki_forus __ARGS((struct iface *iface,struct mbuf *bp));

/* In nrdump.c: */
void netrom_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));

/* In pppdump.c: */
void ppp_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));

/* In ripdump.c: */
void rip_dump __ARGS((FILE *fp,struct mbuf **bpp));

/* In slcompdump.c: */
void sl_dump __ARGS((FILE *fp,struct mbuf **bpp,int check));
void vjcomp_dump __ARGS((FILE *fp,struct mbuf **bpp,int unused));

/* In tcpdump.c: */
void tcp_dump __ARGS((FILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check));

/* In udpdump.c: */
void udp_dump __ARGS((FILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check));

#endif  /* _TRACE_H */
