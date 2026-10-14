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
extern int Digi_keep_path;
extern int Digiout;
/* Ob dieser Knoten als Nexthop den Hop selbst terminiert und bestaetigt -
 * die Faehigkeit, die das hop2hop-ack-Bit im XID ankuendigt (doc/EAX25.md).
 * Das ist genau Store-and-Forward-Digipeaten (Digipeat == 2). */
int ax25_hop2hop(void);
extern int Ax25_v2_default;         /* globaler Factory-Default fuer ax25-v2.2 */
extern long Xid_probe_time;         /* Frist vor erneutem XID-Probe, Sekunden */
/* WIE OFT eine unbeantwortete XID-Frage in Folge wiederholt wird, ehe
 * zwischen zwei Fragen nur noch Xid_probe_time liegt.  Frisches Fragen
 * ist billig und eine verlorene Frage darf nicht eine ganze Stunde
 * kosten; nach diesen Versuchen wird die Station als "stumm" gefuehrt
 * (Thomas). */
extern int Xid_retries;
/* WIE LANGE eine BEANTWORTETE XID gilt.  Eine echte Antwort beschreibt
 * eine Faehigkeit, die sich aendern kann - aber nicht im Minutentakt.
 * Bis dahin wird der Peer nicht wieder gefragt, danach neu ausgehandelt
 * (1,5 Tage als Vorgabe, Thomas). */
extern long Xid_expire_time;
extern int Ax25mbox;
extern int Axigntos;

/* Whether the node segments ITS OWN packets when the MTU is larger than
 * paclen - and only those: received segments are always reassembled, and a
 * repeated frame is always segmented to fit the outgoing port, neither of
 * which is a matter of configuration.  off: never.  on: every protocol.
 * exempt-l3: every protocol except PID_NO_L3 (plain text, 0xf0), which is
 * the default.  The value here is the node default; a port can override it
 * with "ifconfig <iface> segmentation".  See doc/AX25-MTU-SEGMENTATION.md.
 */
#define AXSEG_OFF       1
#define AXSEG_ON        2
#define AXSEG_EXEMPT_L3 3
extern int Ax25_segmentation;
char *axseg_name(int mode);
int   axseg_word(char *s);
int   ax25_segments_pid(struct iface *ifp, int pid);

/* Loop-Schutz: Fingerprint eines fertigen UI-Rahmens merken und ein
 * byte-identisches Wiederkommen innerhalb Ax_dup_window als Schleife/Echo
 * verwerfen.  Nur UI: connected-mode identische Wiederholungen (RR+/I/SABM)
 * sind Protokoll-Timing.  Verstellbar mit "ax25 loop-protect <s>" - das
 * Fenster in Millisekunden, 0 schaltet die Erkennung aus.  Ax_echoes zaehlt
 * die verworfene Wiederkunft (siehe "axip stats").
 */
