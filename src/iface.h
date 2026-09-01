/* @(#) $Id: iface.h,v 1.28 2002/09/19 19:11:44 dl9sau Exp $ */

#ifndef _IFACE_H
#define _IFACE_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

#include <stdio.h>

/* Interface encapsulation mode table entry. An array of these structures
 * are initialized in config.c with all of the information necessary
 * to attach a device.
 */
struct iface;   /* Defined later */
struct iftype {
	char *name;             /* Name of encapsulation technique */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
				/* Routine to send an IP datagram */
	int (*output)(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
				/* Routine to send link packet */
	char *(*format)(char *,const uint8 *);
				/* Function that formats addresses */
	int (*scan)(uint8 *,const char *);
				/* Reverse of format */
	int type;               /* Type field for network process */
	int hwalen;             /* Length of hardware address, if any */
	void (*rcvf)(struct iface *,struct mbuf **);
				/* Function that handles incoming packets */
	int (*addrtest)(struct iface *,struct mbuf *);
				/* Function that tests incoming addresses */
	void (*trace)(FILE *,struct mbuf **,int);
				/* Function that decodes protocol headers */
	int (*dinit)(struct iface *,int32,int,char **);
				/* Function to initialize demand dialing */
	int (*dstat)(struct iface *);
				/* Function to display dialer status */
        // dl9sau: patch for ARP requests (to QST) via multible digipeaters
        // for an extended "collision domain"
#define	AX_MCAST_DIGIS_MAX	8
	uint8 *ax_mcast_digis[AX_MCAST_DIGIS_MAX];
				/* possible multicast digis for ax25 ARP to QST-0 */
};
extern struct iftype Iftypes[];

/* Interface control structure */
struct iface {
	struct iface *next;     /* Linked list pointer */
	char *name;             /* Ascii string with interface name */

	int32 addr;             /* IP address */
	int32 broadcast;        /* Broadcast address */
	int32 netmask;          /* Network mask */

	uint mtu;               /* Maximum transmission unit size */

	uint trace;             /* Trace flags */
#define IF_TRACE_OUT    0x01    /* Output packets */
#define IF_TRACE_IN     0x10    /* Packets to me except broadcast */
#define IF_TRACE_ASCII  0x100   /* Dump packets in ascii */
#define IF_TRACE_HEX    0x200   /* Dump packets in hex/ascii */
#define IF_TRACE_NOBC   0x1000  /* Suppress broadcasts */
#define IF_TRACE_RAW    0x2000  /* Raw dump, if supported */
	FILE *trfp;             /* Stream to trace to */

	struct iface *forw;     /* Forwarding interface for output, if rx only */

	void (*rxproc)(void *); /* Receiver process, if any */
	struct proc *txproc;    /* IP send process */
	struct proc *supv;      /* Supervisory process, if any */

	struct mbuf *outq;      /* IP datagram transmission queue */
	int outlim;             /* Limit on outq length */
	int txbusy;             /* Transmitter is busy */

	void *dstate;           /* Demand dialer link state, if any */
	int (*dtickle)(struct iface *);
				/* Function to tickle dialer, if any */
	void (*dstatus)(struct iface *);
				/* Function to display dialer state, if any */

	/* Device dependent */
	int dev;                /* Subdevice number to pass to send */
				/* To device -- control */
	int32 (*ioctl)(struct iface *,int cmd,int set,int32 val);
				/* From device -- when status changes */
	int (*iostatus)(struct iface *,int cmd,int32 val);
				/* Call before detaching */
	int (*stop)(struct iface *);
	uint8 *hwaddr;          /* Device hardware address, if any */

	/* Encapsulation dependent */
	void *edv;              /* Pointer to protocol extension block, if any */
	int xdev;               /* Associated Slip or Nrs channel, if any */
	struct iftype *iftype;  /* Pointer to appropriate iftype entry */

