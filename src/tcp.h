/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcp.h,v 1.10 1992/11/25 12:29:37 deyke Exp $ */

#ifndef _TCP_H
#define _TCP_H

/* TCP implementation. Follows RFC 793 as closely as possible */
#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _INTERNET_H
#include "internet.h"
#endif

#ifndef _IP_H
#include "ip.h"
#endif

#ifndef _NETUSER_H
#include "netuser.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

#define DEF_MSS 512     /* Default maximum segment size */
#define DEF_WND 2048    /* Default receiver window */
/* #define RTTCACHE 16  /* # of TCP round-trip-time cache entries */
#define DEF_RTT 5000    /* Initial guess at round trip time (5 sec) */
#define MSL2    30      /* Guess at two maximum-segment lifetimes */
#define MIN_RTO 500L    /* Minimum timeout, milliseconds */
#define TCP_HDR_PAD     70      /* mbuf size to preallocate for headers */

#define geniss()        ((int32)msclock() << 12) /* Increment clock at 4 MB/sec */

/* Number of consecutive duplicate acks to trigger fast recovery */
#define TCPDUPACKS      3

/* Round trip timing parameters */
#define AGAIN   8       /* Average RTT gain = 1/8 */
#define LAGAIN  3       /* Log2(AGAIN) */
#define DGAIN   4       /* Mean deviation gain = 1/4 */
#define LDGAIN  2       /* log2(DGAIN) */

/* TCP segment header -- internal representation
 * Note that this structure is NOT the actual header as it appears on the
 * network (in particular, the offset field is missing).
 * All that knowledge is in the functions ntohtcp() and htontcp() in tcpsubr.c
 */
#define TCPLEN          20      /* Minimum Header length, bytes */
#define TCP_MAXOPT      40      /* Largest option field, bytes */
struct tcp {
	int16 source;   /* Source port */
	int16 dest;     /* Destination port */
	int32 seq;      /* Sequence number */
	int32 ack;      /* Acknowledgment number */
	int16 wnd;                      /* Receiver flow control window */
	int16 checksum;                 /* Checksum */
	int16 up;                       /* Urgent pointer */
	int16 mss;                      /* Optional max seg size */
	struct {
		char congest;   /* Echoed IP congestion experienced bit */
		char urg;
		char ack;
		char psh;
		char rst;
		char syn;
		char fin;
	} flags;
	char optlen;                    /* Length of options field, bytes */
	char options[TCP_MAXOPT];       /* Options field */
};
/* TCP options */
#define EOL_KIND        0
#define NOOP_KIND       1
#define MSS_KIND        2
#define MSS_LENGTH      4

/* Resequencing queue entry */
struct reseq {
	struct reseq *next;     /* Linked-list pointer */
	struct tcp seg;         /* TCP header */
	struct mbuf *bp;        /* data */
	int16 length;           /* data length */
	char tos;               /* Type of service */
};
#define NULLRESEQ       (struct reseq *)0

/* TCP connection control block */
struct tcb {
	struct tcb *next;       /* Linked list pointer */

	struct connection conn;

	char state;     /* Connection state */

/* These numbers match those defined in the MIB for TCP connection state */
#define TCP_CLOSED              1
#define TCP_LISTEN              2
#define TCP_SYN_SENT            3
#define TCP_SYN_RECEIVED        4
#define TCP_ESTABLISHED         5
#define TCP_FINWAIT1            6
#define TCP_FINWAIT2            7
#define TCP_CLOSE_WAIT          8
#define TCP_LAST_ACK            9
#define TCP_CLOSING             10
#define TCP_TIME_WAIT           11

	char reason;            /* Reason for closing */
#define NORMAL          0       /* Normal close */
#define RESET           1       /* Reset by other end */
#define TIMEOUT         2       /* Excessive retransmissions */
#define NETWORK         3       /* Network problem (ICMP message) */

/* If reason == NETWORK, the ICMP type and code values are stored here */
	char type;
	char code;

