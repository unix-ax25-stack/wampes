/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.h,v 1.13 1993/05/17 13:44:45 deyke Exp $ */

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
	int qso_num;                    /* QSO number or -1 */
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
	void (*funct)(struct iface *,struct ax25_cb *,char *, char *,
	 struct mbuf *,int);
};
extern struct axlink Axlink[];

/* Codes for the open_ax25 call */
#define AX_PASSIVE      0
#define AX_ACTIVE       1
#define AX_SERVER       2       /* Passive, clone on opening */

/* In ax25.c: */
void ax_recv(struct iface *,struct mbuf *);
int axui_send(struct mbuf *bp,struct iface *iface,int32 gateway,int tos);
int axi_send(struct mbuf *bp,struct iface *iface,int32 gateway,int tos);
int ax_output(struct iface *iface,char *dest,char *source,int    pid,
	struct mbuf *data);
int sendframe(struct ax25_cb *axp,int cmdrsp,int ctl,struct mbuf *data);
void axnl3(struct iface *iface,struct ax25_cb *axp,char *src,
	char *dest,struct mbuf *bp,int mcast);
int valid_remote_call(const char *call);
struct ax_route *ax_routeptr(const char *call, int create);
void axroute_add(struct iface *iface, struct ax25 *hdr, int perm);
void axroute(struct ax25 *hdr, struct iface **ifpp);

/* In ax25cmd.c: */
void st_ax25(struct ax25_cb *axp);

/* In axhdr.c: */
struct mbuf *htonax25(struct ax25 *hdr,struct mbuf *data);
int ntohax25(struct ax25 *hdr,struct mbuf **bpp);

/* In ax25user.c: */
int ax25val(struct ax25_cb *axp);
int disc_ax25(struct ax25_cb *axp);
int kick_ax25(struct ax25_cb *axp);
struct ax25_cb *open_ax25(struct ax25 *,
	int,
	void (*)(struct ax25_cb *,int),
	void (*)(struct ax25_cb *,int),
	void (*)(struct ax25_cb *,int,int),
	char *user);
struct mbuf *recv_ax25(struct ax25_cb *axp,int    cnt);
int reset_ax25(struct ax25_cb *axp);
int send_ax25(struct ax25_cb *axp,struct mbuf *bp,int pid);
int space_ax25(struct ax25_cb *axp);

/* In ax25subr.c: */
int addreq(const char *a,const char *b);
struct ax25_cb *cr_ax25(char *addr);
void del_ax25(struct ax25_cb *axp);
struct ax25_cb *find_ax25(char *);
char *pax25(char *e,char *addr);
int setcall(char *out,char *call);
struct iface *ismyax25addr(const char *addr);
void addrcp(char *to,const char *from);
int ax25args_to_hdr(int argc,char *argv[],struct ax25 *hdr);
char *ax25hdr_to_string(struct ax25 *hdr);

/* In ax25file.c: */
void axroute_savefile(void);
void axroute_loadfile(void);

#endif  /* _AX25_H */
