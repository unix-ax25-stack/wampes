/* @(#) $Id: lapb.h,v 1.27 2005/03/11 14:36:09 dl9sau Exp $ */

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
#define SABME   0x6f    /* Set Asynchronous Balanced Mode moduluo-128 */
#define DISC    0x43    /* Disconnect */
#define DM      0x0f    /* Disconnected mode */
#define UA      0x63    /* Unnumbered acknowledge */
#define FRMR    0x87    /* Frame reject */
#define UI      0x03    /* Unnumbered information */
#define PF      0x10    /* Poll/final bit */
#define PF_EAX25 0x01   /* Poll/final bit in extra eax25 byte */


#define MMASK   7       /* Mask for modulo-8 sequence numbers */
#define EMMASK  0x7f    /* Mask for modulo-128 sequence numbers */

/* FRMR reason bits */
#define W       1       /* Invalid control field */
#define X       2       /* Unallowed I-field */
#define Y       4       /* Too-long I-field */
#define Z       8       /* Invalid sequence number */

#define SEG_FIRST       0x80    /* First segment of a sequence */
#define SEG_REM         0x7f    /* Mask for # segments remaining */

/* Receive resequence buffer */
struct axreseq {
	struct mbuf *bp;
	int sum;
};

enum lapb_version {
	V1=1,                   /* AX.25 Version 1 */
	V2                      /* AX.25 Version 2 */
};

enum lapb_state {
	LAPB_DISCONNECTED=1,
	LAPB_LISTEN,
	LAPB_SETUP,
	LAPB_DISCPENDING,
	LAPB_CONNECTED,
	LAPB_RECOVERY
};

/* Per-connection link control block
 * These are created and destroyed dynamically,
 * and are indexed through a hash table.
 * One exists for each logical AX.25 Level 2 connection
 */
/* One consumer of one protocol id on a link.
 *
 * Its own receive queue, because a link may carry several protocols at once
 * and a consumer that is not reading must not hold back another's data - the
 * bytes would otherwise lie mixed in one buffer with nothing to tell them
 * apart, since what comes out of a queue is a stream and not records.
 *
 * What they do share is the link: one window, one RNR.  AX.25 cannot say
 * "not ready for this pid", so a slow consumer does hold up the others in the
 * end.  Whoever needs them apart gives them separate SSIDs.
 */

struct axservice {
	struct axservice *next;
	struct ax25_cb *axp;            /* the link it belongs to */
	int pid;
	struct mbuf *rxq;
	void (*r_upcall)(struct axservice *sp,int cnt);
	void (*t_upcall)(struct axservice *sp,int cnt);
	void (*s_upcall)(struct axservice *sp,enum lapb_state old,
		enum lapb_state new);
	void *user;
};

struct ax25_cb {
	struct ax25_cb *next;           /* Linked list pointer */

	struct iface *iface;            /* Interface */

	struct mbuf *txq;               /* Transmit queue */
	struct axreseq reseq[8];        /* Receive resequence buffer */
	struct mbuf *rxasm;             /* Receive reassembly buffer */
	struct axservice *services;     /* Consumers, one per protocol id,
					 * each with a queue of its own */

	struct ax25 hdr;                /* AX25 header */

	struct {
		int32 remotebusy;               /* Remote sent RNR */
		unsigned int rejsent:1;         /* REJ frame has been sent */
		unsigned int rtt_run:1;         /* Round trip "timer" is running */
		unsigned int retrans:1;         /* A retransmission has occurred */
		unsigned int clone:1;           /* Server-type cb, will be cloned */
		unsigned int closed:1;          /* Disconnect when transmit queue empty */
		unsigned int rnrsent:1;         /* RNR frame has been sent */
	} flags;

