/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapb.h,v 1.12 1993/01/29 06:48:29 deyke Exp $ */

#ifndef _LAPB_H
#define _LAPB_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

#ifndef _AX25_H
#include "ax25.h"
#endif

/* Upper sub-layer (LAPB) definitions */

/* Control field templates */
#define I       0x00    /* Information frames */
#define S       0x01    /* Supervisory frames */
#define RR      0x01    /* Receiver ready */
#define RNR     0x05    /* Receiver not ready */
#define REJ     0x09    /* Reject */
#define U       0x03    /* Unnumbered frames */
#define SABM    0x2f    /* Set Asynchronous Balanced Mode */
#define DISC    0x43    /* Disconnect */
#define DM      0x0f    /* Disconnected mode */
#define UA      0x63    /* Unnumbered acknowledge */
#define FRMR    0x87    /* Frame reject */
#define UI      0x03    /* Unnumbered information */
#define PF      0x10    /* Poll/final bit */

#define MMASK   7       /* Mask for modulo-8 sequence numbers */

/* FRMR reason bits */
#define W       1       /* Invalid control field */
#define X       2       /* Unallowed I-field */
#define Y       4       /* Too-long I-field */
#define Z       8       /* Invalid sequence number */

/* AX25 protocol constants */
#define DST_C   1               /* Set C bit in dest addr */
#define SRC_C   2               /* Set C bit in source addr */
#define VERS1   0               /* AX25L2V1 compatible frame */
#define CMD     (DST_C)         /* Command frame */
#define POLL    (DST_C|PF)      /* Poll frame */
#define RESP    (SRC_C)         /* Response frame */
#define FINAL   (SRC_C|PF)      /* Final frame */

/* Per-connection link control block
 * These are created and destroyed dynamically,
 * and are indexed through a hash table.
 * One exists for each logical AX.25 Level 2 connection
 */
struct ax25_cb {
	struct ax25_cb *next;           /* Linked list pointer */

	struct iface *iface;            /* Interface */

	struct mbuf *sndq;              /* Transmit queue */
	int32 sndqtime;                 /* Last send queue write time */
	struct mbuf *txq;               /* Transmit queue */
	struct axreseq {                /* Resequencing queue */
		struct mbuf *bp;
		int sum;
	} reseq[8];
	struct mbuf *rxq;               /* Receive queue */
	int16 rcvcnt;                   /* Receive queue length */

	struct ax25 hdr;                /* AX25 header */

	struct {
		char rejsent;           /* REJ frame has been sent */
		int32 remotebusy;       /* Remote sent RNR */
		char rtt_run;           /* Round trip "timer" is running */
		char retrans;           /* A retransmission has occurred */
/*              char clone;             /* Server-type cb, will be cloned */
		char closed;            /* Disconnect when send queue empty */
		char polling;           /* Poll frame has been sent */
		char rnrsent;           /* RNR frame has been sent */
	} flags;

	char reason;                    /* Reason for connection closing */
#define LB_NORMAL       0               /* Normal close */
#define LB_DM           1               /* Received DM from other end */
#define LB_TIMEOUT      2               /* Excessive retries */

	char vs;                        /* Our send state variable */
	char vr;                        /* Our receive state variable */
	char unack;                     /* Number of unacked frames */
	unsigned retries;               /* Retry counter */
	int state;                      /* Link state */
#define LAPB_DISCONNECTED       1
#define LAPB_LISTEN             2
#define LAPB_SETUP              3
#define LAPB_DISCPENDING        4
#define LAPB_CONNECTED          5
/* #define LAPB_RECOVERY        6          not used */
	struct timer t1;                /* Retry timer */
	struct timer t2;                /* Acknowledgement delay timer */
	struct timer t3;                /* Keep-alive poll timer */
	struct timer t4;                /* Busy timer */
	struct timer t5;                /* Packet assembly timer */
	int32 rtt_time;                 /* Stored clock values for RTT, ms */
	int rtt_seq;                    /* Sequence number being timed */
	int32 srt;                      /* Smoothed round-trip time, ms */
	int32 mdev;                     /* Mean rtt deviation, ms */

	void (*r_upcall) __ARGS((struct ax25_cb *,int));        /* Receiver upcall */
	void (*t_upcall) __ARGS((struct ax25_cb *,int));        /* Transmit upcall */
	void (*s_upcall) __ARGS((struct ax25_cb *,int,int));    /* State change upcall */

	char *user;                     /* User pointer */

	int routing_changes;            /* Number of routing changes */
	int mode;                       /* Connection mode */
#define STREAM        0
#define DGRAM         1
	int cwind;                      /* Congestion window */
	struct ax25_cb *peer;           /* Pointer to peer's control block */
};
#define NULLAX25        ((struct ax25_cb *)0)
extern struct ax25_cb Ax25default,*Ax25_cb;
extern char *Ax25states[],*Axreasons[];
extern int Axirtt,T3init,Blimit;
extern int N2,Maxframe,Paclen,Pthresh,Axwindow,Axversion;

extern int T1init;                      /* Retransmission timeout */
extern int T2init;                      /* Acknowledgement delay timeout */
extern int T4init;                      /* Busy timeout */
extern int T5init;                      /* Packet assembly timeout */
extern struct ax25_cb *Axcb_server;     /* Server control block */

/* In lapb.c: */
int lapb_input __ARGS((struct iface *iface, struct ax25 *hdr, struct mbuf *bp));
void send_packet __ARGS((struct ax25_cb *cp, int type, int cmdrsp, struct mbuf *data));
int busy __ARGS((struct ax25_cb *cp));
void send_ack __ARGS((struct ax25_cb *cp, int cmdrsp));
int lapb_output __ARGS((struct ax25_cb *cp, int fill_sndq));
void lapbstate __ARGS((struct ax25_cb *axp, int s));
void t2_timeout __ARGS((struct ax25_cb *cp));
void pollthem __ARGS((void *p));
void t4_timeout __ARGS((struct ax25_cb *cp));
void t5_timeout __ARGS((struct ax25_cb *cp));
void build_path __ARGS((struct ax25_cb *cp, struct iface *ifp, struct ax25 *hdr, int reverse));

/* In lapbtime.c: */
void pollthem __ARGS((void *p));
void recover __ARGS((void *p));

/* In ax25subr.c: */
int16 ftype __ARGS((int control));
void lapb_garbage __ARGS((int drastic));

#endif  /* _LAPB_H */
