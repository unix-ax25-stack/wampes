/* @(#) $Id: ax25.h,v 1.19 1996/08/12 18:51:17 deyke Exp $ */

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
extern uint8 Mycall[AXALEN];

/* List of AX.25 multicast addresses, e.g., "QST   -0" in shifted ASCII */
extern uint8 Ax25multi[][AXALEN];

extern int Digipeat;
extern int Ax25mbox;
extern int Axigntos;

enum lapb_cmdrsp {
	LAPB_UNKNOWN,
	LAPB_COMMAND,
	LAPB_RESPONSE
};

/* Internal representation of an AX.25 header */
struct ax25 {
	uint8 dest[AXALEN];             /* Destination address */
	uint8 source[AXALEN];           /* Source address */
	uint8 digis[MAXDIGIS][AXALEN];  /* Digi string */
	int ndigis;                     /* Number of digipeaters */
	int nextdigi;                   /* Index to next digi in chain */
	enum lapb_cmdrsp cmdrsp;        /* Command/response */
	int qso_num;                    /* QSO number or -1 */
};

/* AX.25 routing table entry */
struct ax_route {
	struct ax_route *next;          /* Linked list pointer */
	uint8 target[AXALEN];
	struct ax_route *digi;
	struct iface *ifp;
	int perm;
	int jumpstart;
	long time;
};

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

/* Link quality report packet header, internal format */
struct lqhdr;
/* Link quality entry, internal format */
struct lqentry;

/* Link quality database record format
 * Currently used only by AX.25 interfaces
 */
struct lq {
	struct lq *next;
	uint8 addr[AXALEN];     /* Hardware address of station heard */
	struct iface *iface;    /* Interface address was heard on */
	int32 time;             /* Time station was last heard */
	int32 currxcnt; /* Current # of packets heard from this station */

#ifdef  notdef          /* Not yet implemented */
	/* # of packets heard from this station as of his last update */
	int32 lastrxcnt;

	/* # packets reported as transmitted by station as of his last update */
	int32 lasttxcnt;

	uint16 hisqual; /* Fraction (0-1000) of station's packets heard
			 * as of last update
			 */
	uint16 myqual;  /* Fraction (0-1000) of our packets heard by station
			 * as of last update
			 */
#endif
};

extern struct lq *Lq;   /* Link quality record headers */

/* Structure used to keep track of monitored destination addresses */
struct ld {
	struct ld *next;        /* Linked list pointers */
	uint8 addr[AXALEN];/* Hardware address of destination overheard */
	struct iface *iface;    /* Interface address was heard on */
	int32 time;             /* Time station was last mentioned */
	int32 currxcnt; /* Current # of packets destined to this station */
};

extern struct ld *Ld;   /* Destination address record headers */

/* In ax25.c: */
void ax_recv(struct iface *,struct mbuf **);
int axui_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
int axi_send(struct mbuf **bp,struct iface *iface,int32 gateway,uint8 tos);
int ax_output(struct iface *iface,uint8 *dest,uint8 *source,uint16 pid,
	struct mbuf **data);
int axsend(struct iface *iface,uint8 *dest,uint8 *source,
	enum lapb_cmdrsp cmdrsp,int ctl,struct mbuf **data);
int valid_remote_call(const uint8 *call);
struct ax_route *ax_routeptr(const uint8 *call, int create);
void axroute_add(struct iface *iface, struct ax25 *hdr, int perm);
void axroute(struct ax25 *hdr, struct iface **ifpp);

/* In axhdr.c: */
void htonax25(struct ax25 *hdr,struct mbuf **data);
int ntohax25(struct ax25 *hdr,struct mbuf **bpp);

/* In axlink.c: */
void getlqentry(struct lqentry *ep,struct mbuf **bpp);
void getlqhdr(struct lqhdr *hp,struct mbuf **bpp);
void logsrc(struct iface *iface,uint8 *addr);
void logdest(struct iface *iface,uint8 *addr);
char *putlqentry(char *cp,uint8 *addr,int32 count);
char *putlqhdr(char *cp,uint16 version,int32 ip_addr);
struct lq *al_lookup(struct iface *ifp,uint8 *addr,int sort);

/* In ax25subr.c: */
int addreq(const uint8 *a,const uint8 *b);
char *pax25(char *e,uint8 *addr);
int setcall(uint8 *out,char *call);
struct iface *ismyax25addr(const uint8 *addr);
void addrcp(uint8 *to,const uint8 *from);
int ax25args_to_hdr(int argc,char *argv[],struct ax25 *hdr);
char *ax25hdr_to_string(struct ax25 *hdr);

/* In ax25file.c: */
void axroute_savefile(void);
void axroute_loadfile(void);

#endif  /* _AX25_H */