extern int Ax_dup_window;
extern int Ax_echoes;
int    ax25_frame_is_ui(const uint8 *data, int len);
uint32 ax_fingerprint(struct mbuf *bp);
uint32 ax_fingerprint_data(const uint8 *data, int len);
void   ax_dup_remember(uint32 h);
int    ax_dup_recent(uint32 h);

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
	/* WANN DAS INTERFACE ZULETZT BESTAETIGT WURDE - nicht dasselbe wie
	 * "time", das jeder Rahmen erneuert.  Nur hieran haengt der
	 * Schreibschutz der Bruecke, siehe AXROUTE_PIN.
	 */
	long iftime;
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
	/* What a station told us - or what it did not answer - about
	 * AX.25 2.2 XID.  Same shape as eax25 and reset with it whenever
	 * the route moves (see axroute_add()): XID capability belongs to a
	 * path, not to a callsign.  Not persisted, for the same reason.
	 */
	int xid;
	/* WANN "kann kein XID" FESTGESTELLT WURDE, und WARUM.  Ohne die beiden
	 * waere eine "kann nicht"-Auskunft eine Sackgasse: der naechste Connect
	 * wuerde sie nie hinterfragen, bis die Route nach AXROUTE_HOLDTIME
	 * (24.8 Tage) verfaellt.  Die Trennung muss eine FRMR-Antwort von
	 * Schweigen unterscheiden - denn eines ist eine Auskunft des Peers, das
	 * andere nur unsere Vermutung:
	 *
	 *   xid_time  Zeitpunkt, zu dem der letzte XID-Probe unbeantwortet blieb.
	 *             Nach Xid_probe_time wird wieder gefragt - der Peer koennte
	 *             sich geaendert haben.
	 *   xid_frmr  Der Peer antwortete auf ein XID unsererseits mit FRMR: er
	 *             spricht definitiv kein XID (und sagt es uns).  Dann wird
	 *             NIE wieder gefragt, nicht nach einer Frist.  FRMR schlaegt
	 *             also die Uhr.
	 *
	 * Alle drei werden wie xid selbst vergessen, wenn die Route umzieht
	 * (axroute_add), und nicht persistiert (ax25file.c) - beim Neustart ist
	 * frisches Fragen ohnehin richtig.
	 */
	long xid_time;                  /* Sekunden, secclock() */
	int xid_frmr;                   /* Peer antwortete mit FRMR: nie wieder */
	/* Unbeantwortete XID-Fragen IN FOLGE, je Aufbau hoechstens eine.
	 * Auf HF geht eine Frage verloren; ohne diesen Zaehler haette erst
	 * Xid_probe_time wieder gefragt, und zwei verlorene Fragen kosteten
	 * zwei Stunden, ehe XID und Modulo 128 ueberhaupt in Frage kommen.
	 * Die ersten XID_MAXTRIES Aufbauten fragen daher von selbst wieder,
	 * danach erst greift die Stunde.
	 */
	int xid_tries;
	/* WAS UNS DER PEER IN SEINER XID-ANTWORT UEBER SICH GESAGT HAT -
	 * fuer KUENFTIGE Sitzungen, nicht nur fuer die, in der er es sagte.
	 *
	 * Bisher starb das mit jeder Verbindung: est_link() nullte
	 * peer_n1/peer_k, und eine Station, die einmal brav geantwortet
	 * hatte, wurde beim naechsten Anruf wieder gefragt - oder, schlimmer,
	 * gar nicht gefragt und dann ohne ihre Grenzen bedient.  Eine
	 * gemessene Antwort ist eine Auskunft ueber die STATION, und sie
	 * gilt, bis sie verfaellt (Xid_expire_time) oder die Route umzieht.
	 *
	 * Die ROHEN PI-Werte, so wie sie auf dem Draht standen: n1rx in
	 * BITS (PI=6) und window in RAHMEN (PI=8).  Null heisst "er hat
	 * dazu nichts gesagt" - und darf nirgends als "kann 0" gelesen
	 * werden (siehe ax25_apply_peer_xid).
	 *
	 * peer_xid_time ist der secclock()-Zeitpunkt der Antwort.  Nur eine
	 * echte Antwort fuellt das; eine blosse Frage (Kommando) oder ein
	 * Verstummen sagt ueber N1 und k nichts und laesst die Felder leer.
	 */
	unsigned peer_n1rx;             /* PI=6: was er annimmt (Bits) */
	unsigned peer_window;           /* PI=8: was er puffert (Rahmen) */
	int peer_srej;                  /* PI=3: er kann SREJ */
	int peer_segment;               /* PI=3: er kann den Segmenter */
	long peer_xid_time;             /* wann gelernt, secclock() */
	/* WHERE THIS STATION SITS ON AN ETHERNET, when the port is a BPQether
	 * one.  Learned from what arrives, so that we can answer to the one
	 * machine instead of shouting at the whole segment.
	 *
	 * It belongs to the ROUTE and not to a table of its own because the
	 * question is the same one the route answers - by which path do we
	 * reach him - and because the two then age and change together.
	 *
	 * IT CARRIES THE INTERFACE IT WAS HEARD ON, and that is what makes it
	 * valid or not.  A MAC is unique on ONE segment; on the next one the
	 * same six octets may belong to somebody else entirely (Thomas), so
	 * an address learned on bpq0 must never be used on bpq1.
	 *
	 * The interface is kept HERE rather than compared against rp->ifp,
	 * and that was learned the hard way: net_route() only ENQUEUES a
	 * frame (iface.c, onto Hopper), so at the moment a driver has one in
	 * its hands, ax_recv() and with it axroute_add() have not run yet.
	 * The route still says where the station was BEFORE.  Tying the
	 * address to its own interface makes the question independent of that
	 * order - and of whether the route moves at all.
	 *
	 * mactime is its own clock and not the route's.  Expiry here does not
	 * mean "station gone", only "ask the whole segment again", so it is
	 * short where AXROUTE_HOLDTIME is 24.8 days.
	 */
	uint8 mac[6];
	struct iface *mac_ifp;          /* 0: nothing known */
	long mactime;
};

