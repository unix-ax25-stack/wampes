/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmp.h,v 1.3 1990/08/23 17:33:00 deyke Exp $ */

#ifndef ICMP_ECHO_REPLY

#include "global.h"
#include "internet.h"
#include "ip.h"

/* SNMP MIB variables, used for statistics and control. See RFC 1066 */
extern struct mib_entry Icmp_mib[];
#define icmpInMsgs              Icmp_mib[1].value.integer
#define icmpInErrors            Icmp_mib[2].value.integer
#define icmpInDestUnreachs      Icmp_mib[3].value.integer
#define icmpInTimeExcds         Icmp_mib[4].value.integer
#define icmpInParmProbs         Icmp_mib[5].value.integer
#define icmpInSrcQuenchs        Icmp_mib[6].value.integer
#define icmpInRedirects         Icmp_mib[7].value.integer
#define icmpInEchos             Icmp_mib[8].value.integer
#define icmpInEchoReps          Icmp_mib[9].value.integer
#define icmpInTimestamps        Icmp_mib[10].value.integer
#define icmpInTimestampReps     Icmp_mib[11].value.integer
#define icmpInAddrMasks         Icmp_mib[12].value.integer
#define icmpInAddrMaskReps      Icmp_mib[13].value.integer
#define icmpOutMsgs             Icmp_mib[14].value.integer
#define icmpOutErrors           Icmp_mib[15].value.integer
#define icmpOutDestUnreachs     Icmp_mib[16].value.integer
#define icmpOutTimeExcds        Icmp_mib[17].value.integer
#define icmpOutParmProbs        Icmp_mib[18].value.integer
#define icmpOutSrcQuenchs       Icmp_mib[19].value.integer
#define icmpOutRedirects        Icmp_mib[20].value.integer
#define icmpOutEchos            Icmp_mib[21].value.integer
#define icmpOutEchoReps         Icmp_mib[22].value.integer
#define icmpOutTimestamps       Icmp_mib[23].value.integer
#define icmpOutTimestampReps    Icmp_mib[24].value.integer
#define icmpOutAddrMasks        Icmp_mib[25].value.integer
#define icmpOutAddrMaskReps     Icmp_mib[26].value.integer
#define NUMICMPMIB      26

/* Internet Control Message Protocol */

/* Message types */
#define ICMP_ECHO_REPLY         0       /* Echo Reply */
#define ICMP_DEST_UNREACH       3       /* Destination Unreachable */
#define ICMP_QUENCH             4       /* Source Quench */
#define ICMP_REDIRECT           5       /* Redirect */
#define ICMP_ECHO               8       /* Echo Request */
#define ICMP_TIME_EXCEED        11      /* Time-to-live Exceeded */
#define ICMP_PARAM_PROB         12      /* Parameter Problem */
#define ICMP_TIMESTAMP          13      /* Timestamp */
#define ICMP_TIME_REPLY         14      /* Timestamp Reply */
#define ICMP_INFO_RQST          15      /* Information Request */
#define ICMP_INFO_REPLY         16      /* Information Reply */
#define ICMP_ADDR_MASK          17      /* Address mask request */
#define ICMP_ADDR_MASK_REPLY    18      /* Address mask reply */
#define ICMP_TYPES              19

/* Internal format of an ICMP header (checksum is missing) */
struct icmp {
	char type;
	char code;
	union icmp_args {
		int32 unused;
		unsigned char pointer;
		int32 address;
		struct {
			int16 id;
			int16 seq;
		} echo;
	} args;
};
#define ICMPLEN         8       /* Length of ICMP header on the net */
#define NULLICMP        (union icmp_args *)0

/* Destination Unreachable codes */
#define ICMP_NET_UNREACH        0       /* Net unreachable */
#define ICMP_HOST_UNREACH       1       /* Host unreachable */
#define ICMP_PROT_UNREACH       2       /* Protocol unreachable */
#define ICMP_PORT_UNREACH       3       /* Port unreachable */
#define ICMP_FRAG_NEEDED        4       /* Fragmentation needed and DF set */
#define ICMP_ROUTE_FAIL         5       /* Source route failed */

#define NUNREACH        6

/* Time Exceeded codes */
#define ICMP_TTL_EXCEED         0       /* Time-to-live exceeded */
#define ICMP_FRAG_EXCEED        1       /* Fragment reassembly time exceeded */

#define NEXCEED         2

/* Redirect message codes */
#define ICMP_REDR_NET   0       /* Redirect for the network */
#define ICMP_REDR_HOST  1       /* Redirect for the host */
#define ICMP_REDR_TOS   2       /* Redirect for Type of Service, or-ed with prev */

#define NREDIRECT       3

struct ping {
	struct ping *next;      /* Linked list pointers */
	struct ping *prev;
	int32 target;           /* Starting target IP address */
	int32 sent;             /* Total number of pings sent */
	int32 srtt;             /* Smoothed round trip time */
	int32 mdev;             /* Mean deviation */
	int32 responses;        /* Total number of responses */
	struct timer timer;     /* Ping interval timer */
	int16 len;              /* Length of data portion of ping */
};
/* ICMP messages, decoded */
extern char *Icmptypes[],*Unreach[],*Exceed[],*Redirect[];

/* In icmp.c: */
void icmp_input __ARGS((struct mbuf *bp, int protocol, int32 source, int32 dest, int tos, int length, int rxbroadcast));
int icmp_output __ARGS((struct ip *ip, struct mbuf *data, int type, int code,
      union icmp_args *args));

/* In icmpcmd.c: */
void echo_proc __ARGS((int32 source,int32 dest,struct icmp *icmp,struct mbuf *bp));

/* In icmphdr.c: */
struct mbuf *htonicmp __ARGS((struct icmp *icmp,struct mbuf *data));
int ntohicmp __ARGS((struct icmp *icmp,struct mbuf **bpp));

/*** old stuff ***/

struct icmp_errors {
	unsigned checksum;              /* ICMP Checksum errors */
	unsigned nospace;               /* alloc_mbuf failed someplace */
	unsigned noloop;                /* No ICMP in response to an ICMP */
	unsigned bdcsts;                /* Ignore broadcast ICMPs */
};

struct icmp_stats {
	unsigned input[ICMP_TYPES];     /* ICMP input stats by type */
	unsigned output[ICMP_TYPES];    /* ICMP output stats by type */
};

/*** old stuff ***/

#endif  /* ECHO_REPLY */

