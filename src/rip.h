/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/rip.h,v 1.2 1991/02/24 20:17:34 deyke Exp $ */

#ifndef _RIP_H
#define _RIP_H

/* Routing Information Protocol (RIP)
 *
 *      This code is derived from the 4.2 BSD version which was
 * used as a spec since no formal specification is known to exist.
 * See RFC 1009, Gateway Requirements, for more details. AGB 4-29-88
 *
 * The draft RIP RFC was used to develop most of this code. The above
 * referred to the basics of the rip_recv() function of RIP.C. The RIP
 * RFC has now been issued as RFC1058. AGB 7-23-88
 *
 * Substantially rewritten and integrated into NOS 9/1989 by KA9Q
 */
#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _UDP_H
#include "udp.h"
#endif

#define RIP_INFINITY    16
#define RIP_TTL         240     /* Default time-to-live for an entry */
#define RIPVERSION      1
#define RIP_IPFAM       2

/* UDP Port for RIP */
#define RIP_PORT        520

/* RIP Packet Types */
#define RIPCMD_REQUEST          1       /* want info */
#define RIPCMD_RESPONSE         2       /* responding to request */
#define RIPCMD_MAX              3

#define HOPCNT_INFINITY         16      /* per Xerox NS */
#define MAXRIPROUTES            25      /* maximum # routes per RIP pkt */

struct rip_list {
	struct rip_list *prev;
	struct rip_list *next;  /* doubly linked list */

	/* address to scream at periodically:
	 * this address must have a direct network interface route and an
	 * ARP entry for the appropriate  hardware broadcast address, if approp.
	 */
	int32 dest;

	/* basic rate of RIP clocks on this net interface */
	int32 interval;

	struct timer rip_time;  /* time to output next on this net. iface */

	/* the interface to transmit on  and receive from */
	struct iface *iface;

	/* described below with the mask defs */
	char    flags;
#define RIP_SPLIT 0x1
};
#define NULLRL  (struct rip_list *)0

/* Host format of a single entry in a RIP response packet */
struct rip_route {
	int16   addr_fam;
	int32   target;
	int32   metric;
};
#define RIPROUTE        20      /* Size of each routing entry */
#define RIPHEADER       4       /* Size of rip header before routes */
#define MAXRIPPACKET    RIPHEADER + (MAXRIPROUTES*RIPROUTE)

/* RIP statistics counters */
struct rip_stat {
	int32 output;           /* Packets sent */
	int32 rcvd;             /* Packets received */
	int32 request;          /* Number of request packets received */
	int32 response;         /* Number of responses received */
	int32 unknown;          /* Number of unknown command pkts received */
	int32 version;          /* Number of version errors */
	int32 addr_family;      /* Number of address family errors */
	int32 refusals;         /* Number of packets dropped from a host
					on the refuse list */
};

struct rip_refuse {
	struct rip_refuse *prev;
	struct rip_refuse *next;
	int32   target;
};
#define NULLREF (struct rip_refuse *)0

/* RIP primitives */
int rip_init __ARGS((void));
void rt_timeout __ARGS((void *s));
void rip_trigger __ARGS((void));
int rip_add __ARGS((int32 dest,int32 interval,int flags));
int riprefadd __ARGS((int32 gateway));
int riprefdrop __ARGS((int32 gateway));
int ripreq __ARGS((int32 dest,int replyport));
int rip_drop __ARGS((int32 dest));
int nbits __ARGS((int32 target));
void pullentry __ARGS((struct rip_route *ep,struct mbuf **bpp));

/* RIP Definition */
extern int16 Rip_trace;
extern int Rip_merge;
extern struct rip_stat Rip_stat;
extern struct rip_list *Rip_list;
extern struct rip_refuse *Rip_refuse;
extern struct udp_cb *Rip_cb;

#endif  /* _RIP_H */

