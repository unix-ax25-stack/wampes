/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapb.h,v 1.9 1991/03/28 19:39:45 deyke Exp $ */

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

/* Round trip timing parameters */
#define AGAIN   8               /* Average RTT gain = 1/8 */
#define DGAIN   4               /* Mean deviation gain = 1/4 */

/* AX25 connection control block */
struct ax25_cb {
  struct ax25 hdr;              /* AX25 header */
  struct iface *ifp;            /* Pointer to interface structure */
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
  int  mode;                    /* Connection mode */
#define STREAM        0
#define DGRAM         1
  int  closed;                  /* Disconnect when send queue empty */
  int  polling;                 /* Poll frame has been sent */
  int  rnrsent;                 /* RNR frame has been sent */
  int  rejsent;                 /* REJ frame has been sent */
  int32 remote_busy;            /* Other end's window is closed */
  int  vr;                      /* Incoming sequence number expected next */
  int  vs;                      /* Next sequence number to be sent */
  int  cwind;                   /* Congestion window */
  int  retry;                   /* Retransmission retry count */
  int32 srtt;                   /* Smoothed round trip time, milliseconds */
  int32 mdev;                   /* Mean deviation, milliseconds */
  struct timer timer_t1;        /* Retransmission timer */
  struct timer timer_t2;        /* Acknowledgement delay timer */
  struct timer timer_t3;        /* No-activity timer */
  struct timer timer_t4;        /* Busy timer */
  struct timer timer_t5;        /* Packet assembly timer */
  struct axreseq {              /* Resequencing queue */
    struct mbuf *bp;
    int  sum;
  } reseq[8];
  struct mbuf *rcvq;            /* Receive queue */
  int16 rcvcnt;                 /* Receive queue length */
  struct mbuf *sndq;            /* Send queue */
  int32 sndqtime;               /* Last send queue write time */
  struct mbuf *resndq;          /* Resend queue */
  int  unack;                   /* Number of unacked frames */
  int32 sndtime[8];             /* Time of 1st transmission */
  void (*r_upcall) __ARGS((struct ax25_cb *p, int cnt));
				/* Call when data arrives */
  void (*t_upcall) __ARGS((struct ax25_cb *p, int cnt));
				/* Call when ok to send more data */
  void (*s_upcall) __ARGS((struct ax25_cb *p, int oldstate, int newstate));
				/* Call when connection state changes */
  char  *user;                  /* User parameter (e.g., for mapping to an
				 * application control block)
				 */
  struct ax25_cb *peer;         /* Pointer to peer's control block */
  struct ax25_cb *next;         /* Linked-list pointer */
};

#define NULLAXCB ((struct ax25_cb *) 0)

extern char  *ax25reasons[];            /* Reason names */
extern char  *ax25states[];             /* State names */
extern int  ax_maxframe;                /* Transmit flow control level */
extern int  ax_paclen;                  /* Maximum outbound packet size */
extern int  ax_pthresh;                 /* Send polls for packets larger than this */
extern int  ax_retry;                   /* Retry limit */
extern int32 ax_t1init;                 /* Retransmission timeout */
extern int32 ax_t2init;                 /* Acknowledgement delay timeout */
extern int32 ax_t3init;                 /* No-activity timeout */
extern int32 ax_t4init;                 /* Busy timeout */
extern int32 ax_t5init;                 /* Packet assembly timeout */
extern int  ax_window;                  /* Local flow control limit */
extern struct ax25_cb *axcb_server;     /* Server control block */

/* In lapb.c: */
int lapb_input __ARGS((struct iface *iface, struct ax25 *hdr, struct mbuf *bp));
int doax25 __ARGS((int argc, char *argv [], void *p));
struct ax25_cb *open_ax __ARGS((char *path, int mode, void (*r_upcall )__ARGS ((struct ax25_cb *p, int cnt )), void (*t_upcall )__ARGS ((struct ax25_cb *p, int cnt )), void (*s_upcall )__ARGS ((struct ax25_cb *p, int oldstate, int newstate )), char *user));
int send_ax __ARGS((struct ax25_cb *cp, struct mbuf *bp));
int space_ax __ARGS((struct ax25_cb *cp));
int recv_ax __ARGS((struct ax25_cb *cp, struct mbuf **bpp, int cnt));
int close_ax __ARGS((struct ax25_cb *cp));
int reset_ax __ARGS((struct ax25_cb *cp));
int del_ax __ARGS((struct ax25_cb *cp));
int valid_ax __ARGS((struct ax25_cb *cp));

#endif  /* _LAPB_H */
