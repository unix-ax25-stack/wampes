/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/netrom.h,v 1.13 1992/01/12 18:40:16 deyke Exp $ */

#ifndef _NETROM_H
#define _NETROM_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _AX25_H
#include "ax25.h"
#endif

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

#define NR3HLEN         15      /* length of a net/rom level 3 hdr, */
#define NR3DLEN         241     /* max data size in net/rom l3 packet */
#define NR3NODESIG      0xff    /* signature for nodes broadcast */
#define NR3NODEHL       7       /* nodes bc header length */

#define NRRTDESTLEN     21      /* length of destination entry in */
				/* nodes broadcast */
#define NRDESTPERPACK   11      /* maximum number of destinations per */
				/* nodes packet */

/* NET/ROM protocol numbers */
#define NRPROTO_IP      0x0c

/* Round trip timing parameters */
#define AGAIN   8               /* Average RTT gain = 1/8 */
#define DGAIN   4               /* Mean deviation gain = 1/4 */

#define NR4MAXINFO      236     /* Maximum data in an info packet */
#define NR4MINHDR       5       /* Minimum length of NET/ROM transport header */

#define NR4OPPID        0       /* Protocol ID extension to network layer */
#define NR4OPCONRQ      1       /* Connect request */
#define NR4OPCONAK      2       /* Connect acknowledge */
#define NR4OPDISRQ      3       /* Disconnect request */
#define NR4OPDISAK      4       /* Disconnect acknowledge */
#define NR4OPINFO       5       /* Information packet */
#define NR4OPACK        6       /* Information acknowledge */
#define NR4OPCODE       0x0f    /* Mask for opcode nybble */
#define NR4MORE         0x20    /* MORE bit */
#define NR4NAK          0x40    /* NAK bit */
#define NR4CHOKE        0x80    /* CHOKE bit */

struct circuit {
  int  localindex;              /* Local circuit index */
  int  localid;                 /* Local circuit ID */
  int  remoteindex;             /* Remote circuit index */
  int  remoteid;                /* Remote circuit ID */
  int  outbound;                /* Circuit was created by local request */
  char  node[AXALEN];           /* Call of peer node */
  char  cuser[AXALEN];          /* Call of user */
  int  state;                   /* Connection state */
#define DISCONNECTED  0
#define CONNECTING    1
#define CONNECTED     2
#define DISCONNECTING 3
  int  reason;                  /* Reason for disconnecting */
#define NORMAL        0         /* Normal disconnect */
#define RESET         1         /* Disconnected by other end */
#define TIMEOUT       2         /* Excessive retransmissions */
#define NETWORK       3         /* Network problem */
  int  window;                  /* Negotiated window size */
  int  naksent;                 /* NAK has been sent */
  int  chokesent;               /* CHOKE has been sent */
  int  closed;                  /* Disconnect when send queue empty */
  int  recv_state;              /* Incoming sequence number expected next */
  int  send_state;              /* Next sequence number to be sent */
  int  cwind;                   /* Congestion window */
  int32 remote_busy;            /* Other end's window is closed */
  int  retry;                   /* Retransmission retry count */
  int32 srtt;                   /* Smoothed round trip time, milliseconds */
  int32 mdev;                   /* Mean deviation, milliseconds */
  struct timer timer_t1;        /* Retransmission timer */
  struct timer timer_t2;        /* Acknowledgement delay timer */
  struct timer timer_t3;        /* No-activity timer */
  struct timer timer_t4;        /* Busy timer */
  struct timer timer_t5;        /* Packet assembly timer */
  struct mbuf *reseq;           /* Resequencing queue */
  struct mbuf *rcvq;            /* Receive queue */
  int16 rcvcnt;                 /* Receive queue length */
  struct mbuf *sndq;            /* Send queue */
  int32 sndqtime;               /* Last send queue write time */
  struct mbuf *resndq;          /* Resend queue */
  int  unack;                   /* Number of unacked frames */
  int32 sndtime[256];           /* Time of 1st transmission */
  void (*r_upcall) __ARGS((struct circuit *p, int cnt));
				/* Call when data arrives */
  void (*t_upcall) __ARGS((struct circuit *p, int cnt));
				/* Call when ok to send more data */
  void (*s_upcall) __ARGS((struct circuit *p, int oldstate, int newstate));
				/* Call when connection state changes */
  char  *user;                  /* User parameter (e.g., for mapping to an
				 * application control block)
				 */
  struct circuit *next;         /* Linked-list pointer */
};

/* In netrom.c: */
int isnetrom __ARGS((char *call));
void new_neighbor __ARGS((char *call));
int nr_send __ARGS((struct mbuf *bp, struct iface *iface, int32 gateway, int prec, int del, int tput, int rel));
void nr3_input __ARGS((struct mbuf *bp, char *fromcall));
char *nr_addr2str __ARGS((struct circuit *pc));
struct circuit *open_nr __ARGS((char *node, char *cuser, int window, void (*r_upcall )__ARGS ((struct circuit *p, int cnt )), void (*t_upcall )__ARGS ((struct circuit *p, int cnt )), void (*s_upcall )__ARGS ((struct circuit *p, int oldstate, int newstate )), char *user));
int send_nr __ARGS((struct circuit *pc, struct mbuf *bp));
int space_nr __ARGS((struct circuit *pc));
int recv_nr __ARGS((struct circuit *pc, struct mbuf **bpp, int cnt));
int close_nr __ARGS((struct circuit *pc));
int reset_nr __ARGS((struct circuit *pc));
int del_nr __ARGS((struct circuit *pc));
int valid_nr __ARGS((struct circuit *pc));
int kick_nr __ARGS((struct circuit *pc));
void nrclient_send_upcall __ARGS((struct circuit *pc, int cnt));
void nrclient_recv_upcall __ARGS((struct circuit *pc, int cnt));
int nr_attach __ARGS((int argc, char *argv [], void *p));
int donetrom __ARGS((int argc, char *argv [], void *p));
int nr4start __ARGS((int argc, char *argv [], void *p));
int nr40 __ARGS((int argc, char *argv [], void *p));
void netrom_initialize __ARGS((void));

#endif  /* _NETROM_H */
