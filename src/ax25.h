/* @(#) $Id: ax25.h,v 1.27 2005/03/11 14:36:09 dl9sau Exp $ */

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
#define SSID_DAMA       0x20    /* DAMA flag */
#define SSID_EAX25      0x40    /* EAX25 session marker */

/* Our AX.25 address */
extern uint8 Mycall[];

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
	uint8 ext;                      /* ax25 extensions like DAMA, modulo-128 (EAX25) */
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
	int vjcomp;                     /* MW: can do TCP compression */
	/* What we learned about modulo-128 with this station, from our own
	 * traffic and nothing else: 0 not tried, 1 it worked, -1 it did not.
	 * Three values and not two, because "did not" has to be told apart
	 * from "never asked" - otherwise every connect probes again, and the
	 * station that answers nothing costs the full probe each time.
	 *
	 * Deliberately NOT written to axroute_data.  A restart is exactly when
	 * asking again is right, because the other end may have grown new
	 * hardware meanwhile, and one probe is all it costs.  The entry ages
	 * with the route: axroute_savefile() frees what has not been used for
	 * AXROUTE_HOLDTIME, so a "cannot" does not outlive its station.
	 */
	int eax25;
	/* ZUFALL VON EIGENSCHAFT TRENNEN, und dafuer taugt keine Frist.
	 *
	 * Ein Rueckfall auf SABM sagt fuer sich genommen nichts: er kann
	 * heissen "er kann kein Modulo 128", oder es ging ein SABME verloren -
	 * oder, schlimmer, SEIN UA darauf, denn dann steht er auf 128 und wir
	 * auf 8.  Eine Vergesszeit hilft nicht (Thomas): bricht ein Link und
	 * baut nach 30 s neu auf, liegt das innerhalb jeder sinnvollen Frist.
	 *
	 * Was die beiden trennt, ist WIEDERHOLUNG.  Verlust ist sporadisch,
	 * fehlendes Modulo 128 ist konstant:
	 *
	 *   eax25_fails  Rueckfaelle IN FOLGE.  Ein Erfolg setzt ihn zurueck;
	 *                erst bei EAX25_MAXFAILS gilt "kann nicht".  Bei 20 %
	 *                Verlust scheitert ein Aufbau mit rund 5 %, drei in
	 *                Folge also mit etwa 1:10000.
	 *   eax25_skips  Verbindungen seit dem Aufgeben.  Nach EAX25_RETRY
	 *                wird wieder gefragt - auf einer Strecke, die oft neu
	 *                aufbaut, also frueher, und dort war die Fehldiagnose
	 *                auch wahrscheinlicher.
	 */
	int eax25_fails;
	int eax25_skips;
	/* WHERE THIS STATION SITS ON AN ETHERNET, when the port is a BPQether
	 * one.  Learned from what arrives, so that we can answer to the one
	 * machine instead of shouting at the whole segment.
	 *
	 * It belongs to the ROUTE and not to a table of its own because the
	 * question is the same one the route answers - by which path do we
	 * reach him - and because the two then age and change together.
	 *
	 * IT IS ONLY VALID FOR rp->ifp, the interface it was heard on.  When
	 * the route moves - to another port, to another BPQether segment, or
	 * behind a digipeater - the address is dropped rather than carried
	 * along: a MAC is unique on ONE segment, and on the next one it may
	 * well belong to somebody else (Thomas).  Falling back to the
	 * broadcast costs a little traffic; a stale unicast costs the
	 * connection, and silently.
	 *
	 * mactime is its own clock and not the route's.  Expiry here does not
	 * mean "station gone", only "ask the whole segment again", so it is
	 * short where AXROUTE_HOLDTIME is 24.8 days.
	 */
	uint8 mac[6];
	int mac_valid;
	long mactime;
};

/* An hour (Thomas).  Long enough that a quiet neighbour is not shouted at
 * every few minutes, short enough that a station which moved is found again
 * the same afternoon.
 */
#define AXROUTE_MACHOLD  3600L

#define AXR_EAX25_UNKNOWN        0
#define AXR_EAX25_YES            1
#define AXR_EAX25_NO           (-1)

/* Siehe eax25_fails / eax25_skips oben. */
#define EAX25_MAXFAILS           3      /* Rueckfaelle in Folge bis "kann nicht" */
#define EAX25_RETRY             15      /* Verbindungen bis zum naechsten Versuch */