	/* Send sequence variables */
	struct {
		int32 una;      /* First unacknowledged sequence number */
		int32 nxt;      /* Next sequence num to be sent for the first time */
		int32 ptr;      /* Working transmission pointer */
		int32 wl1;      /* Sequence number used for last window update */
		int32 wl2;      /* Ack number used for last window update */
		int16 wnd;      /* Other end's offered receive window */
		int16 up;       /* Send urgent pointer */
	} snd;
	int32 iss;              /* Initial send sequence number */
	int32 resent;           /* Count of bytes retransmitted */
	int16 cwind;            /* Congestion window */
	int16 ssthresh;         /* Slow-start threshold */
	int dupacks;            /* Count of duplicate (do-nothing) ACKs */

	/* Receive sequence variables */
	struct {
		int32 nxt;      /* Incoming sequence number expected next */
		int16 wnd;      /* Our offered receive window */
		int16 up;       /* Receive urgent pointer */
	} rcv;
	int32 irs;              /* Initial receive sequence number */
	int32 rerecv;           /* Count of duplicate bytes received */
	int16 mss;              /* Maximum segment size */

	int16 window;           /* Receiver window and send queue limit */
	int16 limit;            /* Send queue limit */

	void (*r_upcall) __ARGS((struct tcb *tcb,int cnt));
		/* Call when "significant" amount of data arrives */
	void (*t_upcall) __ARGS((struct tcb *tcb,int cnt));
		/* Call when ok to send more data */
	void (*s_upcall) __ARGS((struct tcb *tcb,int old,int new));
		/* Call when connection state changes */
	struct {                /* Control flags */
		char force;     /* We owe the other end an ACK or window update */
		char clone;     /* Server-type TCB, cloned on incoming SYN */
		char retran;    /* A retransmission has occurred */
		char active;    /* TCB created with an active open */
		char synack;    /* Our SYN has been acked */
		char rtt_run;   /* We're timing a segment */
		char congest;   /* Copy of last IP congest bit received */
	} flags;
	char tos;               /* Type of service (for IP) */
	int backoff;            /* Backoff interval */

	struct mbuf *rcvq;      /* Receive queue */
	struct mbuf *sndq;      /* Send queue */
	int16 rcvcnt;           /* Count of items on rcvq */
	int16 sndcnt;           /* Number of unacknowledged sequence numbers on
				 * sndq. NB: includes SYN and FIN, which don't
				 * actually appear on sndq!
				 */

	struct reseq *reseq;    /* Out-of-order segment queue */
	struct timer timer;     /* Retransmission timer */
	int32 rtt_time;         /* Stored clock values for RTT */
	int32 rttseq;           /* Sequence number being timed */
	int32 srtt;             /* Smoothed round trip time, milliseconds */
	int32 mdev;             /* Mean deviation, milliseconds */
	int32 lastactive;       /* Clock time when xmtr last active */

	int user;               /* User parameter (e.g., for mapping to an
				 * application control block
				 */
};
#define NULLTCB (struct tcb *)0
/* TCP round-trip time cache */
struct tcp_rtt {
	int32 addr;             /* Destination IP address */
	int32 srtt;             /* Most recent SRTT */
	int32 mdev;             /* Most recent mean deviation */
	struct tcp_rtt *next;   /* Linked-list pointer */
};
#define NULLRTT (struct tcp_rtt *)0
extern struct tcp_rtt *Tcp_rtt;

/* TCP statistics counters */
struct tcp_stat {
	int16 runt;             /* Smaller than minimum size */
	int16 checksum;         /* TCP header checksum errors */
	int16 conout;           /* Outgoing connection attempts */
	int16 conin;            /* Incoming connection attempts */
	int16 resets;           /* Resets generated */
	int16 bdcsts;           /* Bogus broadcast packets */
};
extern struct mib_entry Tcp_mib[];
#define tcpRtoAlgorithm Tcp_mib[1].value.integer
#define tcpRtoMin       Tcp_mib[2].value.integer
#define tcpRtoMax       Tcp_mib[3].value.integer
#define tcpMaxConn      Tcp_mib[4].value.integer
#define tcpActiveOpens  Tcp_mib[5].value.integer
#define tcpPassiveOpens Tcp_mib[6].value.integer
#define tcpAttemptFails Tcp_mib[7].value.integer
#define tcpEstabResets  Tcp_mib[8].value.integer
#define tcpCurrEstab    Tcp_mib[9].value.integer
#define tcpInSegs       Tcp_mib[10].value.integer
#define tcpOutSegs      Tcp_mib[11].value.integer
#define tcpRetransSegs  Tcp_mib[12].value.integer
#define tcpInErrs       Tcp_mib[14].value.integer
#define tcpOutRsts      Tcp_mib[15].value.integer
#define NUMTCPMIB       15