/* An hour (Thomas).  Long enough that a quiet neighbour is not shouted at
 * every few minutes, short enough that a station which moved is found again
 * the same afternoon.
 */
#define AXROUTE_MACHOLD  3600L

/* WIE LANGE EINE GEBRUECKTE ROUTE FESTGEKLOPFT IST (Sekunden).
 *
 * Bruecken wir einen Rahmen von A nach B, kann er auf B zurueckkommen - auf
 * einem geteilten Segment ist das der Normalfall.  Wuerden wir daraus
 * lernen, wanderte die Route des ABSENDERS auf B, und die naechste Antwort
 * ginge im Kreis.  Thomas' Mass dafuer: "ein loop Paket ist via inet < 1s
 * unterwegs, auf HF vielleicht 30s.  Ein loop Schutz von 2-5min sollte
 * eigentlich genuegen."
 *
 * NUR GEGEN DAS ECHO DER EIGENEN BRUECKE, nicht gegen jedes Umlernen: sonst
 * entschiede diese Zeile nebenbei die offene Frage, wohin wir senden, wenn
 * ein Nutzer auf 2 m UND auf 70 cm auftaucht.  Das ist eine eigene
 * Entscheidung und nicht die dieser Frist.
 */
#define AXROUTE_PIN      300L

#define AXR_EAX25_UNKNOWN        0
#define AXR_EAX25_YES            1
#define AXR_EAX25_NO           (-1)
/* AX.25 2.2 XID capability of a station, learned from XID traffic and
 * reset exactly like the eax25 field: when the route moves, everything
 * learned about the old path goes.  See doc/AX25-2.2-GAP.md.
 */
#define AXR_XID_UNKNOWN          0
#define AXR_XID_YES              1
#define AXR_XID_NO             (-1)
/* Nur fuer die Anzeige in "ax25 route list": xid_frmr setzen, damit das
 * gezeigte Zeichen "F" meint und nicht "x" - im struct selbst ist der
 * Zustand zweigeteilt.
 */
#define AXR_XID_FRMR           (-2)

/* AX.25 2.2 XID over a DIGIPEATED LEG (point D, ax25 digipeat 2).
 *
 * We are B, and A and C are connected through us.  A sends us a XID
 * probe that is meant to reach C.  Two things then have to be answered
 * separately, and both with different parameters:
 *
 *   - The probe to C goes out over the OUTGOING port B->C, so it carries
 *     that port's paclen/window/modulo.
 *   - The answer to A goes out over the PORT we speak to A on, so it
 *     carries that port's parameters.
 *
 * The answer to A is WITHHELD until C has answered (or the timer runs
 * out): otherwise we would confirm to A a capability for the path to C
 * that C does not have - the answer to A would name a station that does
 * not exist.
 *
 * THIS block is the job during which the answer stays silent.  It belongs
 * to the DIGIPEATED path, not to an ax25_cb: there is no LAPB connection
 * to C here, only an outgoing probe on a port.
 */