#define AXROUTESIZE     499
extern struct ax_route *Ax_routes[];
extern struct iface *Axroute_default_ifp;

/* AX.25 Level 3 Protocol IDs (PIDs) */
#define PID_X25         0x01    /* CCITT X.25 PLP */
#ifdef	AX25_VJCOMP
#define PID_VJCOMP      0x06    /* MW: VJ compressed  */
#define PID_VJUNCOMP    0x07    /* MW: VJ uncompressed */
#endif
#define PID_SEGMENT     0x08    /* Segmentation fragment */
#define PID_FLEXTALK    0x0f    /* FLEXTALK - voice over ax25 */
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
int ax_output(struct iface *iface,uint8 *dest,uint8 *source,uint pid,
	struct mbuf **data);
int axsend(struct iface *iface,uint8 *dest,uint8 *source,
	enum lapb_cmdrsp cmdrsp,int ctl,struct mbuf **data, uint8 *ax_via);
int valid_remote_call(const uint8 *call);
struct ax_route *ax_routeptr(const uint8 *call, int create);
void axroute_add(struct iface *iface, struct ax25 *hdr, int perm);

/* The ethernet address of a station on a BPQether port.  <call> is the one
 * that put the frame on the wire - the last repeater that has already
 * repeated it, or the source when there is none - because that is the
 * machine whose card we are looking at.
 */
void axroute_mac_learn(struct iface *iface, const uint8 *call,
	const uint8 *mac);
/* NULL when nothing is known, when it has aged out, or when the route has
 * since moved somewhere else.  The caller then uses the broadcast.
 */
const uint8 *axroute_mac_get(struct iface *iface, const uint8 *call);

/* One answer to "is that address one we answer to on this interface" - the
 * interface's own callsign, one of its links', or one we listen for.
 */
int ax_answers_to(struct iface *iface, const uint8 *addr);

/* A UI frame with the header exactly as given - no routing, nothing added.
 * For frames we are handed to originate, where the path is the sender's.
 */
int ax_send_ui(struct iface *iface, struct ax25 *hdr, int pid,
	struct mbuf **bpp);
void axroute(struct ax25 *hdr, struct iface **ifpp);

#ifdef	AX25_VJCOMP
/* MW: prototypes for VJ receiver hooks (in ax25.c) */
void ax_rx_vjcomp(struct iface *ifp, struct ax25_cb *axp, uint8 *dest, uint8 *src, struct mbuf **bpp, int mcast);
void ax_rx_vjuncomp(struct iface *ifp, struct ax25_cb *axp, uint8 *dest, uint8 *src, struct mbuf **bpp, int mcast);
void ax_rx_ip(struct iface *ifp, struct ax25_cb *axp, uint8 *dest, uint8 *src, struct mbuf **bpp, int mcast);
#endif

/* In axhdr.c: */
void htonax25(struct ax25 *hdr,struct mbuf **data);
int ntohax25(struct ax25 *hdr,struct mbuf **bpp);

/* In axlink.c: */
void getlqentry(struct lqentry *ep,struct mbuf **bpp);
void getlqhdr(struct lqhdr *hp,struct mbuf **bpp);
void logsrc(struct iface *iface,uint8 *addr);
void logdest(struct iface *iface,uint8 *addr);
char *putlqentry(char *cp,uint8 *addr,int32 count);
char *putlqhdr(char *cp,uint version,int32 ip_addr);
struct lq *al_lookup(struct iface *ifp,uint8 *addr,int sort);

/* In ax25subr.c: */
int addreq(const uint8 *a,const uint8 *b);
char *pax25(char *e,const uint8 *addr);
int setcall(uint8 *out,const char *call);
struct iface *ismyax25addr(const uint8 *addr);
void addrcp(uint8 *to,const uint8 *from);
int ax25args_to_hdr(int argc,char *argv[],struct ax25 *hdr);

/* Defined in lapb.h, which this header does not pull in - named here so the
 * prototype below refers to that type and not to one of its own.
 */
struct ax25_opts;

int ax25_parse_target(int argc,char *argv[],struct ax25 *hdr,
	struct ax25_opts *opts,int *pid,int *silent,char *err,int errlen);
char *ax25hdr_to_string(struct ax25 *hdr);

/* In ax25file.c: */
void axroute_savefile(void *arg);
void axroute_loadfile(void);

/* In axserver.c: */	// dl9sau
struct axservice;
void axserv_recv_upcall_discard(struct axservice *sp, int cnt);

#endif  /* _AX25_H */
