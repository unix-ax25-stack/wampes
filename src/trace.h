/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/trace.h,v 1.3 1990/09/11 13:46:42 deyke Exp $ */

#ifndef TRACE_INCL
#define TRACE_INCL

#include <stdio.h>
#include "iface.h"

/* Definitions for packet dumping */

/* List of address testing and tracing functions for each interface.
 * Entries are placed in this table by conditional compilation in main.c.
 */
struct trace {
	int (*addrtest) __ARGS((struct iface *iface,struct mbuf *bp));
	void (*tracef) __ARGS((FILE *,struct mbuf **,int));
};

extern struct trace Tracef[];

/* In trace.c: */
void dump __ARGS((struct iface *iface,int direction,unsigned type,struct mbuf *bp));

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

/* In ripdump.c: */
void rip_dump __ARGS((FILE *fp,struct mbuf **bpp));

/* In tcpdump.c: */
void tcp_dump __ARGS((FILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check));

/* In udpdump.c: */
void udp_dump __ARGS((FILE *fp,struct mbuf **bpp,int32 source,int32 dest,int check));

#endif  /* TRACE_INCL */