struct ax25_xid_leg {
	struct ax25_xid_leg *next;	/* next job waiting */
	/* WHO waits: the station whose XID we are answering (A).  Its
	 * answer goes back over ans_iface - NOT over the outgoing port,
	 * because that is the path to it. */
	uint8 answer_to[AXALEN];		/* A */
	struct iface *ans_iface;		/* port B->A: its parameters */
	/* WHERE we probe: the next hop on the path (C) over the outgoing
	 * port.  leg_iface carries its parameters. */
	uint8 probe_to[AXALEN];		/* C */
	struct iface *leg_iface;		/* port B->C */
	/* the header for the later answer to A: seen from B the source is B
	 * itself, the destination A, and the digipeater path is empty - A
	 * sees the answer straight from B, not by the roundabout way. */
	struct ax25 *anshdr;		/* header for the answer to A */
	/* THE OTHER KIND OF JOB: not a question on somebody's behalf, but
	 * the opening of the second leg of a relayed call.  A's SABM has
	 * to be followed by a SABM(E) of OURS towards C, and the only way
	 * to learn what C can is to ask C - which the point D case above
	 * never did, because there the answer was to go to A and C never
	 * heard us ask.  So the probe leaves as B->C (link_to's port
	 * callsign as the source) and its answer lands here, and this job
	 * then carries on with the leg instead of writing to A: anshdr is
	 * null and answer_to is empty.
	 *
	 * It belongs to the SESSION, not to a frame: if that leg is gone
	 * by the time the answer comes, the job is simply dropped. */
	struct ax25_cb *link_to;		/* second leg we are opening */
	uint8 srej;               /* what his answer said in PI=3 */
	/* AND WHAT HE WILL TAKE: N1 (bits, PI=6) and window (frames, PI=8)
	 * from C's answer, to clamp the leg's own sending to - the same
	 * per-link clamp an own connect gets (ax25_apply_peer_xid).  Zero
	 * until an answer fills it. */
	unsigned n1rx;
	unsigned window;
	/* AND WHAT HE SAID ABOUT MODULO 128.  The answer that goes to A in
	 * C's name has to carry C's values and not ours: A builds its link
	 * to C from it, and a C that never answered can do no modulo 128
	 * - which is the whole verdict of a silent C.  Zero until an answer
	 * from C fills it, so a job that gives up says so. */
	uint8 mod128;
	/* HAT C GEANTWORTET?  Null heisst "nein": der Auftrag ist dann
	 * ueber die Frist abgelaufen, und das Verstummen beweist gar
	 * nichts - weder Modulo 128 noch SREJ noch 2.2.  Der Auftrag
	 * braucht das, weil er beim zweiten Bein sonst aus dem Nichts
	 * eine Faehigkeit behauptete: proto = V2_2 und ein SABME an
	 * eine Station, die sich nie gemeldet hat. */
	uint8 heard;
	/* HABEN WIR C AUCH GEFRAGT?  Ein Auftrag entsteht fuer jede
	 * geschluckte Frage (Punkt D), auch wenn die Ratenbegrenzung die
	 * Sonde spart - der Auftrag dient dann nur A's Warter.  Nur ein
	 * Auftrag, der wirklich gefragt hat, darf aus dem Ablauf ein Urteil
	 * ueber C machen (xid_leg_timeout): sonst schoebe jede weitere
	 * Anfrage von A xid_time nach vorn und C bekaeme nie wieder eine
	 * Sonde (Xid_probe_time). */
	uint8 probed;
	struct timer timer;		/* how long we wait for C, then give up */
};
extern struct ax25_xid_leg *Ax25_xid_legs;

