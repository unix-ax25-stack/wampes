/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.h,v 1.6 1991/03/28 19:39:07 deyke Exp $ */

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

/* AX.25 datagram (address) sub-layer definitions */

#define MAXDIGIS        7       /* Maximum number of digipeaters */
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

/* AX.25 broadcast address: "QST   -0" in shifted ASCII */
extern char Ax25_bdcst[AXALEN];

extern int Digipeat;

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
	char call[AXALEN];
	struct ax_route *digi;
	struct iface *ifp;
	int perm;
	long time;
	struct ax_route *next;
};

#define AXROUTESIZE     499

extern struct ax_route *Ax_routes[AXROUTESIZE];
extern struct iface *axroute_default_ifp;

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

#define ismycall(call)   addreq(call, Mycall)

/* Linkage to network protocols atop ax25 */
struct axlink {
	int pid;
	void (*funct) __ARGS((struct iface *,struct ax25_cb *,char *, char *,
	 struct mbuf *,int));
};
extern struct axlink Axlink[];

/* List of AX.25 multicast addresses */
extern char *Axmulti[];

/* Codes for the open_ax call */
#define AX_PASSIVE      0       /* not implemented */
#define AX_ACTIVE       1
#define AX_SERVER       2       /* Passive, clone on opening */

/* In ax25.c: */
void ax_recv __ARGS((struct iface *,struct mbuf *));
int ax_send __ARGS((struct mbuf *bp,struct iface *iface,int32 gateway,int prec,
	int del,int tput,int rel));
int ax_output __ARGS((struct iface *iface,char *dest,char *source,int pid,
	struct mbuf *data));
void axnl3 __ARGS((struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast));
struct ax_route *ax_routeptr __ARGS((char *call, int create));
void axroute_add __ARGS((struct iface *iface, struct ax25 *hdr, int perm));
int axroute __ARGS((struct ax25_cb *cp, struct mbuf *bp));
char *ax25hdr_to_string __ARGS((struct ax25 *hdr));

/* In axhdr.c: */
struct mbuf *htonax25 __ARGS((struct ax25 *hdr,struct mbuf *data));
int ntohax25 __ARGS((struct ax25 *hdr,struct mbuf **bpp));

/* In ax25subr.c: */
int addreq __ARGS((char *a,char *b));
struct iface *ismyax25addr __ARGS((char *addr));
void addrcp __ARGS((char *to,char *from));
char *pax25 __ARGS((char *e,char *addr));
int setcall __ARGS((char *out,char *call));
int16 ftype __ARGS((int control));

/* In ax25file.c: */
void axroute_savefile __ARGS((void));
void axroute_loadfile __ARGS((void));

#endif  /* _AX25_H */