extern struct tcb *Tcbs;
extern char *Tcpstates[];
extern char *Tcpreasons[];

/* In tcpcmd.c: */
extern int32 Tcp_irtt;
extern int16 Tcp_limit;
extern int16 Tcp_mss;
extern int Tcp_syndata;
extern int Tcp_trace;
extern int16 Tcp_window;

void st_tcp __ARGS((struct tcb *tcb));

/* In tcphdr.c: */
struct mbuf *htontcp __ARGS((struct tcp *tcph,struct mbuf *data,
	struct pseudo_header *ph));
int ntohtcp __ARGS((struct tcp *tcph,struct mbuf **bpp));

/* In tcpin.c: */
void reset __ARGS((struct ip *ip,struct tcp *seg));
void send_syn __ARGS((struct tcb *tcb));
void tcp_input __ARGS((struct iface *iface,struct ip *ip,struct mbuf *bp,
	int rxbroadcast));
void tcp_icmp __ARGS((int32 icsource,int32 source,int32 dest,
	int type,int code,struct mbuf **bpp));

/* In tcpsubr.c: */
void close_self __ARGS((struct tcb *tcb,int reason));
struct tcb *create_tcb __ARGS((struct connection *conn));
struct tcb *lookup_tcb __ARGS((struct connection *conn));
void rtt_add __ARGS((int32 addr,int32 rtt));
struct tcp_rtt *rtt_get __ARGS((int32 addr));
int seq_ge __ARGS((int32 x,int32 y));
int seq_gt __ARGS((int32 x,int32 y));
int seq_le __ARGS((int32 x,int32 y));
int seq_lt __ARGS((int32 x,int32 y));
int seq_within __ARGS((int32 x,int32 low,int32 high));
#define setstate set_state /* setstate is used in BSD */
void setstate __ARGS((struct tcb *tcb,int newstate));
void tcp_garbage __ARGS((int red));

/* In tcpout.c: */
void tcp_output __ARGS((struct tcb *tcb));

/* In tcptimer.c: */
int32 backoff __ARGS((int n));
void tcp_timeout __ARGS((void *p));

/* In tcpuser.c: */
int close_tcp __ARGS((struct tcb *tcb));
int del_tcp __ARGS((struct tcb *tcb));
int kick __ARGS((int32 addr));
int kick_tcp __ARGS((struct tcb *tcb));
struct tcb *open_tcp __ARGS((struct socket *lsocket,struct socket *fsocket,
	int mode,int window,
	void (*r_upcall) __ARGS((struct tcb *tcb,int cnt)),
	void (*t_upcall) __ARGS((struct tcb *tcb,int cnt)),
	void (*s_upcall) __ARGS((struct tcb *tcb,int old,int new)),
	int tos,int user));
int recv_tcp __ARGS((struct tcb *tcb,struct mbuf **bpp,int cnt));
void reset_all __ARGS((void));
void reset_tcp __ARGS((struct tcb *tcb));
int send_tcp __ARGS((struct tcb *tcb,struct mbuf *bp));
int space_tcp __ARGS((struct tcb *tcb));
char *tcp_port __ARGS((int n));
int tcpval __ARGS((struct tcb *tcb));

/* In tcpsocket.c: */
int so_tcp __ARGS((struct usock *up,int protocol));
int so_tcp_listen __ARGS((struct usock *up,int backlog));
int so_tcp_conn __ARGS((struct usock *up));
int so_tcp_recv __ARGS((struct usock *up,struct mbuf **bpp,char *from,
	int *fromlen));
int so_tcp_send __ARGS((struct usock *up,struct mbuf *bp,char *to));
int so_tcp_qlen __ARGS((struct usock *up,int rtx));
int so_tcp_kick __ARGS((struct usock *up));
int so_tcp_shut __ARGS((struct usock *up,int how));
int so_tcp_close __ARGS((struct usock *up));
char *tcpstate __ARGS((struct usock *up));
int so_tcp_stat __ARGS((struct usock *up));

#endif  /* _TCP_H */
