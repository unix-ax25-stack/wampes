/* Round trip timing parameters */
#define AGAIN   8               /* Average RTT gain = 1/8 */
#define DGAIN   4               /* Mean deviation gain = 1/4 */

#define NR4MAXINFO      236     /* Maximum data in an info packet */

#define NR4CHOKE        0x80
#define NR4NAK          0x40
#define NR4MORE         0x20

#define NR4OPPID        0       /* Protocol ID extension to network layer */
#define NR4OPCONRQ      1       /* Connect request */
#define NR4OPCONAK      2       /* Connect acknowledge */
#define NR4OPDISRQ      3       /* Disconnect request */
#define NR4OPDISAK      4       /* Disconnect acknowledge */
#define NR4OPINFO       5       /* Information packet */
#define NR4OPACK        6       /* Information acknowledge */
#define NR4OPCODE       0x0f    /* Mask for opcode nybble */

struct circuit {
  int  localindex;              /* Local circuit index */
  int  localid;                 /* Local circuit ID */
  int  remoteindex;             /* Remote circuit index */
  int  remoteid;                /* Remote circuit ID */
  int  outbound;                /* Circuit was created by local request */
  struct ax25_addr node;        /* Call of peer node */
  struct ax25_addr orguser;     /* Call of originating user */
  struct ax25_addr orgnode;     /* Call of originating node */
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
  int  closed;                  /* Disconnect when send queue empty */
  int  recv_state;              /* Incoming sequence number expected next */
  int  send_state;              /* Next sequence number to be sent */
  int  remote_busy;             /* Other end's window is closed */
  int  retry;                   /* Retransmission retry count */
  int32 srtt;                   /* Smoothed round trip time, milliseconds */
  int32 mdev;                   /* Mean deviation, milliseconds */
  struct timer timer_t1;        /* Retransmission timer */
  struct timer timer_t2;        /* Acknowledgement delay timer */
  struct timer timer_t3;        /* No-activity timer */
  struct timer timer_t4;        /* Busy timer */
  struct mbuf *reseq;           /* Resequencing queue */
  struct mbuf *rcvq;            /* Receive queue */
  int16 rcvcnt;                 /* Receive queue length */
  struct mbuf *sndq;            /* Send queue */
  struct mbuf *resndq;          /* Resend queue */
  int  unack;                   /* Number of unacked frames */
  long  sndtime[256];           /* Time of 1st transmission */
  void (*r_upcall)();           /* Call when data arrives */
  void (*t_upcall)();           /* Call when ok to send more data */
  void (*s_upcall)();           /* Call when connection state changes */
  char  *user;                  /* User parameter (e.g., for mapping to an
				 * application control block
				 */
  struct circuit *next;         /* Linked-list pointer */
};

extern struct circuit *open_nr();

