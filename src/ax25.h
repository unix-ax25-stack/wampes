/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.h,v 1.11 1993/02/23 21:34:03 deyke Exp $ */

#ifndef _AX25_H
#define _AX25_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

extern char Ax25_eol[];

/* AX.25 datagram (address) sub-layer definitions */

#define MAXDIGIS        8       /* Maximum number of digipeaters */
#define ALEN            6       /* Number of chars in callsign field */
#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define AXBUF           10      /* Buffer size for maximum-length ascii call */

/* Bits within SSID field of AX.25 address */
#define SSID            0x1e    /* Sub station ID */
#define REPEATED        0x80    /* Has-been-repeated bit in repeater field */
#define E               0x01    /* Address extension bit */
#define C               0x80    /* Command/response designation */

/* Our AX.25 address */
extern char Mycall[AXALEN];

/* List of AX.25 multicast addresses, e.g., "QST   -0" in shifted ASCII */
extern char Ax25multi[][AXALEN];

extern int Digipeat;
extern int Ax25mbox;

/* Internal representation of an AX.25 header */
struct ax25 {
	char dest[AXALEN];              /* Destination address */
	char source[AXALEN];            /* Source address */
	char digis[MAXDIGIS][AXALEN];   /* Digi string */
	int ndigis;                     /* Number of digipeaters */
	int nextdigi;                   /* Index to next digi in chain */
	int cmdrsp;                     /* Command/response */
};

/* C-bit stuff */
#define LAPB_UNKNOWN            0
#define LAPB_COMMAND            1
#define LAPB_RESPONSE           2

/* AX.25 routing table entry */
struct ax_route {
	struct ax_route *next;          /* Linked list pointer */
	char target[AXALEN];
	struct ax_route *digi;
	struct iface *ifp;
	int perm;
	int jumpstart;
	long time;
};
#define NULLAXR ((struct ax_route *)0)

#define AXROUTESIZE     499
extern struct ax_route *Ax_routes[];
extern struct iface *Axroute_default_ifp;

/* AX.25 Level 3 Protocol IDs (PIDs) */
#define PID_X25         0x01    /* CCITT X.25 PLP */
#define PID_SEGMENT     0x08    /* Segmentation fragment */
#define PID_TEXNET      0xc3    /* TEXNET datagram protocol */
#define PID_LQ          0xc4    /* Link quality protocol */
#define PID_APPLETALK   0xca    /* Appletalk */
#define PID_APPLEARP    0xcb    /* Appletalk ARP */
#define PID_IP          0xcc    /* ARPA Internet Protocol */
#define PID_ARP         0xcd    /* ARPA Address Resolution Protocol */
#define PID_FLEXNET     0xce    /* FLEXNET */
#define PID_NETROM      0xcf    /* NET/ROM */
#define PID_NO_L3       0xf0    /* No level 3 protocol */

#define SEG_FIRST       0x80    /* First segment of a sequence */
#define SEG_REM         0x7f    /* Mask for # segments remaining */

/* Linkage to network protocols atop ax25 */
struct axlink {
	int pid;
	void (*funct) __ARGS((struct iface *,struct ax25_cb *,char *, char *,
	 struct mbuf *,int));
};
extern struct axlink Axlink[];

/* Codes for the open_ax25 call */
#define AX_PASSIVE      0
#define AX_ACTIVE       1
#define AX_SERVER       2       /* Passive, clone on opening */

/* In ax25.c: */
void ax_recv __ARGS((struct iface *,struct mbuf *));
int axui_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int tos));
int axi_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int tos));
int ax_output __ARGS((struct iface *iface,char *dest,char *source,int   pid,
	struct mbuf *data));
int sendframe __ARGS((struct ax25_cb *axp,int cmdrsp,int ctl,struct mbuf *data));
void axnl3 __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast));
int valid_remote_call __ARGS((const char *call));
struct ax_route *ax_routeptr __ARGS((const char *call, int create));
void axroute_add __ARGS((struct iface *iface, struct ax25 *hdr, int perm));
void axroute __ARGS((struct ax25 *hdr, struct iface **ifpp));

/* In ax25cmd.c: */
void st_ax25 __ARGS((struct ax25_cb *axp));

/* In axhdr.c: */
struct mbuf *htonax25 __ARGS((struct ax25 *hdr,struct mbuf *data));
int ntohax25 __ARGS((struct ax25 *hdr,struct mbuf **bpp));

/* In ax25user.c: */
int ax25val __ARGS((struct ax25_cb *axp));
int disc_ax25 __ARGS((struct ax25_cb *axp));
int kick_ax25 __ARGS((struct ax25_cb *axp));
struct ax25_cb *open_ax25 __ARGS((struct ax25 *,
	int,
	void (*) __ARGS((struct ax25_cb *,int)),
	void (*) __ARGS((struct ax25_cb *,int)),
	void (*) __ARGS((struct ax25_cb *,int,int)),
	char *user));
struct mbuf *recv_ax25 __ARGS((struct ax25_cb *axp,int   cnt));
int reset_ax25 __ARGS((struct ax25_cb *axp));
int send_ax25 __ARGS((struct ax25_cb *axp,struct mbuf *bp,int pid));
int space_ax25 __ARGS((struct ax25_cb *axp));

/* In ax25subr.c: */
int addreq __ARGS((const char *a,const char *b));
struct ax25_cb *cr_ax25 __ARGS((char *addr));
void del_ax25 __ARGS((struct ax25_cb *axp));
struct ax25_cb *find_ax25 __ARGS((char *));
char *pax25 __ARGS((char *e,char *addr));
int setcall __ARGS((char *out,char *call));
struct iface *ismyax25addr __ARGS((const char *addr));
void addrcp __ARGS((char *to,const char *from));
int ax25args_to_hdr __ARGS((int argc,char *argv[],struct ax25 *hdr));
char *ax25hdr_to_string __ARGS((struct ax25 *hdr));

/* In ax25file.c: */
void axroute_savefile __ARGS((void));
void axroute_loadfile __ARGS((void));

#endif  /* _AX25_H */