	uint8 reason;                   /* Reason for connection closing */
#define LB_NORMAL       0               /* Normal close */
#define LB_DM           1               /* Received DM from other end */
#define LB_TIMEOUT      2               /* Excessive retries */

/*      uint8 response;                    Response owed to other end */
	uint8 vs;                       /* Our send state variable */
	uint8 vr;                       /* Our receive state variable */
	uint8 unack;                    /* Number of unacked frames */
	int maxframe;                   /* Transmit flow control level, frames */
	uint paclen;                    /* Maximum outbound packet size, bytes */
	uint window;                    /* Local flow control limit, bytes */
	enum lapb_version proto;        /* Protocol version */
	uint pthresh;                   /* Poll threshold, bytes */
	unsigned retries;               /* Retry counter */
	unsigned n2;                    /* Retry limit */
	enum lapb_state state;          /* Link state */
	struct timer t1;                /* Retry timer */
	struct timer t2;                /* Acknowledge delay timer */
	struct timer t3;                /* Idle poll timer */
	struct timer t4;                /* Busy timer */
	struct timer t5;                /* Idle disconnect timer */
	int32 rtt_time;                 /* Stored clock values for RTT, ms */
	int rtt_seq;                    /* Sequence number being timed */
	int32 srt;                      /* Smoothed round-trip time, ms */
	int32 mdev;                     /* Mean rtt deviation, ms */

					/* State change upcall */


	int segremain;                  /* Segmenter state */
	int routing_changes;            /* Number of routing changes */
	struct ax25_cb *peer;           /* Pointer to peer's control block */
	int id;                         /* Control block ID */
#ifdef	AX25_VJCOMP
	struct slcompress *slcomp;      /* MW: TCP header compression table */
	int slcomp_enable;              /* MW: compression enable flag */
#endif
};
/* Linkage to network protocols atop ax25 */
struct axlink {
	int pid;
	void (*funct)(struct iface *,struct ax25_cb *,uint8 *, uint8 *,
	 struct mbuf **,int);
};
extern struct axlink Axlink[];

/* Codes for the open_ax25 call */
#define AX_PASSIVE      0
#define AX_ACTIVE       1
#define AX_SERVER       2       /* Passive, clone on opening */

extern struct ax25_cb Ax25default,*Ax25_cb;
extern char *Ax25states[],*Axreasons[];
extern int32 Axirtt,Blimit;
extern int N2,Maxframe,Paclen,Pthresh,Axwindow;
extern enum lapb_version Axversion;

extern int T1init;                      /* Retransmission timeout */
extern int T2init;                      /* Acknowledge delay timeout */
extern int T3init;                      /* Idle poll timeout */
extern int T4init;                      /* Busy timeout */
extern int T5init;                      /* Idle disconnect timeout */
extern int Axserver_enabled;

/* In ax25cmd.c: */
void st_ax25(struct ax25_cb *axp);

/* In ax25subr.c: */
struct ax25_cb *cr_ax25(uint8 *addr);
void del_ax25(struct ax25_cb *axp);
struct ax25_cb *find_ax25(uint8 *);

/* In ax25user.c: */
int ax25val(struct ax25_cb *axp);
int disc_ax25(struct ax25_cb *axp);
int kick_ax25(struct ax25_cb *axp);
/* Per-connection choices for an outgoing link.  A null pointer means what it
 * always meant: the routing table picks the interface and stamps its callsign
 * as the source.  Given here, the caller's choice wins - which is what the
 * service socket needs, where several users share one node and each may want
 * its own callsign and its own port.
 */
struct ax25_opts {
	struct iface *iface;    /* go out here, whatever the route says */
	int ownsource;          /* hdr->source is the caller's, keep it */
};

/* Attach a consumer to a link for one protocol id.  Returns the existing one
 * if there already is one for that id.
 */
struct axservice *open_axservice(struct ax25_cb *axp,int pid,
	void (*r_upcall)(struct axservice *,int),
	void (*t_upcall)(struct axservice *,int),
	void (*s_upcall)(struct axservice *,enum lapb_state,enum lapb_state),
	void *user);

