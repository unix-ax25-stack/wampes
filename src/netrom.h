/* @(#) $Id: netrom.h,v 1.23 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _NETROM_H
#define _NETROM_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _AX25_H
#include "ax25.h"
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
#define NRAGAIN         8       /* Average RTT gain = 1/8 */
#define NRDGAIN         4       /* Mean deviation gain = 1/4 */

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

enum netrom_state {
  NR4STDISC,                    /* Disconnected */
  NR4STCPEND,                   /* Connection pending */
  NR4STCON,                     /* Connected */
  NR4STDPEND,                   /* Disconnect requested locally */
  NR4STLISTEN                   /* Listening for incoming connections */
};

enum netrom_reason {
  NR4RNORMAL,                   /* Normal, requested disconnect */
  NR4RREMOTE,                   /* Remote requested */
  NR4RTIMEOUT,                  /* Connection timed out */
  NR4RRESET,                    /* Connection reset locally */
  NR4RREFUSED                   /* Connect request refused */
};

struct circuit {
  int localindex;               /* Local circuit index */
  int localid;                  /* Local circuit ID */
  int remoteindex;              /* Remote circuit index */
  int remoteid;                 /* Remote circuit ID */
  unsigned int outbound:1;      /* Circuit was created by local request */
  uint8 node[AXALEN];           /* Call of peer node */
  uint8 cuser[AXALEN];          /* Call of user */
  enum netrom_state state;      /* Connection state */
  enum netrom_reason reason;    /* Reason for disconnecting */
  int window;                   /* Negotiated window size */
  unsigned int naksent:1;       /* NAK has been sent */
  unsigned int chokesent:1;     /* CHOKE has been sent */
  unsigned int closed:1;        /* Disconnect when send queue empty */
  int recv_state;               /* Incoming sequence number expected next */
  int send_state;               /* Next sequence number to be sent */
  int cwind;                    /* Congestion window */
  unsigned int response:1;      /* Response owed to other end */
  int32 remote_busy;            /* Other end's window is closed */
  int retry;                    /* Retransmission retry count */
  int32 srtt;                   /* Smoothed round trip time, milliseconds */
  int32 mdev;                   /* Mean deviation, milliseconds */
  struct timer timer_t1;        /* Retransmission timer */
  struct timer timer_t3;        /* No-activity timer */
  struct timer timer_t4;        /* Busy timer */
  struct mbuf *reseq;           /* Resequencing queue */
  struct mbuf *rcvq;            /* Receive queue */
  uint16 rcvcnt;                /* Receive queue length */
  struct mbuf *sndq;            /* Send queue */
  struct mbuf *resndq;          /* Resend queue */
  int unack;                    /* Number of unacked frames */
  int32 sndtime[256];           /* Time of 1st transmission */
  void (*r_upcall)(struct circuit *p, int cnt);
				/* Call when data arrives */
  void (*t_upcall)(struct circuit *p, int cnt);
				/* Call when ok to send more data */
  void (*s_upcall)(struct circuit *p, enum netrom_state oldstate, enum netrom_state newstate);
				/* Call when connection state changes */
  char *user;                   /* User parameter (e.g., for mapping to an
				 * application control block)
				 */
  struct circuit *next;         /* Linked-list pointer */
};

extern char *Nr4states[];

/* In netrom.c: */
int nr_send(struct mbuf **bpp, struct iface *iface, int32 gateway, uint8 tos);
void nr3_input(const uint8 *src, struct mbuf **bpp);
char *nr_addr2str(struct circuit *pc);
struct circuit *open_nr(uint8 *node, uint8 *cuser, int window, void (*r_upcall)(struct circuit *p, int cnt), void (*t_upcall)(struct circuit *p, int cnt), void (*s_upcall)(struct circuit *p, enum netrom_state oldstate, enum netrom_state newstate), char *user);
int send_nr(struct circuit *pc, struct mbuf **bpp);
int space_nr(struct circuit *pc);
int recv_nr(struct circuit *pc, struct mbuf **bpp, int cnt);
int close_nr(struct circuit *pc);
int reset_nr(struct circuit *pc);
int del_nr(struct circuit *pc);
int valid_nr(struct circuit *pc);
int kick_nr(struct circuit *pc);
void nrclient_send_upcall(struct circuit *pc, int cnt);
void nrclient_recv_upcall(struct circuit *pc, int cnt);
int nr_attach(int argc, char *argv [], void *p);
int donetrom(int argc, char *argv [], void *p);
int nr4start(int argc, char *argv [], void *p);
int nr40(int argc, char *argv [], void *p);
void netrom_initialize(void);

#endif  /* _NETROM_H */
