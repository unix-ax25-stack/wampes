/* AX.25 datagram (address) sub-layer definitions */

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

/* Maximum number of digipeaters */
#define MAXDIGIS        7       /* 7 digipeaters plus src/dest */
#define ALEN            6       /* Number of chars in callsign field */
#define AXALEN          7       /* Total AX.25 address length, including SSID */

/* Internal representation of an AX.25 address */
struct ax25_addr {
	char call[ALEN];
	char ssid;
#define SSID            0x1e    /* Sub station ID */
#define REPEATED        0x80    /* Has-been-repeated bit in repeater field */
#define E               0x01    /* Address extension bit */
#define C               0x80    /* Command/response designation */
};
#define NULLAXADDR      (struct ax25_addr *)0
/* Our AX.25 address */
extern struct ax25_addr mycall;

/* AX.25 broadcast address: "QST   -0" in shifted ASCII */
extern struct ax25_addr ax25_bdcst;

/* Internal representation of an AX.25 header */
struct ax25 {
	struct ax25_addr dest;                  /* Destination address */
	struct ax25_addr source;                /* Source address */
	struct ax25_addr digis[MAXDIGIS];       /* Digi string */
	int ndigis;                             /* Number of digipeaters */
	int cmdrsp;                             /* Command/response */
};

/* C-bit stuff */
#define UNKNOWN         0
#define COMMAND         1
#define RESPONSE        2

/* Bit fields in AX.25 Level 3 Protocol IDs (PIDs)
 * The high order two bits control multi-frame messages.
 * The lower 6 bits is the actual PID. Single-frame messages are
 * sent with both the FIRST and LAST bits set, so that the resulting PIDs
 * are compatible with older code.
 */
#define PID_FIRST       0x80    /* Frame is first in a message */
#define PID_LAST        0x40    /* Frame is last in a message */
#define PID_PID         0x3f    /* Protocol ID subfield */

#define PID_IP          0x0c    /* ARPA Internet Protocol */
#define PID_ARP         0x0d    /* ARPA Address Resolution Protocol */
#define PID_NETROM      0x0f    /* NET/ROM */
#define PID_NO_L3       0x30    /* No level 3 protocol */

#define axptr(a)         ((struct ax25_addr *) (a))
#define ismycall(call)   addreq((call), &mycall)