				/* Routine to send an IP datagram */
	int (*send)(struct mbuf **,struct iface *,int32,uint8);
			/* Encapsulate any link packet */
	int (*output)(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
			/* Send raw packet */
	int (*raw)(struct iface *,struct mbuf **);
			/* Display status */
	void (*show)(struct iface *);

	int (*discard)(struct iface *,struct mbuf **);
	int (*echo)(struct iface *,struct mbuf **);

	/* Counters */
	int32 ipsndcnt;         /* IP datagrams sent */
	int32 rawsndcnt;        /* Raw packets sent */
	int32 iprecvcnt;        /* IP datagrams received */
	int32 rawrecvcnt;       /* Raw packets received */
	int32 lastsent;         /* Clock time of last send */
	int32 lastrecv;         /* Clock time of last receive */

	/* ARP-ANFRAGEN AUF DIESEM PORT.  0 = erlaubt, wie es immer war.
	 *
	 * Auf einem DAMA-Kanal wird er beim "dama slave" abgeschaltet
	 * (Thomas): eine ARP-Anfrage ist ein Rundspruch an QST, sie kostet
	 * den Kanal, und wer dort IP im Datagramm-Modus faehrt, hat seinen
	 * Partner ohnehin eingetragen - dann entsteht sie erst gar nicht.
	 * Wer es anders will, schaltet es mit "ifconfig <iface> arp on"
	 * wieder ein.
	 *
	 * Betroffen ist nur das FRAGEN.  Eine hereinkommende Anfrage
	 * beantworten wir weiter: das kostet nichts, was nicht ohnehin
	 * gesendet wuerde, und hilft der Gegenseite.
	 */
	int noarp;
	/* Und wer es abgeschaltet hat.  1: "dama slave" war es, dann nimmt
	 * "dama off" es auch wieder zurueck - der Grund war DAMA, faellt der
	 * Grund weg, faellt die Folge weg.  0: der Sysop hat es gesagt, und
	 * dann bleibt es, bis er etwas anderes sagt.
	 */
	int noarp_auto;

	int crccontrol;         /* CRC send control */
	int crcfixed;           /* Set by "ifconfig <if> crc": stop autodetecting */
#define CRC_OFF         0       /* Don't send CRC packets */
#define CRC_TEST_16     1       /* Send a single CRC_16 packet, then switch to CRC_TEST_RMNC */
#define CRC_TEST_RMNC   2       /* Send a single CRC_RMNC packet, then switch to CRC_OFF */
#define CRC_16          3       /* Send CRC_16 packets */
#define CRC_RMNC        4       /* Send CRC_RMNC packets */
#define CRC_CCITT       5       /* Send CRC_CCITT packets */
	/* DAMA, see dama.c.  The role is what the sysop asked for; whether it
	 * is in force depends on a master actually being heard, which is what
	 * dama_heard records.
	 */
	int dama;               /* DAMA_OFF, DAMA_SLAVE oder DAMA_MASTER */
	int32 hf_datarate;      /* Bit/s AUF DER LUFT, 0: unbekannt.  Nicht
				 * die Geschwindigkeit zum TNC - bei 9600 Baud
				 * FSK ueber eine 38400er Leitung waere die
				 * falsch (Thomas).  DAMA rechnet daraus seine
				 * Fristen. */
	int32 dama_gap;         /* als Master: Pause zwischen zwei Zuegen in
				 * ms.  0: Vorgabe (DAMA_GAP_DEFAULT).  TNN
				 * fuehrt dieselbe Groesse als Parameter
				 * (dama_init, "DAMA-Tout"), bei uns war sie
				 * bis 2026-09-01 ein festes #define. */
	/* DIE BRUECKENGRUPPE DIESES PORTS, 0 = keine.
	 *
	 * Ein Rahmen, der hier hereinkommt und dessen NAECHSTER HOP auf einem
	 * Port DERSELBEN GRUPPE zuhause ist, geht dort hinaus - unveraendert,
	 * ohne uns im Digipfad.  Das ist etwas anderes als "forward", das
	 * UNSERE eigenen Aussendungen umlenkt und fremden Verkehr gar nicht
	 * traegt.
	 *
	 * EINE NUMMER UND KEIN ZEIGER, und das ist mehr als Geschmack
	 * (Thomas' Frage nach mehr als zwei Ports): mit einem Zeiger waere
	 * eine Bruecke immer ein PAAR, und wer einen Port in eine zweite
	 * haengt, liesse seinen alten Partner still allein zurueck.  Mit
	 * einer Gruppe sind beliebig viele Ports in einer Bruecke, beliebig
	 * viele Bruecken nebeneinander, und ein Port ist immer in hoechstens
	 * einer - das ist durch die eine Zahl schon ausgedrueckt und braucht
	 * keine Pruefung.
	 */
	int bridgegroup;
	/* DUERFEN SICH ZWEI NUTZER DIESES PORTS DIREKT ERREICHEN?
	 *
	 * Vorgabe AUS, wie ueberall hier.  Eingeschaltet reichen wir einen
	 * Rahmen, der weder an uns geht noch uns im Digipfad nennt, an den
	 * Partner desselben Ports weiter - ohne uns in den Pfad zu setzen.
	 *
	 * NOETIG IST DAS, WO DIE NUTZER EINANDER NICHT HOEREN: axip/axudp und
	 * bpqether sind Punkt zu Punkt je Partner, und ein DUPLEX-Einstieg
	 * ist es auch (dort senden die Nutzer auf der Eingabe und hoeren die
	 * Ausgabe).  Auf einem gewoehnlichen Simplex-Funkkanal hoeren sie
	 * einander ohnehin, und Weiterreichen waere eine Verdopplung -
	 * deshalb entscheidet der Sysop und nicht der Porttyp.
	 */
	int user_to_user;
	int dama_policy;        /* als Master: DAMA_LAZY/PERMISSIVE/ENFORCE */
	int dama_mark_own;      /* als Slave: eigene Rahmen markieren.  Vorgabe
				 * AUS - das Bit ist das des Masters. */
	int dama_ca_set;        /* 1: WIR haben den Kanalzugriff umgestellt,
				 * also auch zurueckzunehmen.  Wie noarp_auto:
				 * was der Sysop selbst gesagt hat, nehmen wir
				 * ihm nicht wieder aus der Hand. */
	int32 dama_persist_save;/* was vorher galt */
	int32 dama_slot_save;
	int dama_watchdog;      /* Seconds of silence before we stop following
				 * a master; 0 means the built-in default */
	int32 dama_heard;       /* When a DAMA marked frame was last seen here,
				 * 0 = not following anybody */
	int32 dama_entered;     /* Times a master was found */
	int32 dama_lost;        /* Times one went away again */
	int32 dama_polls;       /* Polls answered - als Slave; als Master die
				 * ausgegebenen */
	int32 dama_violations;  /* Master: Kommandos mit P von einem Slave,
				 * der selbst gepollt hat */
	int dama_window;        /* Set only while lapb_input() handles a poll
				 * on this port - the whole of a slave's
				 * permission to transmit, and it belongs to
				 * the station, not to one connection */
	uint8 dama_sender[7];   /* WER ZULETZT DAMA GESPROCHEN HAT, und nicht
				 * mehr als das.  Frueher hiess das Feld
				 * dama_master, und es stand der falsche Name
				 * ueber dem richtigen Wert: gespeichert wurde
				 * die QUELLE eines markierten Rahmens, und die
				 * ist bei store-and-forward das Rufzeichen des
				 * fernen Nutzers, nicht das des Masters.  Wer
				 * der Master ist, laesst sich auf der Leitung
				 * ueberhaupt nicht feststellen - das Bit sagt
				 * "spricht DAMA", eine Digi-Rolle sagt
				 * "wiederholt gerade", und ein Kommando mit P
				 * sendet jede Station, wenn T1 ablaeuft.
				 * Gespeichert wird jetzt, WER GESENDET HAT
				 * (letzter wiederholter Digi, sonst Quelle) -
				 * eine Groesse, die es wirklich gibt. */

	int32 crcerrors;        /* Packets received with CRC errors */
	int32 ax25errors;       /* Packets received with bad ax25 header */
	uint flags;             /* Configuration flags */
	/* WHICH "attach" MADE THIS PORT.  The name is the operator's choice and
	 * says nothing about the kind - db0fhn calls a tun interface "ax25",
	 * because seen from Linux that is where the AX.25 world lies, while
	 * from the node's side it goes to Linux.  "Link encap" does not help
	 * either: a tun says "None".  Set centrally in doattach(), so no
	 * attach function has to remember it.
	 */
	char *attached_as;
#define NO_RT_ADD       1       /* Don't call rt_add in ip_route */

	/* May links on this port run modulo-128, and who decides.  Zero is the
	 * default on purpose, so an interface that nobody configured behaves
	 * the way most ports should.
	 */
	/* Largest AX.25 frame this port can carry, or 0 for no limit.  A driver
	 * with a hard buffer sets it at attach and ifmtu() then refuses to be
	 * configured past it - silently dropping oversized frames, which is
	 * what 6pack does today, is the worst of the possible answers.
	 */
	int framemax;

	/* Per-port overrides for the three numbers that are otherwise one for
	 * the whole node.  Zero means "use the global".  A node with a 1k2
	 * user access and a 19k2 interlink wants two different answers, and
	 * the right window and packet length are properties of the CHANNEL.
	 */
	int paclen;
	int maxframe;
	int emaxframe;

	int eax25;
	int eax25_hinted;       /* We have already said, on this port, that
				 * somebody here speaks modulo-128.  Once per
				 * PORT and not per station: the advice names
				 * the port, so repeating it for every caller
				 * adds nothing and on a busy user access it
				 * would be a nuisance. */
/* Zero is the default on purpose: an unconfigured port answers modulo-128
 * when it is offered and never asks for it, so nothing changes for anyone
 * who has not asked for it.  Probing costs 19 s against a peer that ignores
 * SABME, once per station, and that is not a cost to hand to every
 * installation by surprise.
 */
#define EAX25_ACCEPT    0       /* answer SABME, never send one */
#define EAX25_OFF       1       /* never - answer SABME with DM */
#define EAX25_CALLER    2       /* follow the caller; ask when we originate */
#define EAX25_ALWAYS    3       /* also upgrade a caller who asked for AX.25 */

	/* Which protocols may cross this port, one bit per protocol id and
	 * direction - see pidfilter.c.  The count is there so that the send
	 * and receive paths ask one int and not a bitmap: a port nobody
	 * configured must cost nothing.
	 */
	int pidblocked[2];      /* PF_IN, PF_OUT: how many bits are set */
	uint32 pidblock[2][8];
};
extern struct iface *Ifaces;    /* Head of interface list */
extern struct iface  Loopback;  /* Optional loopback interface */
extern struct iface  Encap;     /* IP-in-IP pseudo interface */

/* Header put on front of each packet sent to an interface */
#ifdef ibm032
#define qhdr Xqhdr      /* Resolve name conflict */
#endif
struct qhdr {
	uint8 tos;
	int32 gateway;
};

extern char Noipaddr[];
extern struct mbuf *Hopper;

/* In iface.c: */
int bitbucket(struct iface *ifp,struct mbuf **bp);
int if_detach(struct iface *ifp);
struct iface *if_lookup(char *name);
char *if_name(struct iface *ifp,char *comment);
void if_tx(int dev,void *arg1,void *unused);
struct iface *ismyaddr(int32 addr);
void network(int i,void *v1,void *v2);
int nu_send(struct mbuf **bpp,struct iface *ifp,int32 gateway,uint8 tos);
int nu_output(struct iface *,uint8 *,uint8 *,uint,struct mbuf **);
int setencap(struct iface *ifp,char *mode);

/* In config.c: */
int net_route(struct iface *ifp,struct mbuf **bpp);

/* Smallest MTU IP can work with - RFC 791.  See mtu_ok() in iface.c. */
#define MTU_MIN 68

int mtu_ok(const char *who,long mtu);

int if_learns_routes(struct iface *ifp);

#endif  /* _IFACE_H */
