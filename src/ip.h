/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ip.h,v 1.2 1990/08/23 17:33:10 deyke Exp $ */

#ifndef NROUTE

#include "global.h"
#include "internet.h"
#include "timer.h"

#define NROUTE  5       /* Number of hash chains in routing table */

extern int32 Ip_addr;   /* Our IP address for ICMP and source routing */

extern int32 ip_ttl;    /* Default time-to-live for IP datagrams */

#define IPVERSION       4
/* IP header, INTERNAL representation */
struct ip {
	char version;           /* IP version number */
	char tos;               /* Type of service */
	int16 length;           /* Total length */
	int16 id;               /* Identification */
	int16 fl_offs;          /* Flags + fragment offset */

#define F_OFFSET        0x1fff  /* Offset field */
#define DF      0x4000          /* Don't fragment flag */
#define MF      0x2000          /* More Fragments flag */

	char ttl;               /* Time to live */
	char protocol;          /* Protocol */
	int32 source;           /* Source address */
	int32 dest;             /* Destination address */
	char options[44];       /* Options field */
	int16 optlen;           /* Length of options field, bytes */
};
#define NULLIP  (struct ip *)0
#define IPLEN   20      /* Length of standard IP header */

/* Fields in option type byte */
#define OPT_COPIED      0x80    /* Copied-on-fragmentation flag */
#define OPT_CLASS       0x60    /* Option class */
#define OPT_NUMBER      0x1f    /* Option number */

/* IP option numbers */
#define IP_EOL          0       /* End of options list */
#define IP_NOOP         1       /* No Operation */
#define IP_SECURITY     2       /* Security parameters */
#define IP_LSROUTE      3       /* Loose Source Routing */
#define IP_TIMESTAMP    4       /* Internet Timestamp */
#define IP_RROUTE       7       /* Record Route */
#define IP_STREAMID     8       /* Stream ID */
#define IP_SSROUTE      9       /* Strict Source Routing */

/* Timestamp option flags */
#define TS_ONLY         0       /* Time stamps only */
#define TS_ADDRESS      1       /* Addresses + Time stamps */
#define TS_PRESPEC      3       /* Prespecified addresses only */

/* IP routing table entry */
struct route {
	struct route *prev;     /* Linked list pointers */
	struct route *next;
	int32 target;           /* Target IP address */
	int32 gateway;          /* IP address of local gateway for this target */
	int metric;             /* Hop count, whatever */
	struct iface *iface;    /* Device interface structure */
};
#define NULLROUTE       (struct route *)0
extern struct route *Routes[32][NROUTE];        /* Routing table */
extern struct route R_default;                  /* Default route entry */

/* Cache for the last-used routing entry, speeds up the common case where
 * we handle a burst of packets to the same destination
 */
struct rt_cache {
	int32 target;
	struct route *route;
};
extern struct rt_cache rt_cache;

/* Reassembly descriptor */
struct reasm {
	struct reasm *next;     /* Linked list pointers */
	struct reasm *prev;
	int32 source;           /* These four fields uniquely describe a datagram */
	int32 dest;
	int16 id;
	char protocol;
	int16 length;           /* Entire datagram length, if known */
	struct timer timer;     /* Reassembly timeout timer */
	struct frag *fraglist;  /* Head of data fragment chain */
};
#define NULLREASM       (struct reasm *)0

/* Fragment descriptor in a reassembly list */
struct frag {
	struct frag *prev;      /* Previous fragment on list */
	struct frag *next;      /* Next fragment */
	struct mbuf *buf;       /* Actual fragment data */
	int16 offset;           /* Starting offset of fragment */
	int16 last;             /* Ending offset of fragment */
};
#define NULLFRAG        (struct frag *)0

extern struct reasm *Reasmq;    /* The list of reassembly descriptors */

/* IP error logging counters */
struct ip_stats {
	long total;             /* Total packets received */
	unsigned runt;          /* Smaller than minimum size */
	unsigned length;        /* IP header length field too small */
	unsigned version;       /* Wrong IP version */
	unsigned checksum;      /* IP header checksum errors */
	unsigned badproto;      /* Unsupported protocol */
};
extern struct ip_stats Ip_stats;

/* In ip.c: */
void ip_recv __ARGS((struct ip *ip,struct mbuf *bp,
	int rxbroadcast));
int ip_send __ARGS((int32 source,int32 dest,int protocol,int tos,int ttl,
	struct mbuf *bp,int length,int id,int df));
struct raw_ip *raw_ip __ARGS((int protocol,void (*r_upcall) __ARGS((struct raw_ip *)) ));
void del_ip __ARGS((struct raw_ip *rrp));

/* In iproute.c: */
int16 ip_mtu __ARGS((int32 addr));
int ip_route __ARGS((struct mbuf *bp,int rxbroadcast));
int32 locaddr __ARGS((int32 addr));
void rt_merge __ARGS((int trace));
struct route *rt_add __ARGS((int32 target,unsigned int bits,int32 gateway,
	int32 metric,struct iface *iface));
int rt_drop __ARGS((int32 target,unsigned int bits));
struct route *rt_lookup __ARGS((int32 target));
struct route *rt_blookup __ARGS((int32 target,unsigned int bits));

/* In iphdr.c: */
int16 cksum __ARGS((struct pseudo_header *ph,struct mbuf *m,int len));
int16 eac __ARGS((int32 sum));
struct mbuf *htonip __ARGS((struct ip *ip,struct mbuf *data));
int ntohip __ARGS((struct ip *ip,struct mbuf **bpp));

/* In either lcsum.c or pcgen.asm: */
int16 lcsum __ARGS((int16 *wp,int len));

#endif /* NROUTE */