/* Started on demand when a frame arrives for a protocol id nothing is
 * attached to: the configuration decides what, if anything, to attach.
 */
struct axservice *axserv_start(struct ax25_cb *axp,int pid);
void axserv_connected(struct ax25_cb *axp);

/* NET/ROM: one entry, taking every incoming L4 session.  Returns non-zero if
 * it took this one, so the node's own login stays out of the way.
 */
struct circuit;
int nrserv_listen_start(struct circuit *pc);
void nrserv_listen_close(struct circuit *pc);
struct axservice *find_axservice(struct ax25_cb *axp,int pid);
struct mbuf *recv_axservice(struct axservice *sp,uint cnt);
struct mbuf *recv_axservice_packet(struct axservice *sp);
int space_axservice(struct axservice *sp);
int axservice_pending(struct ax25_cb *axp);
int send_axservice(struct axservice *sp,struct mbuf **bpp);

/* Detach it.  When the last one goes, so does the link - counted by asking
 * rather than by keeping a number, which is the sort that lingers at a wrong
 * value after some path forgot to decrement it.
 */
void close_axservice(struct axservice *sp);

/* Just the link.  Whoever wants to consume something attaches a service to
 * it; a link that never had one is not torn down here, it ages out on t5.
 */
struct ax25_cb *open_ax25(struct ax25 *,int,const struct ax25_opts *);
int reset_ax25(struct ax25_cb *axp);
int send_ax25(struct ax25_cb *axp,struct mbuf **bp,int pid);
int space_ax25(struct ax25_cb *axp);

/* In lapb.c: */
void est_link(struct ax25_cb *axp);
void lapbstate(struct ax25_cb *axp,enum lapb_state s);
int lapb_input(struct iface *iface,struct ax25 *hdr,struct mbuf **bp);
int lapb_output(struct ax25_cb *axp);
struct mbuf *segmenter(struct mbuf **bp,uint ssize);
int sendctl(struct ax25_cb *axp,enum lapb_cmdrsp cmdrsp,int cmd);
int sendframe(struct ax25_cb *axp,enum lapb_cmdrsp cmdrsp,int ctl,struct mbuf **data);
int busy(struct ax25_cb *cp);
void ax_t2_timeout(void *p);
void ax_t5_timeout(void *p);
void build_path(struct ax25_cb *cp,struct iface *ifp,struct ax25 *hdr,int reverse,
	const struct ax25_opts *opts);

/* In lapbtimer.c: */
void pollthem(void *p);
void recover(void *p);

/* In ax25subr.c: */
uint ftype(uint control);
void lapb_garbage(int drastic);

/* In axserver.c: */
void axserv_open(struct ax25_cb *axp,int cnt);

/* Callsigns configured with "ax25 listen".  ax_recv() has to admit frames for
 * them, and axserv_open() has to hand the session on rather than to a login.
 */
/* A port list as written by the sysop: "hf1,hf2", or "!aprs,foo" for every
 * port but those.  spec == NULL means every port.  Names, not pointers - see
 * portlist_set() for why.
 */
struct portlist {
	char *spec;
	int exclude;
};

int portlist_set(struct portlist *pl,const char *spec,char *err,int errlen);
void portlist_free(struct portlist *pl);
int portlist_allows(const struct portlist *pl,const struct iface *ifp);

/* A port has taken this callsign: every forwarding entry for it whose
 * protocol the node serves itself is dropped, and any client holding one is
 * closed.  See axlisten_drop_local().
 */
void axlisten_drop_local(const uint8 *call);

int axlisten_active(const uint8 *call);
int axlisten_client_claim(const uint8 *call,int pid,int ui,int fd,char *err,int errlen);
void axlisten_client_release(int fd);
int axserv_pipe_attach(struct axservice *sp,int binary,int *fdp);
int dolisten(int argc,char *argv[],void *p);

#endif  /* _LAPB_H */