/* HOW LONG we wait for C's XID before answering A anyway (ms).  One T1,
 * as for the connection probe: a silent C costs us exactly one wait, and
 * that is also where we learn that he does not speak XID.
 */
#define XID_LEG_TIMEOUT	10000

/* In lapb.c: */
void xid_leg_served(struct ax25_xid_leg *lp);
/* The job is through: its question was answered, or it is given up on.  The
 * answer is sent on, the leg is opened - ax25.c calls this itself when the
 * route already holds everything the answer would say, so that the station
 * waiting for it is not made to sit out our own Warter first. */
void xid_leg_done(struct ax25_xid_leg *lp);
struct ax25_xid_leg *xid_leg_find(uint8 *call, const struct iface *ifp);
void xid_leg_timeout(void *p);
/* The leg a job was opened for is being taken away - drop the job with it,
 * before its owner turns into freed memory. */
void xid_leg_drop(struct ax25_cb *owner);
/* Is somebody already asking this station, on this port?  The answer means
 * there is a question on the air and a job that will be told the outcome -
 * a second waiter must join it, not open a second one. */
int xid_leg_pending(uint8 *call, const struct iface *ifp);
/* Open a second leg with a question of our own, the way a connect that
 * starts here does - see struct ax25_xid_leg, link_to. */
int xid_leg_open(struct ax25_cb *axp);

/* Siehe eax25_fails / eax25_skips oben. */
#define EAX25_MAXFAILS           3      /* Rueckfaelle in Folge bis "kann nicht" */
#define EAX25_RETRY             15      /* Verbindungen bis zum naechsten Versuch */

/* Siehe xid_tries oben.  Hoehere Werte fragen oefter nach - drei Sonden
 * je Aufbau eine ist die Zahl, die den HF-Verlust abfaengt, ohne die
 * Station zu noetigen und ohne in Xid_probe_time hineinzuregieren.
 */
#define XID_MAXTRIES             3      /* unbeantwortete XID-Sonden in Folge */

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
/* Forget a route.  "ax25 route del" needs the same surgery axroute_add()
 * does, and a caller that only wants to drop a path must not be left
 * guessing at the chain order.  digis[] is the path in command order - the
 * next hop first, as in "ax25 route del CB1 B2" - and ndigis 0 means the
 * whole entry, every path to that destination.
 */
int axroute_del(const uint8 *call, const uint8 *const digis[], int ndigis);
int axroute_delall(void);
/* Sever the digi references from other entries that point to a node about
 * to leave the table - see axroute_unref() in ax25.c.  Used by axroute_del
 * and by the aging pass of axroute_write() (ax25file.c).
 */
void axroute_unref(struct ax_route *rp, struct ax_route *instead);

/* The ethernet address of a station on a BPQether port.  <call> is the one
 * that put the frame on the wire - the last repeater that has already
 * repeated it, or the source when there is none - because that is the
 * machine whose card we are looking at.
 */
void axroute_mac_learn(struct iface *iface, const uint8 *call,
	const uint8 *mac, int may_create);

/* May a frame with this control field and PID teach us a route at all?  A
 * plain-text broadcast may not - see learn_from() in ax25.c.
 */
int axroute_learnable(int ctl, int pid);
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
void axroute(struct ax25 *hdr, struct iface **ifpp, int keep_path);
/* Does this protocol id carry an IP datagram?  Not "is it PID_IP" - see
 * ax25_pid_is_ip() in ax25.c, which is where the reasons are.  Used to
 * decide which of iface->ipsndcnt and iface->ax25cnt a frame belongs in,
 * so that rawsndcnt == ipsndcnt + ax25cnt stays true on an AX.25 port.
 */
int ax25_pid_is_ip(int pid);

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
void axroute_flush(void);                /* write now, do not arm the timer */
void axroute_loadfile(void);

/* In axserver.c: */	// dl9sau
struct axservice;
void axserv_recv_upcall_discard(struct axservice *sp, int cnt);

#endif  /* _AX25_H */
