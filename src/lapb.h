/* AX25 protocol implementation */

/* Codes for the open_ax call */
#define AX25_PASSIVE    0       /* not implemented */
#define AX25_ACTIVE     1
#define AX25_SERVER     2       /* Passive, clone on opening */

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
struct axcb {
  char  path[70];               /* AX25 address field */
  int  pathlen;                 /* Length of AX25 address field */
  struct interface *ifp;        /* Pointer to interface structure */
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
  int  wrongseq;                /* Frame with wrong sequence number received */
  long  remote_busy;            /* Other end's window is closed */
  int  vr;                      /* Incoming sequence number expected next */
  int  vs;                      /* Next sequence number to be sent */
  int  retry;                   /* Retransmission retry count */
  int32 srtt;                   /* Smoothed round trip time, milliseconds */
  int32 mdev;                   /* Mean deviation, milliseconds */
  struct timer timer_t1;        /* Retransmission timer */
  struct timer timer_t2;        /* Acknowledgement delay timer */
  struct timer timer_t3;        /* No-activity timer */
  struct timer timer_t4;        /* Busy timer */
  struct mbuf *rcvq;            /* Receive queue */
  int16 rcvcnt;                 /* Receive queue length */
  struct mbuf *sndq;            /* Send queue */
  int16 sndcnt;                 /* Send queue length */
  int16 segsize;                /* Current send segment size */
  void (*r_upcall)();           /* Call when data arrives */
  void (*t_upcall)();           /* Call when ok to send more data */
  void (*s_upcall)();           /* Call when connection state changes */
  char  *user;                  /* User parameter (e.g., for mapping to an
				 * application control block
				 */
  struct axcb *peer;            /* Pointer to peer's control block */
  struct axcb *next;            /* Linked-list pointer */
};

#define NULLAXCB ((struct axcb *) 0)

extern char  *ax25reasons[];            /* Reason names */
extern char  *ax25states[];             /* State names */
extern char  *pathtostr();
extern int  ax_maxframe;                /* Transmit flow control level */
extern int  ax_paclen;                  /* Maximum outbound packet size */
extern int  ax_retry;                   /* Retry limit */
extern int  ax_t1init;                  /* Retransmission timeout */
extern int  ax_t2init;                  /* Acknowledgement delay timeout */
extern int  ax_t3init;                  /* No-activity timeout */
extern int  ax_t4init;                  /* Busy timeout */
extern int  ax_window;                  /* Local flow control limit */
extern struct axcb *axcb_server;        /* Server control block */

/* AX25 user calls */
extern struct axcb *open_ax();
extern int  recv_ax();
extern int  send_ax();
extern int  close_ax();
extern int  reset_ax();
extern int  del_ax();
extern int  valid_ax();
