/* @(#) $Id: lapb.c,v 1.44 2016/03/13 14:50:38 dl9sau Exp $ */

/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "dama.h"
#include "ip.h"
#include "slhc.h"
#include "pidfilter.h"
#include "framefilter.h"
#include "trace.h"
#include "xid.h"

static void handleit(struct ax25_cb *axp,int pid,struct mbuf **bp);
static void procdata(struct ax25_cb *axp,struct mbuf **bp);
static int ackours(struct ax25_cb *axp,uint n,int rex_all);
static void clr_ex(struct ax25_cb *axp);
static void enq_resp(struct ax25_cb *axp);
static void inv_rex(struct ax25_cb *axp);
static void resequence(struct ax25_cb *axp,struct mbuf **bpp,int ns,int pf,int poll);
static int ax25_v2_refused(uint8 *dest,const struct iface *ifp);
static void eax25_mark(struct ax25_cb *axp);
static void eax25_hint(struct iface *ifp,struct ax25 *hdr);
static void srej_answer(struct ax25_cb *axp,int nr);
static void est_link_sabm(struct ax25_cb *axp);	/* after an XID answer */

/* The one retransmission a SREJ asks for (3.6.4): the I-frame numbered
 * N(R), with the window left exactly where it was.  The caller has already
 * run ackours(axp,nr,0), so the frame sits at the head of txq.  It is sent
 * with its original sequence number and stays in txq for the peer's
 * ordinary ack to free; the frames behind it are not repainted.  Nothing
 * happens when the frame is not outstanding any more - a SREJ repeated for
 * a sequence the ordinary acks have meanwhile covered - and over-running a
 * SREJ would be harmless anyway, the receiver buffers in order and
 * discards the duplicate.  The specification draws the line at the one
 * frame.
 */
static void
srej_answer(
struct ax25_cb *axp,
int nr)
{
	struct mbuf *tbp = NULL;
	int xctl, xctl2 = -1;

	if(axp->txq == NULL ||
	   ((axp->vs - axp->unack) & axp->mmask) != nr)
		return;
	dup_p(&tbp,axp->txq,0,len_p(axp->txq));
	if(tbp == NULL)
		return;
	if(axp->mmask == EMMASK){
		xctl = I | (nr << 1);
		xctl2 = axp->vr << 1;
	} else
		xctl = I | (nr << 1) | (axp->vr << 5);
	sendframe(axp,LAPB_COMMAND,xctl,xctl2,&tbp);
	/* A retransmission spoils the RTT measurement for that frame. */
	axp->flags.retrans = 1;
	start_timer(&axp->t5);
}

/* AX.25 2.2 XID over a DIGIPEATED LEG (point D).
 *
 * This is where the bookkeeping for the answer to A lives: a job holds
 * A, the port to it (ans_iface), C, the port to it (leg_iface) and the
 * header for the answer.  The structure in ax25.h explains why the two
 * are kept apart.
 */
struct ax25_xid_leg *Ax25_xid_legs = NULL;

/* The answer to A, with the parameters of the port we speak to A ON -
 * not those of the outgoing port.  That difference is exactly what a
 * withheld job buys: the probe went out over B->C, the answer comes back
 * over B->A.
 */
static void
xid_leg_answer(struct ax25_xid_leg *lp)
{
	struct mbuf *tbp = NULL;
	struct ax25_cb *axp;

	/* The port the answer has to leave by must still be there. */
	if(lp->ans_iface == NULL || lp->anshdr == NULL)
		return;
	/* OFF SCHWEIGT AUCH HIER.  ax25-v2.2 off ist der Schalter "diese
	 * Station kennt kein XID", und der gilt fuer Antwort UND Frage.
	 * Diese Antwort ginge ueber ans_iface (den Port zu A) hinaus; steht
	 * der auf off, dann darf A von uns keine XID sehen - es ist genau
	 * dieselbe Aussage wie bei einer Frage, nur die andere Richtung.
	 * A wartet dann seine eigene Frist ab und klopft mit SABM an, wie
	 * gegen eine echte 2.0-Station (Thomas, 2026-10-09). */
	if(lp->ans_iface->ax25_v2 == AX25V2_OFF)
		return;
	axp = cr_ax25(lp->answer_to);
	if(axp == NULL)
		return;
	/* DER WEG ZURUECK IST DER WEG HIN, UMGEDREHT.
	 *
	 * Quell- und Zielrufzeichen tauschen, die Digis von hinten nach
	 * vorn, die schon passierten mit dem Wiederholt-Bit - das ist
	 * build_path(..., YES, ...), derselbe Aufruf, den eine Station
	 * macht, deren Frage wirklich bei ihr ankam (lapb.c:425).  Wir
	 * stehen darin an erster Stelle, also ist nextdigi unsere Position
	 * plus eins: B hat seine eigene Stelle im Weg hinter sich.
	 *
	 * Vorher stand hier ein handgebauter Kopf mit ndigis = 0, und die
	 * Antwort an A sah damit aus, als haette A die Station direkt
	 * gehoert - kein Digipeater, kein Stern.  Im Labor mit einem Hop
	 * war das unschuldig, weil A an Bs eigenem Port haengt; mit einem
	 * Digipeater dazwischen war es der Tod der Antwort: Sie ging an D
	 * hinaus, wo niemand sie bestellt hatte, und A wartete weiter auf
	 * eine Station, die ihm nie geantwortet hat.
	 *
	 * Ausdruecklich NICHT axroute(): das wuerde den Weg aus der
	 * Routing-Tabelle dazumischen und den empfangenen wegwerfen - der
	 * ist die Antwort auf die Frage nach dem Weg.
	 */
	axp->hdr = *lp->anshdr;
	build_path(axp, lp->ans_iface, lp->anshdr, YES, NULL);
	/* EAX25 OFF HEISST "ICH FRAGE NICHT", NICHT "ICH SCHWEIGE".  A port
	 * at eax25 off puts no XID of its own on the air, but a question that
	 * is aimed at us gets an answer - that is what answering a XID is
	 * for, and a silent port would leave A probing again and again
	 * against a port that has perfectly good information.  What the
	 * answer must not claim is a capability the port has switched off,
	 * and that is decided by eax25 below, separately.  (Der 2.2er Knopf
	 * ax25-v2.2 off ist strenger und schweigt schon oben: er sagt, dass
	 * dieser Port kein XID kennt.) */
	/* IN C'S NAMEN, ALSO MIT C'S WERTEN.  Diese Antwort geht fuer A
	 * raus und A baut daraus seine Verbindung NACH C - sie beschreibt
	 * das Bein B->C und nicht das Bein B->A, aus dem sie herauskommt.
	 * Ein B, der hier seine eigenen Parameter eintraegt, wuerde einem
	 * schweigenden C (srej bleibt dann null, mod128 auch) ein
	 * Modulo-128-Vermoegen andichten, das niemand nachgewiesen hat:
	 * A spricht danach SABME, und die Antwort, die darauf ausbleibt,
	 * ist A's Problem fuer eine Behauptung, die wir uns ausgedacht
	 * haben.  Also stehen hier die Werte, die C selbst genannt hat
	 * (xid_leg_served) oder die das Verstummen ergeben hat
	 * (xid_leg_done nach XID_LEG_TIMEOUT - dort bleibt mod128 null).
	 *
	 * EAX25 OFF DARF NICHTS BEHAUPTEN, WAS ES ABGESCHALTET HAT - das
	 * gilt auch fuer eine Antwort, die einem anderen die Faehigkeiten
	 * des C vorgibt.  Der 2.0er Knopf (eax25 off) nimmt der Antwort das
	 * Modulo-128-Bit im EAX25-Feld: B gibt A nicht C's Modulo 128
	 * weiter, wenn B es auf diesem Port selbst nicht anbietet.  A sieht
	 * damit "C: nur Modulo 8" und handelt danach - was der Knopf
	 * bewirken soll.
	 *
	 * Der 2.2er Knopf (ax25-v2.2 off) ist strenger und schweigt schon
	 * oben: eine Station, die kein XID kennt, antwortet auf eines gar
	 * nicht.  Im Labor stand er frueher auf b0, und B reichte C's
	 * Modulo 128 trotzdem an A weiter:
	 *
	 *   EAX25: DB0CCC->DB0AAA XID(F) ... SREJ mod128 N1rx=2048 kRx=32
	 *
	 * A baute daraufhin SABME auf, und die Station, die SREJ und Modulo
	 * 128 nicht kann, war der Digipeat selbst.  Das ist der Fehler, den
	 * der fruehe Ausstieg oben behebt (Thomas, 2026-10-09).
	 */
	if(lp->ans_iface->eax25 == EAX25_OFF)
		xid_our_info(lp->ans_iface, 0, lp->srej,
			     ax25_hop2hop(), &tbp);
	else
		xid_our_info(lp->ans_iface,
			     lp->mod128 ? 1 : 0,
			     lp->srej, ax25_hop2hop(), &tbp);
	sendframe(axp,LAPB_RESPONSE,XID | PF,-1,&tbp);
	del_ax25(axp);
}

/* THE LEG IS GONE while the question is on the air.  A gave up, the
 * connection was refused, someone hit "reset": the block the job points at is
 * on its way to free(), and a job that kept that pointer would carry the
 * address of it into its own callback.  So the owner of the pointer drops the
 * job here, where it is still valid - nothing is lost, because what the job
 * was going to do was to continue a leg that no longer exists.
 */
void
xid_leg_drop(struct ax25_cb *owner)
{
	struct ax25_xid_leg *lp, **pp;

	/* `next` must be read from a LIVE node - it is the FIRST field of the
	 * block, so free() writes its own bookkeeping over it (glibc tcache
	 * puts the freelist pointer there), and a loop that then advanced to
	 * &lp->next and read *pp would treat the freed block as the next node.
	 * So step pp forward only when the node is kept; after a delete *pp
	 * already points at the successor. */
	for(pp = &Ax25_xid_legs; (lp = *pp) != NULL; ){
		if(lp->link_to == owner){
			*pp = lp->next;
			stop_timer(&lp->timer);
			free(lp->anshdr);
			free(lp);
		} else
			pp = &lp->next;
	}
}

/* A JOB IS OVER - because C spoke, or because he did not.  What "over" means
 * depends on whose job it was: a question on A's behalf is answered to A, a
 * second leg of our own is CARRIED ON.  What it never means is a frame in
 * C's name on the strength of our guess: a station we have not heard is
 * never written about.
 */
void
xid_leg_done(struct ax25_xid_leg *lp)
{
	struct ax25_xid_leg **pp;
	struct ax25_cb *axp;
	int want;

	/* Off the list BEFORE doing anything - else the answer we are about
	 * to send could land back here and run the job a second time. */
	for(pp = &Ax25_xid_legs; *pp != NULL; pp = &(*pp)->next){
		if(*pp == lp){
			*pp = lp->next;
			break;
		}
	}
	stop_timer(&lp->timer);
	if((axp = lp->link_to) != NULL){
		/* The leg may be gone by now: A gave up, or the connection was
		 * refused, while the question was on the air.  Ten seconds is
		 * long enough for that.
		 *
		 * SETUP gehoert hier in dieselbe Schublade wie DISCONNECTED, und
		 * das ist der Normalfall: der Anrufer setzt den Zustand direkt
		 * nach est_link() auf SETUP, also noch ehe ein SABM auf der
		 * Leitung war - SETUP heisst bei diesem Bein "im Aufbau", und
		 * genau das fehlende SABM ist es, was wir jetzt schicken.  Nur
		 * DISCONNECTED zuzulassen wuerde das Bein nach jedem
		 * Relayed-SABME fuer immer offen lassen: der Zustand waere
		 * schon SETUP, wenn die Antwort kommt.
		 */
		if(ax25_alive(axp) && (axp->state == LAPB_DISCONNECTED ||
		                       axp->state == LAPB_SETUP)){
			struct ax_route *rp = ax_routeptr(lp->probe_to,0);
			/* The one proof of 2.2 that counts, and one of only two
			 * places that may set it: he ANSWERED a XID of ours,
			 * in this session.  What his answer said in PI=3 travels
			 * with it (lp->srej): a leg he opened for us is a leg we
			 * are going to send SREJs on. */
			/* NUR GEGEN EINEN BEWEIS, UND DER BEWEIS LIEGT IN
			 * DIESEM AUFTRAG.  est_link() hat den Modulus
			 * gesetzt, BEVOR die Frage draussen war, aus dem,
			 * was der Eintrag dafuer hergab - bei einer
			 * unbekannten Station also aus dem Knopf des
			 * Ports (initiate heisst EMMASK).  Das ist eine
			 * Absichtserklaerung, kein Beweis, und sie
			 * genuegt fuer ein Bein nicht: C, das nicht
			 * antwortet, antwortet einem SABME erst recht
			 * nicht, und der Aufrufer wartet N2 SabMEs
			 * lang auf eine Antwort, die nicht kommen
			 * kann.  Also sagt der Auftrag, was C
			 * tatsaechlich gesagt hat - oder er schweigt mit
			 * ihm, und dann laeuft das Bein modulo 8.
			 *
			 * accept-or-XID, und hier ist es die zweite
			 * Haelfte: die Frage hat die Wartezeit bezahlt,
			 * und jetzt zaehlt, was auf der Leitung war.  Ein
			 * Eintrag, der "C kann Modulo 128" sagt, ist
			 * derselbe Beweis in aelterer Form und darf
			 * weiterhin genuegen (doc/AX25-2.2-GAP.md).
			 */
			if(lp->heard){
				axp->proto = V2_2;
				if(lp->srej)
					axp->flags.peer_srej = YES;
				want = lp->mod128 ? EMMASK : MMASK;
			} else {
				/* DAS VERSTUMMEN BEWEIST AUCH KEIN 2.2. */
				axp->proto = V1;
				/* UND ES ENTSCHEIDET AUCH NICHT UEBER DEN
				 * MODULUS.  Ein stummes C hat nichts gesagt -
				 * was auf diesem Bein gesendet wird, sagt
				 * deshalb der Knopf des Ausgangsports, genau
				 * wie bei einem Aufbau ganz ohne XID-Frage:
				 * "off"/"accept" oeffnen schlicht mit SABM,
				 * "initiate"/"always" wagen das SABME und
				 * lassen den gewoehnlichen Rueckfall (drei
				 * Proben, lapbtime.c recover()) das Urteil
				 * faellen.
				 *
				 * Vorher erzwang die Stille hier MMASK.  Das
				 * machte aus dem Einschalten von ax25-v2.2
				 * eine Verschaerfung: ein Port auf eax25
				 * "initiate", der ohne Frage ein SABME
				 * schickte, sendete mit der Frage auf einmal
				 * ein SABM an ein stummes C - also das
				 * Gegenteil dessen, was der Knopf verspricht
				 * (Thomas, 2026-10-09). */
				want = (axp->iface != NULL &&
					(axp->iface->eax25 == EAX25_INITIATE ||
					 axp->iface->eax25 == EAX25_ALWAYS))
				       ? EMMASK : MMASK;
			}
			if(axp->iface != NULL &&
			   axp->iface->eax25 != EAX25_OFF &&
			   axp->mmask != want &&
			   (lp->heard
			    ? (lp->mod128 ||
			       (rp != NULL && rp->eax25 == AXR_EAX25_YES))
			    : YES)){
				axp->mmask = want;
				eax25_mark(axp);
			}
			/* Und was C ueber sein Empfangen gesagt hat, kappt
			 * dieses Bein - wie bei einem eigenen Connect. */
			ax25_apply_peer_xid(axp, lp->n1rx, lp->window);
			est_link_sabm(axp);
		}
	} else if (lp->heard)
		xid_leg_answer(lp);
	else {
		/* STILLE IST STILLE.
		 *
		 * C hat nicht geantwortet, und damit ist ueber C nichts
		 * bekannt - er kann stumm sein, er kann weg sein, er kann
		 * gar nicht existieren.  Ein Rahmen in seinem Namen waere
		 * eine Behauptung ueber eine Station, die sich nicht zu
		 * Wort gemeldet hat, und die Werte darin waeren Bs
		 * Portparameter: "C kann nur Modulo 8, SREJ ja, kRx=7,
		 * N1rx=2048".  Genau das stand hier früher, zehn Sekunden
		 * nach As Frage, mit A als Empfaenger - der Aufbau hat
		 * funktioniert, und A hat trotzdem etwas erfundenes
		 * geglaubt.  Ein SREJ und ein Fenster, die niemand
		 * vereinbart hat, sind keine Kleinigkeit: A rechnet damit
		 * weiter, bis die Verbindung ohnehin faellt.
		 *
		 * A braucht diese Antwort nicht.  Sein eigener T1 laeuft
		 * ebenfalls ab, und was er dann schickt, ist sein SABM -
		 * der geht als normaler Digipeat durch (der Aufbau schluckt
		 * nur XID-Fragen, ax25.c), und C antwortet darauf, wenn er
		 * da ist.  Es kostet A dieselbe Wartezeit, die er ohnehin
		 * gehabt haette, und es behauptet nichts.
		 *
		 * Der Kommentar an xid_leg_timeout() sagte genau das
		 * ("A gets nothing in C's name") und der hier nicht: das
		 * war der Widerspruch.
		 */
	}
	free(lp->anshdr);
	free(lp);
}

/* C DID NOT ANSWER.  We learn from that, for the PATH to C, that no XID is
 * spoken there - not that C cannot do XID, it was only a silent station, so
 * xid_frmr stays clear and the next attempt after Xid_probe_time asks again.
 *
 * A gets nothing in C's name.  A probe that came back empty says nothing
 * about C that could be put on the wire, and the two cases want different
 * things anyway: a question in A's name (Punkt D) would otherwise leave A
 * waiting for an answer that is not coming, while for a leg it is A's own
 * timer that runs out and says so.  The one thing both get is the leg below
 * opened with the SABM the wait was for.
 */
void
xid_leg_timeout(void *p)
{
	struct ax25_xid_leg *lp = (struct ax25_xid_leg *) p;
	struct ax_route *rp;

	/* NUR EIN AUFTRAG, DER WIRKLICH GEFRAGT HAT, URTEILT.  Punkt D legt
	 * fuer jede geschluckte Frage von A einen Auftrag an, auch wenn die
	 * Ratenbegrenzung die Sonde spart: der dient dann nur A's Warter.
	 * Liesse man dessen Ablauf hier durch, schoebe jede weitere Anfrage
	 * xid_time nach vorn und C bekaeme nie wieder eine Sonde - der
	 * Eintrag saehe ewig "gerade erst gefragt" aus.  Gefragt hat nur,
	 * wer gefragt hat (lp->probed).
	 *
	 * UND NUR DAS URTEIL, NICHT DER ZAEHLER.  xid_time und xid_tries
	 * hat schon der Absender der Frage gesetzt (xid_leg_open bzw. der
	 * Punkt-D-Zweig in ax25.c), so wie est_link_xid() es fuer den
	 * eigenen Aufbau tut.  Hier noch einmal zu zaehlen machte aus einer
	 * Sitzung zwei und schoebe xid_time um XID_LEG_TIMEOUT nach vorn.
	 */
	if(lp->probed && (rp = ax_routeptr(lp->probe_to,0)) != NULL){
		rp->xid = AXR_XID_NO;
	}
	/* UND KEIN URTEIL UEBER MODULO 128.  Wer auf eine XID-Anfrage
	 * nicht antwortet, hat damit ueber ein SABME nichts gesagt - das
	 * ist nicht dieselbe Unhoerbarkeit, sondern eine unbeantwortete
	 * Frage einer anderen Art.  Der Eintrag ist das, was der naechste
	 * Aufruf liest: waere hier ein "kann nicht" vermerkt, machte das
	 * Schweigen einer Frage aus dem Ausgangsport (initiate heisst
	 * EMMASK) auf einmal ein SABM-Bein und behielte das bei, solange
	 * die Route lebt - ueber eine Station, die nur weg war.  Was er
	 * kann, entscheidet deshalb der Port dieses Beins, so wie bei
	 * einem Aufbau ganz ohne Frage (xid_leg_done(); "What counts as
	 * evidence", doc/EAX25.md; Thomas, 2026-10-13).
	 */
	xid_leg_done(lp);
}

/* Find the job waiting for a XID answer from this station - on THIS port.
 * The port is part of the question, not a detail of it: C can be reachable
 * two ways, and an answer that arrives on one of them says nothing about the
 * other.  Comparing the callsign alone served the wrong job.
 */
struct ax25_xid_leg *
xid_leg_find(uint8 *call, const struct iface *ifp)
{
	struct ax25_xid_leg *lp;

	for(lp = Ax25_xid_legs; lp != NULL; lp = lp->next)
		if(lp->leg_iface == ifp && addreq(lp->probe_to,call))
			return lp;
	return NULL;
}

/* C HAS answered, and the job is done - but what "done" is depends on whose
 * job it was, which is xid_leg_done()'s business: a question in A's name is
 * answered to A (with the port B->A parameters, B's own), a second leg of
 * ours simply goes on to the SABM it was waiting for.  Neither writes
 * anything in C's name but the fact that he spoke.
 *
 * What was learned about C is recorded by the XID handler that calls this,
 * once, for all the jobs.
 */
void
xid_leg_served(struct ax25_xid_leg *lp)
{
	/* DIE EINZIGE STELLE, DIE "C HAT GESPROCHEN" FESTHALTEN KANN -
	 * und die Frage geht ueber xid_leg_find() an JEDEN Auftrag
	 * dieser Frage, nicht an einen.  Der Bein-Auftrag darf aus dem
	 * Verstummen keine Faehigkeit machen (struct ax25_xid_leg in
	 * ax25.h), und das darf er nur, wenn es hier gesetzt wurde. */
	lp->heard = YES;
	xid_leg_done(lp);
}

/* SREJ NUR GEGEN EINEN BEWEIS.  peer_srej sagt, was er im PI=3 genannt hat,
 * und das ist fuer sich genommen eine Behauptung: eine Station kann XID
 * senden, ohne 2.2 zu beherrschen, und dann kaeme ein SREJ bei einem V1- oder
 * V2.0-Partner heraus, der nur ein REJ versteht - und der wartet vergebens
 * auf ein Fenster, das nie kommen wird.  Deshalb verlangt die Benutzung
 * zusaetzlich axp->proto == V2_2.  Das setzen zwei Beweise vom Draht:
 *  - eine XID-ANTWORT, in dieser Sitzung oder gemerkt aus einer frueheren
 *    (est_link), solange Xid_expire_time sie haelt - ein bestaetigter XID,
 *  - ein SREJ, das er selbst schickt - ein unbestaetigter XID: er zeigt die
 *    Faehigkeit wie ein SABME das eax25 zeigt, nennt aber keine Parameter
 *    (N1/k/Segmenter stehen allein in seiner Antwort).  Er ist darum nur
 *    fuer die Sitzung gut, in der er kam, und tastet den gelernten Vermerk
 *    nicht an (lapb.c, SREJ-Fall).
 * Ein Vermerk OHNE Antwort genuegt nicht - und die Frist ist der Grund, warum
 * der gemerkte Beweis es darf: 1,5 Tage sind nicht die Lebensdauer des
 * Routeneintrags (24,8 Tage) (doc/AX25-2.2-GAP.md).
 */
static int
srej_ok(struct ax25_cb *axp)
{
	return axp->flags.peer_srej && axp->proto == V2_2;
}

/* Process incoming frames */
int
lapb_input(
struct iface *iface,
struct ax25 *hdr,
struct mbuf **bpp               /* Rest of frame, starting with ctl */
){

	struct ax25_cb *axp;    /* Link control structure */
	enum lapb_cmdrsp cmdrsp = hdr->cmdrsp;  /* Command/response flag */
	int control;
	int controlx = -1;      /* Second control octet, modulo-128 only */
	int class;              /* General class (I/S/U) of frame */
	uint type;              /* Specific type (I/RR/RNR/etc) of frame */
	int made_here = 0;      /* the frame had no link, so one was made */
	char pf;                /* extracted poll/final bit */
	char poll = 0;
	char final = 0;
	uint nr = 0;            /* ACK number of incoming frame */
	uint ns = 0;            /* Seq number of incoming frame */
	uint tmp;
	int digipeat;

	if(bpp == NULL || *bpp == NULL){
		free_p(bpp);
		return -1;
	}

	/* Extract the various parts of the control field for easy use */
	if((control = PULLCHAR(bpp)) == -1){
		free_p(bpp);    /* Probably not necessary */
		return -1;
	}
	type = ftype(control);
	class = type & 0x3;
	/* Are we the addressee, or are we being asked to relay?  Besides the
	 * interfaces' own callsigns this has to count the ones our links use
	 * and the ones we listen for - see ax_answers_to().  Widened, never
	 * narrowed: a frame for another interface's callsign counts as ours
	 * here exactly as it always did.
	 */
	digipeat = (ismyax25addr(hdr->dest) == NULL
		    && !ax_answers_to(iface,hdr->dest));
	if(digipeat){
		struct ax25_cb *axlast = NULL;
		for(axp = Ax25_cb; axp; axlast = axp,axp = axp->next)
			if(addreq(hdr->source,axp->hdr.dest) &&
			    addreq(hdr->dest,axp->hdr.source)){
				if(axlast != NULL){
					axlast->next = axp->next;
					axp->next = Ax25_cb;
					Ax25_cb = axp;
				}
				break;
			}
	} else
		/* THE PAIR, exactly as the digipeat branch above has always
		 * done it, only the other way round: the frame is addressed
		 * to us, so hdr->dest is the callsign of ours it was sent to
		 * and hdr->source is his.  Looking him up alone found the
		 * first link to him under ANY of our callsigns - and a SABM
		 * landing on that link is answered with UA and resets its
		 * counters, so a second session to a different service of
		 * ours tore down the first instead of opening beside it.
		 */
		axp = find_ax25(hdr->dest,hdr->source);
	if(axp == NULL){
		/* No address yet: build_path() fills the header in, and the
		 * VJ setting with it, from the frame.
		 */
		axp = cr_ax25(NULL);
		made_here = 1;
		build_path(axp,iface,hdr,1,0);
		if(digipeat){
			axp->peer = cr_ax25(NULL);
			axp->peer->peer = axp;
			build_path(axp->peer,NULL,hdr,0,0);
		}
	}

	if(cmdrsp == LAPB_UNKNOWN)
		axp->proto = V1;        /* Old protocol in use */

	/* A SABM or SABME does not merely open a link, it states which modulus
	 * the link runs on, and either may arrive at any time - including on
	 * one that is already up.  Taking what the frame says, here and once,
	 * is what AX.25 2.2 asks for and what the Linux kernel does in
	 * ax25_std_state3_machine().  Deciding it separately in every state is
	 * how the two drifted apart in the patch this was built from.
	 */
	/* A port set to "eax25 off" refuses modulo-128 from the other side too.
	 * DM is the right answer and not silence: it tells him at once that we
	 * are here and that this is not the way, so his own fallback starts
	 * now rather than after his retries run out.
	 */
	/* EIN BANN NACH ZU VIELEN DAMA-VERSTOESSEN, und dieselbe Antwort wie
	 * gleich darunter: DM sagt ihm sofort, dass wir da sind und dass es so
	 * nicht geht.  Wer mit gesetztem DAMA-Bit anklopft, kommt herein - das
	 * prueft dama_connect_refused() selbst.
	 */
	if((type == SABM || type == SABME) && dama_connect_refused(iface,hdr)){
		sendctl(axp,LAPB_RESPONSE,DM | (control & PF));
		free_p(bpp);
		return 0;
	}

	if(type == SABME && iface != NULL && iface->eax25 == EAX25_OFF
	   && !digipeat){
		eax25_hint(iface,hdr);
		sendctl(axp,LAPB_RESPONSE,DM | (control & PF));
		free_p(bpp);
		return 0;
	}

	/* The incoming "conn" gate.  It refuses CALLS, not the traffic of a
	 * connection that is alive: a SABM/SABME for a pair with no live
	 * connection is answered with DM, as the two refusals above do, and
	 * the frames that go with such a call - UA, DM, I, S, DISC - are
	 * dropped.  But the UA that answers the SABM WE sent arrives on a
	 * link in SETUP or CONNECTED - it comes home, as does everything on
	 * an established link, for that link is ours too.  A DISCONNECTED
	 * leftover counts as no connection: the session is over, so a SABM
	 * landing on it is a fresh call and is refused, not re-established.
	 * Digipeated frames are relayed untouched, exactly as the "pid" gate
	 * leaves digipeating alone (framefilter.c).
	 */
	if(frame_blocks(iface,PF_IN,FRF_CONN) && !digipeat
	   && axp->state == LAPB_DISCONNECTED){
		if(type == SABM || type == SABME){
			sendctl(axp,LAPB_RESPONSE,DM | (control & PF));
			free_p(bpp);
			return 0;
		}
		free_p(bpp);
		return 0;
	}

	if(type == SABM || type == SABME){
		axp->mmask = (type == SABME) ? EMMASK : MMASK;
		if(type == SABME){
			axp->hdr.ext |= SSID_EAX25;
			/* He is calling US with it, so he can do it - the one
			 * piece of evidence that needs no probe, and the one
			 * that undoes an earlier "cannot" the moment his end
			 * is fixed.
			 */
			eax25_remember(hdr->source,AXR_EAX25_YES);

			eax25_hint(iface,hdr);
		} else
			axp->hdr.ext &= ~SSID_EAX25;
	}

	/* Now that the link is known, the rest of the control field can be
	 * read: modulo-128 carries the sequence numbers and the poll/final bit
	 * in a SECOND octet, and only for I and S frames.  A U frame has no
	 * sequence number to widen and keeps its single octet in both moduli -
	 * which is what makes SABME readable at all before anything has been
	 * agreed.
	 */
	if(class != U && axp->mmask == EMMASK){
		if((controlx = PULLCHAR(bpp)) == -1){
			free_p(bpp);
			return -1;
		}
		/* Normalize to PF: downstream, sendctl() maps PF back to
		 * PF_EAX25 when it has to put the bit in the second octet.
		 * Handed on unnormalized (0x01), a REJ/RR response built as
		 * "cmd|pf" would silently lose the F bit on a modulo-128 link.
		 */
		pf = (controlx & PF_EAX25) ? PF : 0;
	} else
		pf = control & PF;

	/* Check for polls and finals */
	if(pf){
		switch(cmdrsp){
		case LAPB_COMMAND:
			poll = YES;
			break;
		case LAPB_RESPONSE:
			final = YES;
			break;
		default:
			break;
		}
	}

	/* Extract sequence numbers, if present */
	switch(class){
	case I:
	case I+2:
		ns = (control >> 1) & axp->mmask;
	case S: /* Note fall-thru */
		nr = (axp->mmask == EMMASK) ? ((controlx >> 1) & EMMASK)
					    : ((control >> 5) & MMASK);
		break;
	}

	/* DAMA.  The MASTER's SSID octet carries the bit, so on a connection
	 * the user placed it arrives in the UA, not in the SABM - the SABM is
	 * ours.  Any marked frame feeds the watchdog and puts this port into
	 * DAMA mode; from then on a command with the poll bit is a poll,
	 * whether or not that particular frame is marked as well.
	 *
	 * Deliberately the liberal reading, and it is not what TNN does: TNN
	 * wraps its poll test in "if (rxfDA)" and so needs the bit on every
	 * polling frame, while Linux latches the mode at connect and then
	 * looks only at command+P.  The paper allows either - "it would be
	 * sufficient to tell the user to switch to DAMA mode only once, at
	 * connect time" - so a master may legitimately mark nothing after the
	 * UA, and requiring the bit per frame would leave us silent for ever
	 * against such a master.  Accepting both costs nothing: the watchdog
	 * already handles a master that stops speaking DAMA at all.
	 */
	if(hdr->ext & SSID_DAMA){
		dama_heard_frame(iface,hdr);
		axp->dama_link = 1;
	}
	dama_poll_begin(iface,poll,hdr);
	/* Und die andere Rolle: auf einem Master-Port endet hier der Zug des
	 * Gepollten (F-Bit), und hier wird gezaehlt, wer selbst pollt.
	 */
	dama_master_input(iface,axp,hdr,class == U,class == I,poll,
			  (cmdrsp == LAPB_RESPONSE) && (control & PF));

	/* A TEST command asks for nothing but a TEST back: the information
	 * field, if there is one, is returned, the P/F bit is copied into the
	 * response, and neither the link state nor the sequence variables move
	 * (4.3.3.8).  It may legally arrive in any state, so it is answered
	 * here, clear of the per-state dispatch below - which would otherwise
	 * not know it and might, at best, drop it or, at worst, answer with a
	 * DM.  A TEST response is the echo of our own sending: it changes
	 * nothing either, and it is made available here, the only consumer
	 * who ever asked.  The line above already counted the poll, so on a
	 * DAMA channel a command with P gets its answer, not an unsolicited
	 * transmission.
	 */
	if(type == TEST){
		if(cmdrsp == LAPB_COMMAND){
			sendframe(axp,LAPB_RESPONSE,TEST | pf,-1,bpp);
			free_p(bpp);
			return 0;
		}
		/* A TEST response is the echo of a TEST command we sent:
		 * it changes no state and is handed back here, where whoever
		 * sent the command is listening.
		 */
		{
			char callbuf[8];

			printf("AX.25 test from %s: %lu bytes\n",
				pax25(callbuf,hdr->source),
				(unsigned long)(*bpp != NULL ? len_p(*bpp) : 0));
		}
		free_p(bpp);
		return 0;
	}

/* An XID command is usually answered with an XID response (4.3.3.7):
	 * answering "here is what I can do" binds us to nothing, and refusing
	 * would only hide a capability the far end relies on.  The knob
	 * governs what WE start - a station at accept answers as politely as
	 * one at always.  EXCEPT at off: a port that is told to behave like
	 * plain AX.25 2.0 answers nothing, so that a v2.2 initiator probing
	 * it hears silence and falls back to SABM - a pure 2.0 station that
	 * this port is pretending to be, down to not knowing XID.  It is an
	 * unnumbered frame and may arrive in any state, so it is handled
	 * here, clear of the per-state dispatch - the way TEST is.
	 *
	 * The frame also tells us something for the route: a station that
	 * deals in XID is XID-capable whatever its answer contains, and
	 * the PI=3 optional functions say at once whether it selects
	 * SREJ.  Both are remembered (doc/AX25-2.2-GAP.md).
	 */
	if(type == XID){
		struct xid_info xi;
		struct mbuf *tbp = NULL;
		struct ax_route *rp;

		xid_parse(bpp,&xi);
		/* NUR EINE ANTWORT SCHLOSSS DEN AUSTAUSCH.  Wer zuerst gefragt
		 * hat, ist nebensache - gefragt haben kann er nur, wenn er
		 * selbst eine Antwort bekommen hat, und dann ist beides
		 * geschehen.  Umgekehrt ist eine XID von ihm die Haelfte des
		 * Austauschs: darauf antworten wir, und ueber sich selbst hat
		 * er uns dann nichts gesagt (Thomas, 2026-09-25).  Das Feld
		 * stand vorher auf "spricht XID" und wurde aus JEDER XID
		 * gesetzt - eine Station, die nur fragte, wurde deshalb nie
		 * gefragt, denn Ratenbegrenzung und est_link() lesen es beide.
		 * Jetzt heisst es: wir halten seine Antwort.
		 *
		 * SEIN mod128-BIT GEHOERT DAZU, und es ist ein eigenhaendiger
		 * Befund ueber IHN, nicht eine Meldung ueber einen Dritten:
		 * wir haben C als B gefragt, C hat B geantwortet.  Die
		 * Antwort, die B in C's Namen weiterreicht, ist etwas anderes
		 * und wird nicht gelernt (doc/AX25-2.2-GAP.md).
		 *
		 * WIE LANGE EIN BEWIESENER VERMERK GILT, ist Xid_expire_time
		 * (1,5 Tage) und NICHT Xid_probe_time.  Die eine Stunde
		 * gehoert einer Station, von der wir noch NICHTS wissen: dort
		 * soll der naechste Versuch die Wartezeit nicht nochmal
		 * bezahlen, aber nach einer Stunde doch wieder fragen.  Wer
		 * geantwortet hat, wird so lange nicht wieder gefragt; danach
		 * wird neu ausgehandelt, und ax25.c loescht beide Vermerke
		 * ohnehin, sobald sich Weg oder Port aendern.
		 *
		 * Der Preis davon ist eine Station, deren Betreiber die
		 * Software gewechselt hat: der Vermerk riecht es erst, wenn
		 * Xid_expire_time abgelaufen ist (oder der Eintrag faellt).
		 * Das ist unkritisch, solange nichts 2.2-Wissen OHNE neuen
		 * Beweis auf den Draht bringt - das Modul faellt am DM
		 * zurueck, die Antwort an A am DM von A - und die einzige
		 * Stelle, die das nicht auffaellt, ist SREJ.
		 * SREJ darf darum nur bei axp->proto == V2_2.  Das setzen
		 * Beweise vom Draht: eine XID-Antwort (in dieser Sitzung,
		 * oder gemerkt aus einer frueheren, solange Xid_expire_time
		 * sie haelt), ein SREJ, das er selbst schickt, und fuer ein
		 * Bein seine Antwort auf unsere Frage - srej_ok()
		 * (doc/AX25-2.2-GAP.md).
		 */
		if(cmdrsp == LAPB_RESPONSE &&
		   (rp = ax_routeptr(hdr->source,0)) != NULL){
			rp->xid = AXR_XID_YES;
			rp->xid_time = secclock();
			rp->xid_tries = 0;
			eax25_remember(hdr->source,
				       xi.mod128 ? AXR_EAX25_YES
						 : AXR_EAX25_NO);
			/* UND WAS ER UEBER SEIN EMPFANGEN GESAGT HAT,
			 * BLEIBT STEHEN - fuer die naechste Sitzung, nicht
			 * nur fuer diese.  Eine gemessene Antwort ist eine
			 * Auskunft ueber die STATION; est_link() wendet sie
			 * wieder an, und erst Xid_expire_time laesst sie
			 * verfallen (ax25.h, struct ax_route). */
			rp->peer_n1rx = xi.n1rx;
			rp->peer_window = xi.window;
			rp->peer_srej = xi.srej;
			rp->peer_segment = xi.segment;
			rp->peer_xid_time = rp->xid_time;
		} else if(cmdrsp == LAPB_COMMAND){
			/* SEINE FRAGE IST AUCH EINE AUSKUNFT UEBER IHN.
			 * Ein XID-Kommando traegt die Faehigkeit des
			 * ABSENDERS, nicht eine Frage danach: sagt sein
			 * mod128-Bit ja, kann er Modulo 128, und est_link()
			 * liest das beim naechsten Aufbau - ohne ihn erst
			 * selbst fragen zu muessen.  Das xid-Urteil bleibt
			 * "nur eine Antwort zaehlt": wer nur fragt, hat noch
			 * nichts beantwortet.  Der eigene Befund ueber IHN
			 * steht aber in der Frage (Thomas). */
			eax25_remember(hdr->source,
				       xi.mod128 ? AXR_EAX25_YES
						 : AXR_EAX25_NO);
		}
		/* C's answer to our leg probe.  EVERY JOB IS WORKED OFF, not
		 * the first one found: A, A-1 and Y may all have wanted the
		 * same unknown C at the same moment, and one answer settles it
		 * for all of them.  What was learned about C is the same for
		 * everyone and happened once, above. */
		if(cmdrsp == LAPB_RESPONSE){
			struct ax25_xid_leg *lp;

			/* AUCH EINE SPAETE ANTWORT KLEMMT.
			 *
			 * Sie kann nach unserer Wartezeit kommen, wenn das
			 * SABM schon draussen ist (HF).  Sie kam aber, und
			 * was sie ueber sein Empfangen sagt, bleibt gueltig:
			 * unser SENDEN wird begrenzt, nie behaupten wir
			 * etwas ueber ihn (Thomas: "klemmen").  Der
			 * neg_wait-Fall unten wendet dasselbe an; die
			 * Dopplung schadet nicht, es ist ein Minimum.  Ein
			 * Bein-Auftrag (made_here) steht auf DISCONNECTED
			 * und wird hier nicht angefasst - C's Grenzen fuer
			 * das Bein setzt der Bein-Code selbst. */
			if(axp != NULL && !axp->flags.neg_wait &&
			   axp->state != LAPB_DISCONNECTED)
				ax25_apply_peer_xid(axp, xi.n1rx, xi.window);

			/* DER BEWEIS AUS DIESER SITZUNG.  Eine Station, die
			 * sich mit 2.2 (PI=3, SREJ) bruest, hat damit noch
			 * nichts gezeigt - sie hat nur gefragt.  Erst ihre
			 * ANTWORT tut das, und zwar in dieser Sitzung, was
			 * genau der Nachweis ist, den SREJ braucht
			 * (srej_ok).  Fuer eine spaetere Sitzung traegt der
			 * Routeneintrag denselben Beweis weiter, solange
			 * Xid_expire_time ihn haelt (est_link). */
			if(axp != NULL){
				axp->proto = V2_2;
				/* Auch sein Segmenter-Bit: 1 kann, -1 kann
				 * nicht (siehe peer_segment in lapb.h). */
				axp->peer_segment = xi.segment ? 1 : -1;
				if(xi.srej)
					axp->flags.peer_srej = YES;
			}
			lp = xid_leg_find(hdr->source,iface);
			while(lp != NULL){
				struct ax25_xid_leg *next = lp->next;
				/* Was sie ueber sich selbst gesagt hat, gilt auch
				 * fuer das Bein, das wir ihr gerade eroeffnen -
				 * die Antwort ging an B, und die Sitzung mit ihr
				 * ist unsere. */
				lp->srej = xi.srej;
				/* C's own modulus, for the answer that
				 * goes to A in C's name - see
				 * xid_leg_answer(). */
				lp->mod128 = xi.mod128;
				/* And what C will take, to clamp the leg's
				 * own sending to (ax25_apply_peer_xid). */
				lp->n1rx = xi.n1rx;
				lp->window = xi.window;
				/* xid_leg_served() unlinks and frees it, so the
				 * successor has to be remembered first. */
				xid_leg_served(lp);
				lp = next;
			}
			/* The answer was addressed to US, so it was no station's
			 * link to begin with and find_ax25() found none: the
			 * block above exists only to carry this frame.  Left in
			 * the table it would be a session to C that we never
			 * opened - and ax_answers_to() would then take C for
			 * our own station on that port, which is precisely the
			 * confusion the relay legs are kept apart from.  A block
			 * that was already there is none of our business. */
			if(made_here)
				del_ax25(axp);
		}
		if(cmdrsp == LAPB_RESPONSE && axp != NULL &&
		   axp->flags.neg_wait){
			/* Our own probe is answered: this link may go on
			 * with the SABM it was waiting for - as a SABME if
			 * the answer said he speaks modulo-128.
			 *
			 * The probe changed nothing; the ANSWER is what the
			 * link is built from.  A port at "accept" may use it
			 * as well, and that is the point of the arrangement:
			 * accept does not start a negotiation, because a
			 * SABME against a peer who cannot follow is an
			 * unanswered frame and a wait, but once a XID has
			 * PROVEN the capability the wait is already paid
			 * for.  accept-or-XID, then: at "off" nothing is
			 * read here at all, and a port that wants neither
			 * modulo-128 nor a XID has the two knobs to say so
			 * ("eax25 off", or "ax25-v2.2 off" for no XID).
			 *
			 * What the answer carries besides the modulus
			 * (xi.n1rx, xi.window) is parsed and available, not
			 * applied: only the modulus is a parameter whose
			 * mismatch the peer can see on the wire, so only
			 * the modulus is acted upon.  Nothing is forced on
			 * him - an N1 or a window we would have to clamp to
			 * the smaller of the two is our own housekeeping
			 * (doc/AX25-2.2-GAP.md).
			 */
			/* DIE ANTWORT ENTSCHEIDET, NICHT NUR HEBT
			 * AB.  Sie sagt auch aus, dass der andere KEIN
			 * Modulo 128 spricht - und dann mit einem SABME
			 * weiterzugehen hiesse, eine Faehigkeit zu
			 * behaupten, die auf der Leitung gerade
			 * widerlegt wurde: C, das nicht antworten
			 * kann, antwortet einem SABME erst recht nicht,
			 * und der Aufrufer wartet N2 SabMEs lang auf
			 * etwas, das nicht kommen wird.  Das ist
			 * dasselbe Wissen, aus dem est_link_xid() nach
			 * der Wartezeit ein schlichtes SABM schickt
			 * (lapbtime.c) - nur eben nach dem Beweis statt
			 * nach dem Verstummen.
			 *
			 * NUR WAS DIE ANTWORT HERGIBT, wird
			 * uebernommen: der Modulus oben, und was er
			 * ueber sein eigenes Empfangen sagt (N1, k) als
			 * Obergrenze fuer unser Senden.
			 */
			if(axp->iface != NULL &&
			   axp->iface->eax25 != EAX25_OFF &&
			   axp->mmask != (xi.mod128 ? EMMASK : MMASK)){
				axp->mmask = xi.mod128 ? EMMASK : MMASK;
				eax25_mark(axp);
			}
			ax25_apply_peer_xid(axp, xi.n1rx, xi.window);
			axp->flags.neg_wait = NO;
			axp->retries = 0;
			est_link_sabm(axp);
		}
		if(cmdrsp == LAPB_COMMAND &&
		   (iface == NULL || iface->ax25_v2 != AX25V2_OFF)){
			/* dyadic: answered from the link we have, or from
			 * the throwaway build_path() set up above when the
			 * XID came from a station we know nothing about.
			 * With the F bit echoing his P, as for every
			 * unnumbered answer.
			 *
			 * The modulus advertised is NOT taken from the link
			 * block: that block may be a throwaway which only
			 * ever knew modulo-8, while the port is the truth we
			 * can afford to advert.  eax25 off means modulo-8,
			 * whatever the block believes.
			 */
			xid_our_info(iface,
				     (iface == NULL || iface->eax25 == EAX25_OFF)
					     ? 0 : 1, YES,
				     ax25_hop2hop(), &tbp);
			sendframe(axp,LAPB_RESPONSE,XID | (pf ? PF : 0),-1, &tbp);
		} else if(cmdrsp == LAPB_COMMAND){
			/* OFF SCHWEIGT, AUCH AUF FRAGEN.  Der Knopf
			 * ax25-v2.2 off heisst "diese Station kennt kein
			 * XID" - sonst waere er kein Schalter, sondern nur
			 * eine Aussage darueber, was WIR anfangen.  Ein
			 * prober, der hier fragt, bekommt keine Antwort und
			 * faellt nach seiner eigenen Frist auf SABM zurueck,
			 * so wie gegen eine echte 2.0-Station.  Vorher stand
			 * hier, off sei "eine Regel ueber unsere Fragen,
			 * nicht ueber die Antworten, die wir schulden" -
			 * und genau das war der gemeldete Fehler: ein Port
			 * auf off antwortete trotzdem (Thomas, 2026-10-09).
			 */
		}
		free_p(bpp);
		return 0;
	}

	/* This section follows the SDL diagrams by K3NA fairly closely */
	switch(axp->state){
	case LAPB_DISCONNECTED:
		switch(type){
		case SABM:      /* Initialize or reset link */
		case SABME:     /* the same, modulo-128 */
			if(!digipeat){
			sendctl(axp,LAPB_RESPONSE,UA|pf);       /* Always accept */
			clr_ex(axp);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);/* Resets state counters */
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			if(axp->services == NULL){
				struct ax_route *axr;
				axr = ax_routeptr(axp->hdr.dest,0);
				/* A callsign we listen for is handed over as
				 * soon as the link stands, not when the
				 * caller first types something - whatever is
				 * behind it may want to greet, and a mailbox
				 * always does.  hdr.source is the address
				 * that was called; the header is already
				 * turned round here.
				 */
				if(axr && axr->jumpstart)
					axserv_start(axp,PID_NO_L3);
				axserv_connected(axp);
#ifdef	AX25_VJCOMP
                            /* MW: Reset VJ structures */
                            if (axp->slcomp) {
                              slhc_free(axp->slcomp);
                              axp->slcomp = NULL;
                              printf("VJ reset on ax_bc %p\n", axp);
                            }
                            if (axr && axr->vjcomp) {
                                axp->slcomp_enable = 1;
                                printf("VJ init on ax_cb %p\n", axp);
                            }
#endif
			}
			} else {
				switch(axp->peer->state){
				case LAPB_DISCONNECTED:
					/* THE ONWARD LEG IS OPENED LIKE A CONNECT OF
					 * OUR OWN.  It used to send the SABM here by
					 * hand, which is the whole reason the second
					 * leg never asked anything: there was no
					 * est_link() here to ask.  Now the port's
					 * eax25 and ax25-v2.2 decide as they do for
					 * a connect that starts here - question
					 * first at initiate, and the XID goes out
					 * as B->C so the answer lands here
					 * (xid_leg_open).
					 *
					 * "always" kann das Bein auch
					 * VERWEIGERN, und est_link() raeumt
					 * dabei beide Bloecke ab - diese
					 * Haelfte ist der gerade laufende
					 * Rahmen.  Die Frage kommt deshalb
					 * VOR est_link() und wird hier
					 * beantwortet: der Anrufer bekommt
					 * eine Absage, und der Abbau unter
					 * diesem switch nimmt beide Bloecke
					 * mit.  Diese Haelfte bleibt
					 * DISCONNECTED, damit ein weiteres
					 * SABM des Anrufers wieder hier
					 * hereinkommt und das Bein erneut
					 * aufzubaut versucht.
					 */
					if(ax25_v2_refused(axp->peer->hdr.dest,
							   axp->peer->iface)){
						if(poll)
							sendctl(axp,LAPB_RESPONSE,DM|pf);
						break;
					}
					est_link(axp->peer);
					lapbstate(axp->peer,LAPB_SETUP);
					break;
				case LAPB_SETUP:
					if(axp->peer->routing_changes < 3){
						build_path(axp->peer,NULL,hdr,0,0);
						sendctl(axp->peer,LAPB_COMMAND,SABM|PF);
						start_timer(&axp->peer->t1);
					}
					break;
				default:
					if(poll)
						sendctl(axp,LAPB_RESPONSE,DM|pf);
					break;
				}
			}
			break;
		case DM:        /* Ignore to avoid infinite loops */
			break;
		default:        /* All others get DM */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		if(axp->state == LAPB_DISCONNECTED &&
		   (axp->peer == NULL || axp->peer->state == LAPB_DISCONNECTED)){
			if(axp->peer != NULL)
				del_ax25(axp->peer);
			del_ax25(axp);
			free_p(bpp);
			return 0;
		}
		break;
	case LAPB_LISTEN:
		break;
	case LAPB_SETUP:
		/* Any frame reaching this dispatch - a SABM of his, a DM, a
		 * UA - answers or overtakes an XID probe that may be on the
		 * wire, so the wait for its answer ends here.  The XID
		 * itself never reaches this switch: it is handled clear of
		 * the state dispatch, above.
		 */
		axp->flags.neg_wait = NO;
		switch(type){
		case SABM:      /* Simultaneous open */
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:        /* Connection accepted */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_YES);
			else if(axp->eax25_tried){
				/* He ignored the SABMEs and answered the
				 * SABM.  By itself that read like proof and
				 * is not: if a SABME, or HIS UA to it, was
				 * simply lost, the same picture arises - and
				 * then he is on modulo 128 while we are on
				 * 8.  Measured against TNN at 20 % loss: the
				 * very station whose other link ran modulo
				 * 128 got marked incapable.  So it is
				 * counted, not believed.
				 *
				 * A DM FIRST changes the picture: he SAW
				 * modulo-128 and refused it, so this UA is
				 * the second half of that one answer and may
				 * be believed.  That is the difference
				 * eax25_dmed carries; without it, count.
				 */
				if(axp->eax25_dmed)
					eax25_remember(axp->hdr.dest,
						       AXR_EAX25_NO);
				else
					eax25_failed(axp->hdr.dest);
				axp->eax25_tried = 0;
				axp->eax25_dmed = 0;
			}
			/* Note: xmit queue not cleared */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			axp->unack = axp->vr = axp->vs = 0;
			lapbstate(axp,LAPB_CONNECTED);
			if(axp->peer){
				sendctl(axp->peer,LAPB_RESPONSE,UA|PF);
				start_timer(&axp->peer->t3);
				start_timer(&axp->peer->t5);
				lapbstate(axp->peer,LAPB_CONNECTED);
			}
			break;
		case DM:        /* Connection refused */
			if(axp->mmask == EMMASK){
				/* Not (yet) a verdict.  A DM to a SABME
				 * can mean "I do not speak modulo-128" -
				 * or that there is no service at all (a
				 * crashed ax25d answers DM to anything).
				 * So ask again plainly and let the SABM
				 * settle it: a DM to THAT one means no
				 * service, a UA is proof he cannot do
				 * modulo-128, and only then is the route
				 * marked (see the UA case).  eax25_dmed
				 * keeps WHICH question the DM answered.
				 */
				axp->eax25_dmed = 1;
				eax25_fallback(axp);
				free_p(bpp);
				return 0;
			}
			if(axp->peer)
				/* Propagate the refusal.  No F: whether the
				 * incoming leg has an outstanding poll to be
				 * answered is not ours to know here.
				 */
				sendctl(axp->peer,LAPB_RESPONSE,DM);
			free_q(&axp->txq);
			stop_timer(&axp->t1);
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		default:        /* All other frames ignored */
			break;
		}
		break;
	case LAPB_DISCPENDING:
		switch(type){
		case SABM:
		case SABME:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case DISC:
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		case UA:
		case DM:
			stop_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		default:        /* Respond with DM only to command polls */
			if(poll)
				sendctl(axp,LAPB_RESPONSE,DM|pf);
			break;
		}
		break;
	case LAPB_CONNECTED:
		switch(type){
		case SABM:
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			/* free_q(&axp->txq); */
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			for(tmp = 0; tmp <= EMMASK; tmp++){
				free_p(&axp->reseq[tmp].bp);
			}
			axp->unack = axp->vr = axp->vs = 0;
#ifdef	AX25_VJCOMP
                       /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                              slhc_free(axp->slcomp);
                              axp->slcomp = NULL;
                              printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish */
			break;
		case FRMR:
			/* He accepted the SABME and then could not follow.
			 * Remember "cannot" rather than clearing what we knew:
			 * unknown would mean probing him again on the very next
			 * connect, which is the cost we became three-valued to
			 * avoid.  An incoming SABME from him undoes it.
			 * FRMR also closes, at once, any XID probe of ours
			 * that may be on the wire, and - since this is the
			 * LAPB that accepted a SABME - it says that the probe
			 * is not wanted here: remember no-XID on the route
			 * exactly as the probe timing out does
			 * (doc/AX25-2.2-GAP.md, F1-5).
			 */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_NO);
			if(axp->iface != NULL &&
			   (axp->iface->ax25_v2 == AX25V2_ALWAYS ||
			    axp->iface->ax25_v2 == AX25V2_INITIATE)){
				struct ax_route *rp = ax_routeptr(axp->hdr.dest,0);
				if(rp != NULL){
					rp->xid = AXR_XID_NO;
					/* FRMR ist der Peer selbst: er sagt uns,
					 * XID sei hier nicht willkommen.  Das ist
					 * eine endgueltige Auskunft, keine
					 * Vermutung wie Probe-Timeout - also
					 * wird nach ihr NIE wieder gefragt,
					 * nicht einmal nach Xid_probe_time
					 * (doc/AX25-2.2-GAP.md).
					 */
					rp->xid_frmr = YES;
					/* Und seine Parameter sind damit
					 * auch hin: er will kein XID, also
					 * gilt auch nichts, was er in einem
					 * frueheren gesagt hat. */
					rp->peer_n1rx = 0;
					rp->peer_window = 0;
					rp->peer_srej = 0;
					rp->peer_segment = 0;
					rp->peer_xid_time = 0;
				}
			}
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish link */
			break;
		case RR:
		case RNR:
			if(type == RNR){
				if(!axp->flags.remotebusy)
					axp->flags.remotebusy = msclock();
				start_timer(&axp->t4);
			} else {
				axp->flags.remotebusy = NO;
				stop_timer(&axp->t4);
			}
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
			ackours(axp,nr,0);
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
			ackours(axp,nr,1);
			/* stop_timer(&axp->t1); */
			/* start_timer(&axp->t3); */
			/* This may or may not actually invoke transmission,
			 * depending on whether this REJ was caused by
			 * our losing his prior ACK.
			 */
			/* inv_rex(axp); */
			break;
		case SREJ:
			/* He sent us a SREJ: the XID PI=3 bit is the formal
			 * proof a peer handles selective retransmission, a
			 * received SREJ is the factual one.  No station sends
			 * a SREJ without implementing its answer side - the
			 * specification defines both directions in one breath -
			 * so his next gap gets a SREJ in return, not a REJ.
			 */
			axp->flags.peer_srej = YES;
			/* EIN UNBESTAETIGTER XID.  Er hat den Rahmen geschickt,
			 * und SREJ gibt es erst mit 2.2 - das zeigt die
			 * Faehigkeit so, wie ein SABME das eax25 zeigt.  Aber
			 * er sagt damit nichts ueber sein Empfangen: kein N1,
			 * kein k, kein Segmenter.  Das ist genau der
			 * unbestaetigte Teil, und er ist nur so viel wert wie
			 * die Sitzung, in der er kam.  Ohne proto == V2_2
			 * wuerde srej_ok() ablehnen, und die Station, die sich
			 * gerade bewiesen hat, bekaeme ein REJ statt eines
			 * SREJ. */
			axp->proto = V2_2;
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
			ackours(axp,nr,0);
			/* A SREJ did not exist before 2.2: the path is
			 * therefore PROVEN 2.2-capable, without any XID -
			 * and that is exactly what it is worth: the
			 * capability, not the answer. */
			{
				struct ax_route *srp = ax_routeptr(hdr->source,0);
				/* EIN SREJ BEWEIST AX.25-2.2 - UND SONST
				 * NICHTS.  Oben steht schon alles, was
				 * daraus folgt: peer_srej und proto == V2_2
				 * fuer DIESE Sitzung, beides auf dem Draht
				 * bewiesen.  Er sagt damit NICHT, dass er
				 * Modulo 128 kann; 2.2 gibt es auch mit
				 * acht Sequenznummern, und SREJ ist genau
				 * dafuer der Reparaturweg.  Frueher stand
				 * hier ein eax25_remember(YES): eine
				 * Verwechslung, die die Station beim
				 * naechsten Anruf ein SABME kostete, das sie
				 * nie beantwortet haette.
				 *
				 * UND DER GELERNTE VERMERK BLEIBT, WIE ER
				 * WAR.  Sagte sein XID "kein SREJ" (oder hat
				 * er nie geantwortet), nehmen wir sein SREJ
				 * trotzdem an - lazy im Akzeptieren, und
				 * vielleicht mag er XID nicht und zeigt sich
				 * eben so.  Fuer ihn gutgeschrieben ist es
				 * nur hier: die Route behaelt ihr Urteil,
				 * und die naechste Sitzung faengt wieder bei
				 * dem an, was er dort selbst sagt.  Ein SREJ
				 * als "er hat unsere XID beantwortet" zu
				 * verbuchen, waere die andere Verwechslung:
				 * xid == YES faellt aus xid_may_probe()
				 * heraus, und seine Parameter (N1, k, das
				 * SREJ-Bit, der Segmenter - sie stehen allein
				 * in SEINER Antwort) kaemen nie mehr an.
				 * xid bleibt darum, was es war; der naechste
				 * Aufbau versucht die Frage weiter (xid_tries
				 * faengt neu an).  Auf einem Relais fragt
				 * ohnehin Punkt D. */
				if(srp != NULL && srp->xid != AXR_XID_YES)
					srp->xid_tries = 0;
			}
			/* A SREJ asks for exactly ONE I-frame back - the one
			 * numbered N(R) - not for the run a REJ takes (3.6.4).
			 * ackours() has just removed everything before N(R)
			 * and srej_answer() sends that one frame without
			 * touching the window: it stays in txq until his
			 * ordinary ack frees it, and the frames behind it
			 * are not retransmitted.  Over-running a SREJ would
			 * be harmless - the receiver buffers in order and
			 * discards the duplicates - but the specification
			 * draws the line at the one frame.
			 */
			srej_answer(axp,nr);
			break;
		case I:
			start_timer(&axp->t5);
			ackours(axp,nr,0); /** == -1) */
			resequence(axp,bpp,ns,pf,poll);
			break;
		default:        /* All others ignored */
			break;
		}
		break;
	case LAPB_RECOVERY:
		switch(type){
		case SABM:
		case SABME:
			sendctl(axp,LAPB_RESPONSE,UA|pf);
			clr_ex(axp);
			stop_timer(&axp->t1);
			start_timer(&axp->t3);
			start_timer(&axp->t5);
			for(tmp = 0; tmp <= EMMASK; tmp++){
				free_p(&axp->reseq[tmp].bp);
			}
			axp->unack = axp->vr = axp->vs = 0;
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			lapbstate(axp,LAPB_CONNECTED); /* Purge queues */
			break;
		case DISC:
			free_q(&axp->txq);
			sendctl(axp,LAPB_RESPONSE,DM|pf);
			stop_timer(&axp->t1);
			stop_timer(&axp->t3);
			/* axp->response = DM; */
			axp->reason = LB_NORMAL;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case DM:
			axp->reason = LB_DM;
			lapbstate(axp,LAPB_DISCONNECTED);
			free_p(bpp);
			return 0;
		case UA:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
#ifdef	AX25_VJCOMP
                        /* MW: reset VJ structures */
                        if (axp->slcomp) {
                                slhc_free(axp->slcomp);
                                axp->slcomp = NULL;
                                printf("VJ reset on ax_cb %p\n", axp);
                        }
#endif
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish */
			break;
		case FRMR:
			/* He accepted the SABME and then could not follow.
			 * Remember "cannot" rather than clearing what we knew:
			 * unknown would mean probing him again on the very next
			 * connect, which is the cost we became three-valued to
			 * avoid.  An incoming SABME from him undoes it.
			 * FRMR also closes, at once, any XID probe of ours
			 * that may be on the wire, and - since this is the
			 * LAPB that accepted a SABME - it says that the probe
			 * is not wanted here: remember no-XID on the route
			 * exactly as the probe timing out does
			 * (doc/AX25-2.2-GAP.md, F1-5).
			 */
			if(axp->mmask == EMMASK)
				eax25_remember(axp->hdr.dest,AXR_EAX25_NO);
			if(axp->iface != NULL &&
			   (axp->iface->ax25_v2 == AX25V2_ALWAYS ||
			    axp->iface->ax25_v2 == AX25V2_INITIATE)){
				struct ax_route *rp = ax_routeptr(axp->hdr.dest,0);
				if(rp != NULL){
					rp->xid = AXR_XID_NO;
					/* FRMR ist der Peer selbst: er sagt uns,
					 * XID sei hier nicht willkommen.  Das ist
					 * eine endgueltige Auskunft, keine
					 * Vermutung wie Probe-Timeout - also
					 * wird nach ihr NIE wieder gefragt,
					 * nicht einmal nach Xid_probe_time
					 * (doc/AX25-2.2-GAP.md).
					 */
					rp->xid_frmr = YES;
					/* Und seine Parameter sind damit
					 * auch hin: er will kein XID, also
					 * gilt auch nichts, was er in einem
					 * frueheren gesagt hat. */
					rp->peer_n1rx = 0;
					rp->peer_window = 0;
					rp->peer_srej = 0;
					rp->peer_segment = 0;
					rp->peer_xid_time = 0;
				}
			}
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
			/* est_link(axp); */
			/* lapbstate(axp,LAPB_SETUP);      Re-establish link */
			break;
		case RR:
		case RNR:
			if(type == RNR){
				if(!axp->flags.remotebusy)
					axp->flags.remotebusy = msclock();
				start_timer(&axp->t4);
			} else {
				axp->flags.remotebusy = NO;
				stop_timer(&axp->t4);
			}
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr,1);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll){
					if(dama_master_holds(axp))
						dama_master_owe(axp);
					else
						enq_resp(axp);
				}
				ackours(axp,nr,0);
				/* Keep timer running even if all frames
				 * were acked, since we must see a Final
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case REJ:
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			/* Don't insist on a Final response from the old proto */
			if(axp->proto == V1 || final){
				stop_timer(&axp->t1);
				ackours(axp,nr,1);
				if(axp->unack != 0){
					inv_rex(axp);
				} else {
					start_timer(&axp->t3);
					lapbstate(axp,LAPB_CONNECTED);
				}
			} else {
				if(poll){
					if(dama_master_holds(axp))
						dama_master_owe(axp);
					else
						enq_resp(axp);
				}
				ackours(axp,nr,1);
				if(axp->unack != 0){
					/* This is certain to trigger output */
					inv_rex(axp);
				}
				/* A REJ that acks everything but doesn't
				 * have the F bit set can cause a deadlock.
				 * So make sure the timer is running.
				 */
				if(!run_timer(&axp->t1))
					start_timer(&axp->t1);
			}
			break;
		case SREJ:
			/* He sent us a SREJ: the XID PI=3 bit is the formal
			 * proof a peer handles selective retransmission, a
			 * received SREJ is the factual one.  No station sends
			 * a SREJ without implementing its answer side - the
			 * specification defines both directions in one breath -
			 * so his next gap gets a SREJ in return, not a REJ.
			 */
			axp->flags.peer_srej = YES;
			/* EIN UNBESTAETIGTER XID.  Er hat den Rahmen geschickt,
			 * und SREJ gibt es erst mit 2.2 - das zeigt die
			 * Faehigkeit so, wie ein SABME das eax25 zeigt.  Aber
			 * er sagt damit nichts ueber sein Empfangen: kein N1,
			 * kein k, kein Segmenter.  Das ist genau der
			 * unbestaetigte Teil, und er ist nur so viel wert wie
			 * die Sitzung, in der er kam.  Ohne proto == V2_2
			 * wuerde srej_ok() ablehnen, und die Station, die sich
			 * gerade bewiesen hat, bekaeme ein REJ statt eines
			 * SREJ. */
			axp->proto = V2_2;
			axp->flags.remotebusy = NO;
			stop_timer(&axp->t4);
			if(poll){
				if(dama_master_holds(axp))
					dama_master_owe(axp);
				else
					enq_resp(axp);
			}
			ackours(axp,nr,0);
			/* The same single retransmission as in CONNECTED. */
			srej_answer(axp,nr);
			/* Still need our Final - T1 must keep running. */
			if(!run_timer(&axp->t1))
				start_timer(&axp->t1);
			break;
		case I:
			start_timer(&axp->t5);
			ackours(axp,nr,0); /** == -1) */
			/* Make sure timer is running, since an I frame
			 * cannot satisfy a poll
			 */
			if(!run_timer(&axp->t1))
				start_timer(&axp->t1);
			resequence(axp,bpp,ns,pf,poll);
			break;
		default:
			break;          /* Ignored */
		}
		break;
	}
	free_p(bpp);    /* In case anything's left */

	/* See if we can send some data, perhaps piggybacking an ack.
	 * If successful, lapb_output will clear axp->response.
	 */
	lapb_output(axp);
	/* if(axp->response != 0){ */
	/*      sendctl(axp,LAPB_RESPONSE,axp->response); */
	/*      axp->response = 0; */
	/* } */
	if((axp->state == LAPB_RECOVERY || axp->state == LAPB_CONNECTED) &&
	   ((axp->flags.closed && !axp->txq) ||
	    (axp->flags.remotebusy && TDIFF(msclock(), axp->flags.remotebusy) > 900000L))
	   ){
		/* A DAMA slave waits for the poll before it disconnects too -
		 * the paper is explicit about that one ("the user ... will
		 * wait to send his DISC-frame until polled").  Nothing is lost
		 * by waiting: the wish to close is the state, and the state
		 * outlives the gate being shut.
		 */
		if(dama_holds(axp)){
			dama_wait(axp);
		} else {
			sendctl(axp,LAPB_COMMAND,DISC|PF);
			start_timer(&axp->t1);
			lapbstate(axp,LAPB_DISCPENDING);
		}
	}
	dama_serve_others(iface,axp);
	dama_poll_end(iface);
	return 0;
}
/* Handle incoming acknowledgements for frames we've sent.
 * Free frames being acknowledged.
 * Return -1 to cause a frame reject if number is bad, 0 otherwise
 */
static int
ackours(
struct ax25_cb *axp,
uint n,
int rex_all
){
	struct mbuf *bp;
	int acked = 0;  /* Count of frames acked by this ACK */
	uint oldest;    /* Seq number of oldest unacked I-frame */
	int32 rtt,abserr;
	int32 tmp;

	/* Free up acknowledged frames by purging frames from the I-frame
	 * transmit queue. Start at the remote end's last reported V(r)
	 * and keep going until we reach the new sequence number.
	 * If we try to free a null pointer,
	 * then we have a frame reject condition.
	 */
	oldest = (axp->vs - axp->unack) & axp->mmask;
	while(axp->unack != 0 && oldest != n){
		if((bp = dequeue(&axp->txq)) == NULL){
			/* Acking unsent frame */
			return -1;
		}
		free_p(&bp);
		axp->unack--;
		acked++;
		if(axp->flags.rtt_run && axp->rtt_seq == oldest){
			/* A frame being timed has been acked */
			axp->flags.rtt_run = 0;
			/* Update only if frame wasn't retransmitted */
			if(!axp->flags.retrans){
				rtt = TDIFF(msclock(), axp->rtt_time);
				abserr = (rtt > axp->srt) ? rtt - axp->srt :
				 axp->srt - rtt;

				/* Run SRT and mdev integrators */
				axp->srt = ((axp->srt * 7) + rtt + 4) >> 3;
				axp->mdev = ((axp->mdev*3) + abserr + 2) >> 2;
				/* Update timeout */
				tmp = 4*axp->mdev+axp->srt;
				set_timer(&axp->t1,max(tmp,500));
				if(axp->maxframe < ax25_maxframe(axp))
					axp->maxframe++;
			}
			axp->flags.retrans = 0;
		}
		axp->retries = 0;
		oldest = (oldest + 1) & axp->mmask;
	}
	if(axp->unack == 0){
		/* All frames acked, stop timeout */
		stop_timer(&axp->t1);
		start_timer(&axp->t3);
	} else if(acked != 0) {
		/* Partial ACK; restart timer */
		start_timer(&axp->t1);
	}
	if(rex_all){
		axp->flags.retrans = 1;
		axp->vs -= axp->unack;
		axp->vs &= axp->mmask;
		axp->unack = 0;
	}
	if(acked != 0){
		/* If user has set a transmit upcall, indicate how many frames
		 * may be queued
		 */
		tmp = (axp->maxframe - len_q(axp->txq)) * axp->paclen;
		if(tmp > 0){
			/* Room again.  Every consumer is told; the send queue
			 * is shared and whoever has something puts it in,
			 * first come first served.
			 */
			struct axservice *sp;
			struct axservice *spnext;

			for(sp = axp->services; sp != NULL; sp = spnext){
				spnext = sp->next;
				if(sp->t_upcall != NULL)
					(*sp->t_upcall)(sp,(int)tmp);
			}
		}
		if(axp->peer && axp->peer->flags.rnrsent && !busy(axp->peer))
			sendctl(axp->peer,LAPB_RESPONSE,RR);
	}
	return 0;
}

/* Remember what modulo-128 actually did with this station.  Our own traffic
 * is the only thing that teaches this - what a station claims in the EAX bit
 * of its SSID says it is willing, not that it works.
 */

void eax25_remember(uint8 *call, int verdict)
{
	struct ax_route *rp;

	if((rp = ax_routeptr(call,1)) == NULL)
		return;
	rp->eax25 = verdict;
	/* Ein Erfolg loescht die Fehlschlaege - sie waren dann Zufall, und der
	 * Zaehler soll nur zaehlen, was IN FOLGE misslingt.
	 */
	if(verdict == AXR_EAX25_YES)
		rp->eax25_fails = 0;
	if(verdict == AXR_EAX25_NO)
		rp->eax25_skips = 0;
}

/* Ein Rueckfall auf SABM, und der sagt fuer sich genommen NICHTS: er kann
 * heissen, dass die Station kein Modulo 128 kann - oder dass ein SABME
 * verlorenging, oder ihr UA darauf.  Deshalb wird er gezaehlt und nicht
 * geglaubt; erst EAX25_MAXFAILS Rueckfaelle in Folge sind eine Aussage.
 *
 * Ein FRMR geht weiter direkt an eax25_remember(): dort HAT sie das SABME
 * angenommen und konnte dann nicht folgen, das ist der eine Beweis, den
 * kein verlorener Rahmen vortaeuschen kann.
 */

void eax25_failed(uint8 *call)
{
	struct ax_route *rp;

	if((rp = ax_routeptr(call,1)) == NULL)
		return;
	if(rp->eax25 == AXR_EAX25_NO)
		return;
	if(++rp->eax25_fails >= EAX25_MAXFAILS){
		rp->eax25 = AXR_EAX25_NO;
		rp->eax25_skips = 0;
	}
}

/* The first modulo-128 call heard on a port that does not ask for it - once
 * per port and per run, whoever it came from.  The default is not to ask
 * anyone (see doc/EAX25.md), and an operator who never reads the
 * documentation would otherwise not learn that this port has partners who
 * could do better.  Reported on "off" as well: he said no once, and what he
 * hears now may be a partner who has since been replaced.
 *
 * The packet header goes with it, so it says WHO was heard and over which
 * path, instead of only that somebody was.
 *
 * Console AND log, because neither reaches everyone on its own: logmsg()
 * returns at once when no "log" file is configured, and the console is
 * nothing on a node started without one.  What survives in either case is
 * the E in "ax25 route list".
 */

static void eax25_hint(struct iface *ifp,struct ax25 *hdr)
{
	char msg[256];

	if(ifp == NULL || ifp->eax25_hinted)
		return;
	if(ifp->eax25 != EAX25_ACCEPT && ifp->eax25 != EAX25_OFF)
		return;
	ifp->eax25_hinted = 1;
	sprintf(msg,"EAX25 heard on %s. Consider \"ifconfig %s eax25 initiate\"."
	 "  Packet: %s",ifp->name,ifp->name,ax25hdr_to_string(hdr));
	printf("%s\n",msg);
	logmsg(NULL,"%s",msg);
}

/* Which modulus a link we are about to open should ask for.
 *
 * The knob alone decides, and nothing follows the caller any more.  There was
 * a "from" here - the other half of a relayed connection - and the rule it
 * carried was: a caller who asked for plain AX.25 is carried onward as plain
 * AX.25, because if that leg then misbehaved he was the one who could do
 * nothing about it.  It cost a relay port its own capability: at "initiate"
 * such a leg went out as SABM, and the XID question that would have told us
 * better was never asked (Thomas, 2026-09-25).  "from" is gone with it.
 *
 * What the port may not decide is what the peer does, and that is the route
 * rather than the knob: a learned "he speaks modulo-128" is used, a learned
 * "he does not" is counted.  Both verdicts come out of our own traffic with
 * him - his SABME, or his UA to ours - and never out of an answer relayed on
 * somebody else's behalf, which is why axroute_add() drops them whenever the
 * path or the port moves.
 *
 * DIESELBE FRAGE, DIESELBE ANTWORT.  Der Relais-Bein (xid_leg_open) und die
 * Selbst-Sonde in ax_recv() sind eigene XID-Fragen an das naechste Bein bzw.
 * an C, und sie nennen die Faehigkeit des AUSGANGSPORTS, so wie es unsere
 * eigene Verbindung auch tut.  Ein "accept"-Port kann Modulo 128; er
 * kuendigt es an, sonst erfuehre C nie, dass ein SABME zu ihm durchkaeme,
 * und nennt dazu die Fenstergroesse, die er auf einem Modulo-128-Bein
 * faehrt (emaxframe).  Bit und Fenster sind EINE Faehigkeit; solange das
 * Fenster den OPERATIVEN Modulus dieser einen Verbindung spiegelte, sagte
 * die Sonde "Modulo 128, Fenster 7" und widersprach sich selbst (Thomas,
 * 2026-10-13).  eax25_wanted() beantwortet die andere Frage - "soll ICH mit
 * diesem Ziel Modulo 128 anfangen?" - und wird darum nur noch in est_link()
 * gelesen, nicht mehr an den Sonde-Stellen.
 */

/* Alter the "cannot do modulo-128" verdict by ONE session.  Called at the
 * top of est_link(), i.e. once per attempt to set up a link - and not from
 * eax25_wanted(): the count belongs at the start of an attempt, and
 * est_link() is where an attempt begins.  When the count reaches
 * EAX25_RETRY the verdict goes and the next attempt asks with SABME again;
 * eax25_failed() says why the count exists at all.
 */
static void
eax25_age(uint8 *call)
{
	struct ax_route *rp = ax_routeptr(call,0);

	if(rp == NULL || rp->eax25 != AXR_EAX25_NO)
		return;
	if(++rp->eax25_skips < EAX25_RETRY)
		return;
	rp->eax25 = AXR_EAX25_UNKNOWN;
	rp->eax25_fails = 0;
	rp->eax25_skips = 0;
}

int eax25_wanted(struct iface *ifp, uint8 *dest)
{
	struct ax_route *rp;

	/* No port yet, or a port that never speaks it: do not ask.  Not even
	 * a learned "he can" overrules this - "off" is the operator saying he
	 * knows what is on the air here. */
	if(ifp == NULL || ifp->eax25 == EAX25_OFF)
		return MMASK;
	if((rp = ax_routeptr(dest,0)) != NULL){
		/* GELERNT, NICHT GERATEN.  His SABME, or his UA to our SABME,
		 * on this very path.  It is the E in "ax25 route list" and
		 * it is the same thing the XID answer says a moment later -
		 * which is why asking him again on the next connect is waste
		 * and why "accept" benefits from it as much as "initiate".
		 */
		if(rp->eax25 == AXR_EAX25_YES)
			return EMMASK;
		/* "Kann nicht" gilt nicht fuer immer: nach EAX25_RETRY
		 * einfachen Verbindungen wird wieder gefragt, denn der
		 * Vermerk kann aus verlorenen Rahmen stammen (siehe
		 * eax25_failed).  Gezaehlt wird das EINMAL je Aufbau, in
		 * eax25_age() am Anfang von est_link(); hier wird nur
		 * gelesen.
		 */
		if(rp->eax25 == AXR_EAX25_NO)
			return MMASK;
	}
	/* A port that only answers sends no SABME of its own - unless it has
	 * proof that the far end can take one. */
	if(ifp->eax25 == EAX25_ACCEPT)
		return MMASK;
	/* "initiate" and "always" both ask for it.  They no longer differ
	 * here; the "always" of the AX.25 2.2 knob is the one that is going
	 * away, and the "always" of this knob follows it into the
	 * "ax25 version" axis (TODO.txt). */
	return EMMASK;
}

/* Darf C (wieder) gefragt werden?  Je Aufbau hoechstens eine Frage, und die
 * Regel steht an EINER Stelle, weil drei sie lesen: est_link() fuer die
 * eigene Verbindung, und ax25.c fuer die Ratenbegrenzung der Relais-Sonde.
 *
 * Ein gemessenes JA und ein FRMR sind Auskuenfte und werden nicht
 * hinterfragt.  Ein SCHWEIGEN ist nur unsere Vermutung, und auf HF geht
 * eine Frage verloren: die ersten XID_MAXTRIES Aufbauten fragen daher von
 * selbst wieder, je einer, und erst danach greift Xid_probe_time.
 */
int
xid_may_probe(struct ax_route *rp)
{
	if(rp == NULL)
		return YES;             /* nie gefragt */
	if(rp->xid_frmr)
		return NO;              /* er hat ausdruecklich abgelehnt */
	if(rp->xid == AXR_XID_YES)
		return NO;              /* er hat geantwortet, nichts zu fragen */
	/* AXR_XID_UNKNOWN faellt bewusst hierher und nicht mehr in ein
	 * pauschales JA: der Zaehler wird beim Absenden der Frage erhoeht
	 * (est_link_xid), und ein Aufbau, der vor T1 abgebrochen wurde,
	 * laesst xid auf UNKNOWN - gezaehlt ist seine Frage trotzdem.  Ohne
	 * das galt so ein Aufbau als "nie gefragt" und wurde endlos
	 * wiederholt (Thomas, 2026-10-13). */
	if(rp->xid_tries < Xid_retries)
		return YES;
	return secclock() - rp->xid_time >= Xid_probe_time;
}

/* Say on the wire which modulus this link runs on.  The bit lives in the
 * source SSID, active low, and htonax25() puts it there; every monitor reads
 * it - ours in ax25dump.c decodes the two-octet control field only when it is
 * set - and TNN's "EAXMODE 1, by MHEARD" is the same bit seen from outside.
 *
 * It used to be set in one place only, when we ACCEPTED a SABME.  A link we
 * opened ourselves therefore ran modulo-128 while its frames said "cannot",
 * so our own trace could not decode our own traffic and no neighbour could
 * learn from us that we speak it.  mmask is the truth; this follows it.
 */

static void eax25_mark(struct ax25_cb *axp)
{
	if(axp->mmask == EMMASK)
		axp->hdr.ext |= SSID_EAX25;
	else
		axp->hdr.ext &= ~SSID_EAX25;
}

/* Give up on modulo-128 for this attempt and ask again the plain way.  The
 * link stays in setup: to the caller above us nothing has happened yet, and
 * that is the point - he asked for a connection, not for a modulus.
 */

void eax25_fallback(struct ax25_cb *axp)
{
	axp->mmask = MMASK;
	eax25_mark(axp);
	axp->eax25_probes = 0;
	axp->eax25_tried = 1;

	/* Start the retransmission timer over.  What the probes proved is that
	 * he does not speak modulo-128; they said nothing about how long the
	 * path is, so carrying their stretched T1 into the plain attempt would
	 * punish the connection for the wrong reason.
	 *
	 * The retry COUNT is deliberately not reset.  Left alone, the probes
	 * are paid out of the tries the caller was already willing to spend:
	 * three SABMEs and the seven SABMs that remain come to about 94 s,
	 * where ten SABMs alone take 166 s.  Falling back therefore costs him
	 * nothing - it ends sooner than not trying at all.
	 */
	set_timer(&axp->t1,4 * axp->mdev);
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	start_timer(&axp->t1);
}

/* The SABM proper - used right away when the port has no need of an XID
 * probe, and when the probe's answer has come back.  Which of the two
 * reaches the wire is not decided here: the SABM becomes a SABME in
 * sendctl() when the link is running modulo-128, and the answer is what
 * may have put it there (see the XID answer in lapb_input()).
 */
static void
est_link_sabm(struct ax25_cb *axp)
{
	sendctl(axp,LAPB_COMMAND,SABM|PF);
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
}

/* Is somebody already asking C, on this very port?  Then a second waiter
 * joins that question instead of putting its own on the air: N stations that
 * want the unknown C at the same moment cost ONE probe, and all of them are
 * told the answer.  The port is part of the question, because C may be
 * reachable by two ways and an answer that arrives on one of them says
 * nothing about the other.
 */
int
xid_leg_pending(uint8 *call, const struct iface *ifp)
{
	struct ax25_xid_leg *lp;

	for(lp = Ax25_xid_legs; lp != NULL; lp = lp->next)
		if(lp->leg_iface == ifp && addreq(lp->probe_to,call))
			return 1;
	return 0;
}

/* THE SECOND LEG ASKS FOR ITSELF.  A relayed call arrives as a SABM from A
 * that we have to follow with one of our own towards C, and the port that
 * carries it has an opinion about modulo-128 just as any other port has.  It
 * used to send its SABM straight away and let the fallback find out; the
 * question was not asked, so a C that could have said so in one exchange was
 * found out by trial and timeout instead.
 *
 * The question goes out IN A'S NAME, exactly as the leg's own SABM will go:
 * source A, the path from the incoming frame with our own digi position
 * marked repeated, so that C sees the same caller the link is for and the
 * answer returns along the same way A's own frames took.  The probe is the
 * XID the A-to-C leg would itself have sent; only the control field differs
 * from the SABM that follows.  C's answer is swallowed here (xid_leg_find)
 * and turned into A's answer, and the leg continues with its SABM once C has
 * spoken.  The alternative - asking C as B->C - is the hop2hop-ack variant
 * kept on the TODO list, not what this port does by default.
 *
 * Returns 0 if the job could not be created, and then the caller must not
 * leave the leg half-open: it either falls back to asking with the SABM or
 * gives the attempt up, and it is the caller's decision because it is the
 * one that knows about "always".
 */
int
xid_leg_open(struct ax25_cb *axp)
{
	struct ax25_xid_leg *lp, **pp;
	struct ax_route *rp;
	struct ax25_cb *paxp;
	struct mbuf *pbp = NULL;
	int asking;            /* is this question already on the air? */

	if(axp->iface == NULL)
		return 0;
	/* FUER DIESES BEIN SCHON EINER?  Dann genuegt der vorhandene
	 * Auftrag: beim Eintreffen wuerde ein zweiter dieselbe Eroeffnung
	 * ein zweites Mal machen, also zwei SABMs an C.  Kommt vor, weil
	 * der Anrufer sein SABM wiederholt, waehrend wir noch warten.
	 */
	for(lp = Ax25_xid_legs; lp != NULL; lp = lp->next)
		if(lp->link_to == axp)
			return 1;
	/* SCHON GEFRAGT?  Dann traegt sich dieser Warter in der
	 * vorhandenen Frage ein und wird mit bedient, statt eine eigene
	 * aufzumachen.  Drei Stationen, die im selben Moment den
	 * unbekannten C wollen, kosten so EINE Sonde und alle drei erfahren
	 * dasselbe.  Merken vor dem Einhaengen, sonst findet man sich selbst.
	 */
	asking = xid_leg_pending(axp->hdr.dest,axp->iface);
	lp = (struct ax25_xid_leg *) callocw(1, sizeof(*lp));
	if(lp == NULL)
		return 0;
	addrcp(lp->probe_to, axp->hdr.dest);
	lp->leg_iface = axp->iface;
	lp->link_to = axp;
	lp->timer.func = xid_leg_timeout;
	lp->timer.arg = lp;
	/* Wie beim Punkt-D-Auftrag: eine Dauer muss gesetzt sein, sonst
	 * startet der Timer gar nicht (timer.c) und das Bein bliebe fuer
	 * immer auf sein Bein warten. */
	set_timer(&lp->timer, XID_LEG_TIMEOUT);

	/* THE JOB GOES ON THE LIST BEFORE THE QUESTION GOES ON THE AIR.
	 * Between the two, C could answer - and an answer that finds no
	 * job would be dropped, leaving the leg waiting out its ten
	 * seconds for a station that already spoke.
	 */
	for(pp = &Ax25_xid_legs; *pp != NULL; pp = &(*pp)->next) ;
	*pp = lp;
	start_timer(&lp->timer);
	/* Nur wenn die Frage nicht schon draussen ist, geht hier eine eigene
	 * hinaus - und nur dann darf dieser Auftrag aus dem Ablauf ein
	 * Urteil ueber C machen (xid_leg_timeout). */
	lp->probed = asking ? 0 : 1;
	if(asking)
		/* Nur der Eintrag: die Frage ist draussen und ihre Antwort
		 * bedient diesen Warter mit, denn xid_leg_find() nimmt alle
		 * zu ihr gehoerenden Jobs. */
		return 1;

	/* What we are about to learn belongs to C, and the entry is the
	 * one we would learn it into anyway.  Marking the time now is what
	 * the rate limit reads even if C never answers (ax25.c). */
	rp = ax_routeptr(lp->probe_to,1);
	if(rp != NULL)
		rp->xid_time = secclock();

	paxp = cr_ax25(lp->probe_to);
	if(paxp == NULL){
		/* Nothing to send it with.  Not fatal: drop the job again and
		 * let the caller do it the plain way. */
		*pp = NULL;
		stop_timer(&lp->timer);
		free(lp);
		return 0;
	}
	/* IN A'S NAMEN, WIE DAS SABM-BEIN SELBST.
	 *
	 * Die Sonde ist die Frage, die A an C gerichtet hat: Quellruf A,
	 * Ziel C, und der Weg, den A's eigener Rahmen genommen hat, mit
	 * unserem eigenen Digi-Platz als wiederholt markiert (der Rahmen ist
	 * ja wirklich durch uns gegangen).  Genau diesen Kopf traegt auch das
	 * SABM-Bein, das auf C's Antwort folgt - axp ist der Peer-Block des
	 * Digipeat-Paares, sein hdr ist A->C ueber uns.  C sieht damit
	 * denselben Anrufer wie beim Verbindungsaufbau und schickt seine
	 * Antwort zurueck ueber denselben Weg; wir schlucken sie
	 * (xid_leg_find) und machen A's Antwort daraus.
	 *
	 * Die eigene B->C-Sonde (Quellruf B) ist die im TODO vermerkte
	 * hop2hop-ack-Variante und NICHT das Standardverhalten.
	 */
	/* Only initiate a probe if this port is set to do so.  'accept'
	 * means answer only - it must not send the first XID. */
	if(lp->leg_iface == NULL ||
	   (lp->leg_iface->ax25_v2 != AX25V2_INITIATE &&
	    lp->leg_iface->ax25_v2 != AX25V2_ALWAYS)){
		stop_timer(&lp->timer);
		for(pp = &Ax25_xid_legs; *pp != NULL; pp = &(*pp)->next){
			if(*pp == lp){
				*pp = lp->next;
				break;
			}
		}
		free(lp->anshdr);
		free(lp);
		return 0;
	}
	paxp->hdr = axp->hdr;
	paxp->iface = lp->leg_iface;
	xid_our_info(lp->leg_iface,
		     (lp->leg_iface->eax25 != EAX25_OFF) ? 1 : 0,
		     YES, ax25_hop2hop(), &pbp);
	/* DASS gefragt wurde, zaehlt hier - wie beim eigenen Aufbau
	 * (est_link_xid).  xid_leg_timeout() schreibt spaeter nur das
	 * Urteil; wartete der Zaehler auf dessen Ablauf, fragte ein
	 * Anrufer, der schneller als XID_LEG_TIMEOUT erneut anruft,
	 * dieselbe stumme Station wieder und wieder. */
	if(rp != NULL)
		rp->xid_tries++;
	sendframe(paxp, LAPB_COMMAND, XID | PF, -1, &pbp);
	del_ax25(paxp);
	return 1;
}

/* One XID command and the wait for its answer.  T1 bounds the wait just
 * like for a SABM: a peer who silently drops the XID costs us exactly one
 * time-out, and that is also where we learn that he does not speak it.
 */
static void
est_link_xid(struct ax25_cb *axp)
{
	struct mbuf *tbp = NULL;

	/* A RELAY LEG IS A SESSION, AND IT ASKS LIKE ONE.  Not because the
	 * caller's choice is any of our business - it stopped being that in
	 * eax25_wanted() - but because the leg beyond us is ours to run,
	 * and running it is a connect of its own.
	 */
	if(axp->peer != NULL){
		if(axp->iface == NULL || axp->iface->ax25_v2 == AX25V2_OFF)
			goto plain_sabm;
		if(xid_leg_open(axp))
			return;
		/* No job, so no question - and in particular NOT the question
		 * below: that one leaves in the header's source, which on this
		 * leg is A, and C would answer A.  So the plain SABM it is, auch
		 * bei "always": xid_leg_open() scheitert am Speicher, und einen
		 * Ruf abzuweisen, weil wir 40 Byte nicht bekommen haben, waere
		 * ein Ausfall, den wir uns selbst machen. */
		goto plain_sabm;
	}
	/* DER EINTRAG IST DER PLATZ DES URTEILS, UND ER MUSS VOR DER FRAGE
	 * DA SEIN.  Kommt keine Antwort, schreibt recover() sein "keine
	 * XID-Antwort" nur in einen Eintrag, den es schon gibt
	 * (ax_routeptr(...,0)) - und fuer eine Station, die nie antwortet,
	 * hat bis jetzt niemand einen angelegt: kein Relais (das tut es in
	 * xid_leg_open()), kein empfangener Rahmen.  Der Zaehler stand damit
	 * nach JEDEM Aufbau wieder auf null, und dieselbe stumme Station
	 * wurde bei jedem connect neu gefragt - ueber XID_MAXTRIES hinaus,
	 * endlos (Thomas, 2026-10-13).  Beim Relais faellt das nicht auf,
	 * weil sein Bein den Eintrag selbst anlegt; nur der eigene, direkte
	 * Aufbau hatte keinen.
	 *
	 * Der Eintrag traegt nur Wissen ueber die STATION, keinen Weg: ifp
	 * bleibt leer, und axroute() nimmt weiterhin den Fang
	 * (Axroute_default_ifp), wenn kein anderer Eintrag einen Port nennt.
	 *
	 * GEZAEHLT WIRD HIER, WENN DIE FRAGE HINAUSGEHT - nicht erst in
	 * recover(), wenn ihre Frist abgelaufen ist.  "Ein Aufbau, hoechstens
	 * eine Frage, und die ersten XID_MAXTRIES Aufbauten fragen wieder"
	 * (ax25.h) ist eine Aussage ueber die SITZUNG: die Frage ist gestellt,
	 * sobald sie auf der Leitung war.  Wer von Hand auflegt, sieht die
	 * XID, trennt sofort und ist damit meist schneller als T1 (5 s) -
	 * recover() lief dann nie, der Zaehler stand wieder auf null, und
	 * dieselbe stumme Station wurde bei jedem connect aufs Neue gefragt,
	 * endlos (Thomas, 2026-10-13).  Eine Antwort setzt den Zaehler
	 * ohnehin auf null zurueck (lapb_input), ein verlorenes Bein verliert
	 * damit nichts; nur das Schweigen zaehlt so schon beim ersten Mal.
	 */
	{
		struct ax_route *rp = ax_routeptr(axp->hdr.dest,1);

		if(rp != NULL){
			rp->xid_time = secclock();
			rp->xid_tries++;
		}
	}
	xid_our_info(axp->iface,
		     (axp->iface != NULL && axp->iface->eax25 != EAX25_OFF) ? 1 : 0,
		     YES, ax25_hop2hop(), &tbp);
	sendframe(axp,LAPB_COMMAND,XID|PF,-1,&tbp);
	axp->flags.neg_wait = YES;
	axp->retries = 0;
	stop_timer(&axp->t3);
	start_timer(&axp->t1);
	return;
plain_sabm:
	est_link_sabm(axp);
}

/* Wuerde "always" dieses Bein JETZT ablehnen?  Eine reine Zustandsfrage ohne
 * Nebenwirkung - damit der Aufrufer die Antwort kennt, BEVOR er est_link()
 * ruft: dessen Ablehnung loest den Block auf, und der Aufrufer hat womoeglich
 * noch die andere Haelfe eines Blockpaares in der Hand.
 *
 * Abgelehnt wird nur, was der Peer selbst abgelehnt hat: eine Station, die
 * auf unsere XID-Frage mit FRMR geantwortet hat.  Eine Station, deren
 * "nein" nur daraus folgt, dass sie unsere Frage nicht beantwortet hat, ist
 * keine Verweigerung - das ist eine Vermutung ueber eine stumme Station, und
 * eine stille Station hat nichts verweigert (ax25_v2_refused()).
 */
static int
ax25_v2_refused(uint8 *dest, const struct iface *ifp)
{
	struct ax_route *rp;

	/* OHNE PORT KEINE VERWEIGERUNG.  Ein Relais-Bein, dessen Ausgang
	 * keinen Weg kennt (keine Route und kein FANG), traegt iface == NULL;
	 * es kann niemanden verweigern, weil es gar nicht hinauskaeme.  Der
	 * Aufrufer entscheidet dann, was mit dem Bein geschieht - hier nur
	 * keine Aussage ueber den Peer.  (Ohne diese Zeile starb der
	 * Digi-Modus 2 an einem weiterzuleitenden SABM.)
	 */
	if(ifp == NULL || ifp->ax25_v2 != AX25V2_ALWAYS)
		return 0;
	if((rp = ax_routeptr(dest,0)) == NULL || rp->xid != AXR_XID_NO)
		return 0;
	/* NUR EINE FRMR IST EINE VERWEIGERUNG.
	 *
	 * "Kann kein XID" steht im Eintrag fuer zwei voellig verschiedene
	 * Faelle, und nur einer davon ist eine Auskunft des Peers:
	 *
	 *   xid_frmr     C hat auf unsere Frage mit FRMR geantwortet.  C sagt
	 *                uns damit ausdruecklich, dass hier kein XID hingehoert.
	 *   xid_time     Unsere Frage blieb unbeantwortet, und der Timer hat
	 *                daraus geschlossen: C spricht kein XID.  Das ist eine
	 *                VERMUTUNG ueber eine stumme Station - genug, um nicht
	 *                weiter zu fragen (Xid_probe_time) und um nicht mit
	 *                SABME zu kommen, denn wer nicht antwortet, kann ein
	 *                SABME auch nicht beantworten.  Es ist aber KEINE
	 *                Verweigerung.
	 *
	 * Der Unterschied ist der ganze Grund, warum der Eintrag die beiden
	 * getrennt haelt (ax25.h): eines sagt "ich kann nicht", das andere
	 * nur "ich habe nichts gehoert".  "Always" heisst "nur, wenn die
	 * Aushandlung gelingt" - und eine stille Station hat nichts verweigert.
	 *
	 * Vorher stand hier `xid_frmr || secclock() - xid_time <
	 * Xid_probe_time`, und die zweite Haelfte machte aus JEDER frischen
	 * Stille eine Verweigerung: der erste Aufruf nach einem Verstummen
	 * wurde abgelehnt, bevor ueberhaupt etwas gesendet war.  Im Labor hiess
	 * das: der Digipeat verweigerte den Aufbau zu einer Station, die
	 * einfach keine XID-Fragen beantwortet - und im Trace war zu sehen,
	 * dass er vorher noch selbst nach ihr gefragt hatte.
	 */
	return rp->xid_frmr;
}

/* Establish data link.
 *
 * ACHTUNG: eine Ablehnung (ax25_v2_refused()) loest den Block auf - und wenn
 * sein Gegenstueck keine Dienste mehr hat, auch das Gegenstueck mit.  Wer
 * danach noch weitermacht, benutzt freien Speicher.
 */
void
est_link(struct ax25_cb *axp)
{
	clr_ex(axp);
	axp->retries = 0;
	axp->eax25_probes = 0;
	/* Frischer Aufbau, frischer DM-Vermerk. */
	axp->eax25_dmed = 0;
	/* Ein Aufbau, ein Zaehlimpuls fuer das "kann kein Modulo 128". */
	eax25_age(axp->hdr.dest);
	/* Fuer diese Verbindung noch nichts geklemmt - die XID-Antwort des
	 * Peers fuellt das (ax25_apply_peer_xid), oder die Erinnerung
	 * unten, die noch nicht verfallen ist. */
	axp->peer_n1 = 0;
	axp->peer_k = 0;
	axp->peer_segment = 0;
	axp->mmask = eax25_wanted(axp->iface,axp->hdr.dest);
	eax25_mark(axp);
	/* WAS DER PEER FRUEHER GESAGT HAT, GILT WEITER.
	 *
	 * Eine echte XID-Antwort beschreibt die STATION, nicht diese eine
	 * Verbindung: sein N1, sein k, sein SREJ, sein Segmenter.  Solange
	 * sie nicht verfallen ist (Xid_expire_time), wird sie ohne neue
	 * Frage angewandt - und gefragt wird ohnehin nicht, denn xid == YES
	 * faellt aus xid_may_probe() heraus.  Ohne das hier starb jede
	 * gemessene Grenze mit der Sitzung, und eine Station, die einmal
	 * brav geantwortet hatte, wurde ohne ihre Grenzen bedient.
	 *
	 * proto = V2_2 und peer_srej kommen mit: eine Antwort auf ein XID
	 * ist genau der Nachweis, den srej_ok() verlangt - hier aus einer
	 * frueheren Sitzung, aber nicht aelter als Xid_expire_time.  Das
	 * ist die Grenze, die den alten Einwand entkraeftet (der Vermerk
	 * duerfe nicht die Lebensdauer des EINTRAGS haben): 1,5 Tage sind
	 * nicht 24,8 Tage Route, und danach wird neu gefragt.
	 */
	{
		struct ax_route *rp = ax_routeptr(axp->hdr.dest,0);

		if(rp != NULL && rp->xid == AXR_XID_YES && rp->peer_xid_time &&
		   (Xid_expire_time <= 0 ||
		    secclock() - rp->peer_xid_time < Xid_expire_time)){
			ax25_apply_peer_xid(axp,rp->peer_n1rx,rp->peer_window);
			axp->peer_segment = rp->peer_segment ? 1 : -1;
			axp->proto = V2_2;
			if(rp->peer_srej)
				axp->flags.peer_srej = YES;
		}
	}
	/* AX.25 2.2, F1: if this port starts the negotiation and the route
	 * does not know yet whether the peer speaks XID, ONE XID probe goes
	 * before the SABM.  It is decoupled from the modulus eax25 just
	 * decided above: U-frames carry their own, and the probe forces
	 * nothing on him (doc/AX25-2.2-GAP.md).
	 *
	 * That is not the same as discarding what comes back.  No
	 * connection parameter is imposed - but the answer is read, and
	 * the largest common denominator it allows is ours to work with,
	 * because the most efficient connection is the one both ends
	 * agreed on.  Today the modulus is what is taken from it: an
	 * answer that offers modulo-128 turns the SABM below into a
	 * SABME, and it does so at "accept" as well (accept-or-XID).  The
	 * I-field length and the window it also carries are parsed and
	 * clamped to for this one link (ax25_apply_peer_xid): they are ours
	 * to honour, not his to obey, and nobody on the wire can see whether
	 * we did.  They are NOT remembered per route - a XID seen through a
	 * digipeater describes the far end, not the nexthop (xid.h).
	 *
	 * A route can also remember "no XID" from an earlier probe or an
	 * FRMR.  That is not forever: a peer that merely kept silent may
	 * have learned XID in the meantime, so after Xid_probe_time that
	 * verdict expires and we probe again - but a peer that answered
	 * FRMR told us outright that XID is not wanted here, and that is
	 * honored for good (xid_frmr).
	 */
	if(axp->iface != NULL &&
	   (axp->iface->ax25_v2 == AX25V2_INITIATE ||
	    axp->iface->ax25_v2 == AX25V2_ALWAYS)){
		struct ax_route *rp = ax_routeptr(axp->hdr.dest,0);
		if(rp != NULL && rp->xid == AXR_XID_NO){
			/* Silently decided "does not answer" - about the
			 * QUESTION, not about modulo-128 (that verdict is
			 * not written from a timeout at all; see
			 * recover()/xid_leg_done()).  And NOT for good.
			 * xid_may_probe() answers whether THIS attempt asks
			 * again: the first XID_MAXTRIES attempts do, so a
			 * question lost on HF does not cost a whole hour,
			 * and after that only Xid_probe_time opens it up
			 * once more.  Always at initiate: a refused
			 * negotiation ends the attempt in ALWAYS mode below,
			 * but the peer who turned silent gets another
			 * chance instead of an instant hangup.
			 *
			 * FUER EIN RELAIS-BEIN GILT DER KURZE WEG NICHT.
			 * Die Frage dieser Sitzung war die XID des Anrufers,
			 * die wir am Punkt D geschluckt und mit einer eigenen
			 * Sonde beantwortet haben - dieselbe Station,
			 * dieselbe Sitzung.  Hier noch einmal zu fragen
			 * versteckte das SABM nur hinter weiteren zehn
			 * Sekunden fuer eine Station, die gerade geschwiegen
			 * hat.  Es bleibt beim alten Nachfassen nach
			 * Xid_probe_time. */
			int ask = (axp->peer != NULL)
				? (!rp->xid_frmr &&
				   secclock() - rp->xid_time >= Xid_probe_time)
				: xid_may_probe(rp);
			if(ask){
				est_link_xid(axp);
				return;
			}
			if(ax25_v2_refused(axp->hdr.dest,axp->iface)){
				/* always: a peer who REFUSED the
				 * negotiation (FRMR) is not wanted here at
				 * all - no call.  A peer who only kept
				 * silent has refused nothing, and
				 * ax25_v2_refused() says so. */
				free_q(&axp->txq);
				axp->reason = LB_NEGOTIATION;
				lapbstate(axp,LAPB_DISCONNECTED);
				return;
			}
		}
		/* NUR DER NOCH NICHT ENTSCHIEDENE FALL.  Ein rp mit
		 * xid == NO ist oben schon abgehandelt - und fuer ein
		 * Relais-Bein nach einer ANDEREN Regel (Xid_probe_time,
		 * nicht xid_tries).  Hier trotzdem noch einmal
		 * xid_may_probe() zu fragen ueberstimmte sie und schickte
		 * eine zweite Sonde, wo das SABM des Anrufers laengst
		 * durchgereicht gehoert (s2).  xid == YES fragt nie. */
		if(rp == NULL ||
		   (rp->xid == AXR_XID_UNKNOWN && xid_may_probe(rp))){
			est_link_xid(axp);
			return;
		}
	}
	est_link_sabm(axp);
}
/* Clear exception conditions */
static void
clr_ex(struct ax25_cb *axp)
{
	axp->flags.remotebusy = NO;
	stop_timer(&axp->t4);
	axp->flags.rejsent = NO;
	/* axp->response = 0; */
	stop_timer(&axp->t3);
}
/* Enquiry response */
static void
enq_resp(struct ax25_cb *axp)
{
	char ctl;

	ctl = busy(axp) ? RNR|PF : RR|PF;
	sendctl(axp,LAPB_RESPONSE,ctl);
	/* axp->response = 0; */
	/* stop_timer(&axp->t3); */
}
/* Invoke retransmission */
static void
inv_rex(struct ax25_cb *axp)
{
	axp->vs -= axp->unack;
	axp->vs &= axp->mmask;
	axp->unack = 0;
}
/* Send S or U frame to currently connected station */
int
sendctl(
struct ax25_cb *axp,
enum lapb_cmdrsp cmdrsp,
int cmd
){
	int ctlx = -1;

	switch(cmd & ~PF){
	case RR:
	case REJ:
	case SREJ:
	case UA:
		axp->flags.rnrsent = 0;
		break;
	case RNR:
		axp->flags.rnrsent = 1;
		break;
	}
	if((ftype(cmd & 0xff) & 0x3) == S){     /* Insert V(R) if S frame */
		if(axp->mmask == EMMASK){
			/* Modulo-128 splits an S frame over two octets: the type
			 * stays in the first, N(R) and the poll/final bit move to
			 * the second.
			 */
			ctlx = ((cmd & PF) ? PF_EAX25 : 0) | (axp->vr << 1);
			cmd &= 0x0f;
		} else
			cmd |= (axp->vr << 5);
	} else if(axp->mmask == EMMASK && (cmd & ~PF) == SABM){
		/* This is where the decision to speak modulo-128 leaves the
		 * node: est_link() always asks for a SABM, and whoever set
		 * mmask beforehand turns it into a SABME.  A U frame keeps its
		 * single octet in both moduli, so nothing else changes - and
		 * that is precisely why a SABME is readable by a peer that has
		 * agreed to nothing yet.
		 */
		cmd = SABME | (cmd & PF);
	}
	return sendframe(axp,cmdrsp,cmd,ctlx,NULL);
}
/* Start data transmission on link, if possible
 * Return number of frames sent
 */
int
lapb_output(struct ax25_cb *axp)
{
	struct mbuf *bp;
	struct mbuf *tbp;
	int control;            /* int, not char: modulo-128 puts V(S) up to 127
				 * in bits 1..7, and 127<<1 does not fit a signed
				 * char.  The old value never exceeded 0xEE and
				 * survived by the mask below happening to cut the
				 * sign extension off again. */
	int sent = 0;
	int i;

	if(axp == NULL
	 || (axp->state != LAPB_RECOVERY && axp->state != LAPB_CONNECTED)
	 || axp->flags.remotebusy)
		return 0;

	/* A DAMA slave sends nothing of its own accord.  Nothing is lost by
	 * refusing here: the frames stay on axp->txq, and lapb_input() calls
	 * this again at the end of every received frame - so the next poll
	 * takes them out.  That call was already there for piggybacking an
	 * ack, which is why the slave needs no queue of its own.
	 */
	if(dama_holds(axp)){
		if(axp->txq != NULL)
			dama_wait(axp);
		return 0;
	}

	/* Polled, and T1 has meanwhile said that what we sent last time was
	 * never acknowledged.  In ordinary AX.25 T1 would have resent it long
	 * ago; under DAMA it may not transmit at all, so the retransmission
	 * happens here instead - in the window a poll opened, which is the
	 * only place a slave is allowed to put anything on the air.
	 *
	 * Without this a DAMA slave never retransmits anything.  On a clean
	 * channel that is invisible; measured on one with 30% loss, the node
	 * answered every poll with a bare RR and its data never moved again.
	 */
	if(axp->dama_rex){
		axp->dama_rex = 0;
		if(axp->unack)
			inv_rex(axp);
	}

	/* Dig into the send queue for the first unsent frame */
	bp = axp->txq;
	for(i = 0; i < axp->unack; i++){
		if(bp == NULL)
			break;  /* Nothing to do */
		bp = bp->anext;
	}
	/* Start at first unsent I-frame, stop when either the
	 * number of unacknowledged frames reaches the maxframe limit,
	 * or when there are no more frames to send
	 */
	while(bp != NULL && axp->unack < axp->maxframe){
		int controlx = -1;

		if(axp->mmask == EMMASK){
			control = I | (axp->vs++ << 1);
			controlx = (axp->vr << 1);      /* no poll on this one */
		} else
			control = I | (axp->vs++ << 1) | (axp->vr << 5);
		axp->vs &= axp->mmask;
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp == NULL)
			return sent;    /* Probably out of memory */
		sendframe(axp,LAPB_COMMAND,control,controlx,&tbp);
		axp->unack++;
		/* We're implicitly acking any data he's sent, so stop any
		 * delayed ack
		 */
		/* axp->response = 0; */
		if(!run_timer(&axp->t1)){
			stop_timer(&axp->t3);
			start_timer(&axp->t1);
		}
		start_timer(&axp->t5);
		sent++;
		bp = bp->anext;
		if(!axp->flags.rtt_run){
			/* Start round trip timer */
			axp->rtt_seq = (control >> 1) & axp->mmask;
			axp->rtt_time = msclock();
			axp->flags.rtt_run = 1;
		}
	}
	return sent;
}
/* General purpose AX.25 frame output */
int
sendframe(
struct ax25_cb *axp,
enum lapb_cmdrsp cmdrsp,
int ctl,
int ctlx,                       /* Second control octet, or -1 for none */
struct mbuf **bpp
){
	struct iface *ifp;
	struct mbuf *bp;
	int dpid = -1;

	if(bpp == NULL){
		bp = NULL;
		bpp = &bp;
	}
	/* READ THE PROTOCOL ID WHILE IT IS STILL AT THE FRONT.  Only an I
	 * frame carries one - its control octet has bit 0 clear - and for an
	 * I frame from send_ax25() the pid is the first byte of the
	 * information field, here, before the control field and then the
	 * addresses are pushed in front of it.  A pure control frame has
	 * none, and -1 lets it count as plain AX.25.
	 */
	if((ctl & 1) == 0 && *bpp != NULL && (*bpp)->cnt > 0)
		dpid = (*bpp)->data[0];
	/* Pushed first so that it ends up behind ctl, which is pushed next */
	if(ctlx != -1){
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = ctlx;
	}
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;
	if ((ctl & 3) != U)
		stop_timer(&axp->t2);
	axp->hdr.cmdrsp = cmdrsp;
	dama_mark(axp);
	htonax25(&axp->hdr,bpp);
	if ((ifp = axp->iface)) {
		if (ifp->forw)
			ifp = ifp->forw;
		logsrc(ifp,ifp->hwaddr);
		logdest(ifp,axp->hdr.nextdigi != axp->hdr.ndigis ? axp->hdr.digis[axp->hdr.nextdigi] : axp->hdr.dest);
		/* WHICH OF THE THREE NUMBERS THIS FRAME BELONGS TO.  Answered
		 * from the pid read on entry, the last moment it could be told
		 * apart from data.
		 *
		 * An I frame carrying an IP datagram belongs to ipsndcnt: q_pkt()
		 * counted the datagram before this frame existed.  One carrying
		 * a segment belongs to whoever sliced the datagram - the sender
		 * still knew what was inside, this frame only says PID_SEGMENT.
		 *
		 * Everything else is a plain AX.25 frame and is counted here:
		 * every link control frame (SABM, UA, DM, RR, RNR, REJ), a
		 * keepalive, own text, and each segment of it.  So "tot == ip +
		 * ax25" holds.  A retransmission counts like any other; it is a
		 * frame the driver counts in rawsndcnt too.
		 */
		if(dpid != PID_SEGMENT && !ax25_pid_is_ip(dpid))
			ifp->ax25sndcnt++;
		return (*ifp->raw)(ifp,bpp);
	}
	free_p(bpp);
	return -1;
}
/* Set new link state */
void
lapbstate(
struct ax25_cb *axp,
enum lapb_state s
){
	enum lapb_state oldstate;

	oldstate = axp->state;
	axp->state = s;
	/* Auf einem Master-Port ist eine neue Verbindung der Anlass, die Runde
	 * anzustossen: vorher gibt es niemanden zu pollen, und sie ruht.
	 */
	if(s == LAPB_CONNECTED && oldstate != LAPB_CONNECTED)
		dama_master_kick(axp->iface);
	if(s == LAPB_DISCONNECTED){
		/* The link is over, so what was agreed for it is over too.  A
		 * control block can be reused for the next connection to the
		 * same station, and that one has to earn its DAMA discipline
		 * again rather than inherit it.
		 */
		axp->dama_link = 0;
		stop_timer(&axp->t1);
		stop_timer(&axp->t2);
		stop_timer(&axp->t3);
		stop_timer(&axp->t4);
		stop_timer(&axp->t5);
		free_q(&axp->txq);
		if (axp->peer)
			disc_ax25(axp->peer);
		if (axp->services == NULL &&
		    (!axp->peer || axp->peer->state == LAPB_DISCONNECTED)) {
			if (axp->peer != NULL)
				del_ax25(axp->peer);
			del_ax25(axp);
			return;
		}
	}
	/* Don't bother the consumers unless the state is really changing.
	 * Walk with the next pointer in hand: one of them may close itself
	 * here, and closing the last takes the link with it.
	 */
	if(oldstate != s){
		struct axservice *sp;
		struct axservice *spnext;

		for(sp = axp->services; sp != NULL; sp = spnext){
			spnext = sp->next;
			if(sp->s_upcall != NULL)
				(*sp->s_upcall)(sp,oldstate,s);
		}
	}
}
/* Resequence a valid incoming I frame */
static void
resequence(
struct ax25_cb *axp,
struct mbuf **bpp,
int ns,
int pf,
int poll)
{

	int cnt;
	int old_vr;
	int rejcond;
	int sum;
	int tmp;
	struct axreseq *rp;
	struct mbuf *bp;
	struct mbuf *tp;
	uint8 *p;

	if(bpp == NULL || *bpp == NULL)
		return;

	rp = &axp->reseq[ns];
	if(ns != ((axp->vr - 1) & axp->mmask) && rp->bp == NULL){
		for(sum = 0,tp = *bpp;tp;tp = tp->next)
			for(p = tp->data,cnt = tp->cnt;cnt > 0;cnt--)
				sum += *p++;
		if(ns == axp->vr || sum != rp->sum){
			rp->sum = sum;
			rp->bp = *bpp;
			*bpp = NULL;
		}
	}
	if(*bpp != NULL)
		free_p(bpp);

	old_vr = axp->vr;
	while(axp->reseq[axp->vr].bp != NULL){
		axp->vr = (axp->vr + 1) & axp->mmask;
		axp->flags.rejsent = NO;
	}
	rejcond = 0;
	for(tmp = (axp->vr + 1) & axp->mmask;tmp != old_vr;tmp = (tmp + 1) & axp->mmask)
		if(axp->reseq[tmp].bp != NULL){
			rejcond = 1;
			break;
		}

	if(rejcond && !axp->flags.rejsent){
		axp->flags.rejsent = YES;
		/* A peer that proved itself SREJ-capable gets a SREJ instead
		 * of a REJ: both ask for the first missing frame, but the
		 * SREJ tells him not to repaint the ones we already have
		 * (3.6.4).  Who is capable is decided at XID time - PI=3 -
		 * and peer_srej stays clear until then.  rejsent guards it
		 * the same way it guards the REJ: one request per gap.
		 */
		sendctl(axp,LAPB_RESPONSE,
			(srej_ok(axp) ? SREJ : REJ)|pf);
	} else {
		if(poll){
			/* Auch hier: als Master nicht in einen fremden Zug
			 * hinein.  Der REJ-Zweig darueber bleibt sofort - ein
			 * REJ fordert eine Wiederholung an, und ihn
			 * zurueckzuhalten wuerde den Link anhalten, waehrend
			 * flags.rejsent schon gesetzt ist.
			 */
			if(dama_master_holds(axp)){
				dama_master_owe(axp);
			} else {
				tmp = busy(axp) ? RNR : RR;
				sendctl(axp,LAPB_RESPONSE,tmp|pf);
			}
		} else
			start_timer(&axp->t2);
	}

	while((bp = axp->reseq[old_vr].bp) != NULL){
		axp->reseq[old_vr].bp = NULL;
		old_vr = (old_vr + 1) & axp->mmask;
		if(axp->peer)
			send_ax25(axp->peer,&bp,-1);
		else
			procdata(axp,&bp);
	}
}

/* Process a valid incoming I frame */
static void
procdata(
struct ax25_cb *axp,
struct mbuf **bpp
){
	int pid;
	int seq;

	/* Extract level 3 PID */
	if((pid = PULLCHAR(bpp)) == -1)
		return; /* No PID */

	if(axp->segremain != 0){
		/* Reassembly in progress; continue */
		seq = PULLCHAR(bpp);
		if(pid == PID_SEGMENT
		 && (seq & SEG_REM) == axp->segremain - 1){
			/* Correct, in-order segment */
			append(&axp->rxasm,bpp);
			if((axp->segremain = (seq & SEG_REM)) == 0){
				/* Done; kick it upstairs */
				*bpp = axp->rxasm;
				axp->rxasm = NULL;
				pid = PULLCHAR(bpp);
				/* The wire showed fragments; this is the
				 * datagram they add up to. */
				ax25_dump_reasm(axp->iface,&axp->hdr,pid,bpp);
				handleit(axp,pid,bpp);
			}
		} else {
			/* Error! */
			free_p(&axp->rxasm);
			axp->rxasm = NULL;
			axp->segremain = 0;
			free_p(bpp);
		}
	} else {
		/* No reassembly in progress */
		if(pid == PID_SEGMENT){
			/* Start reassembly */
			seq = PULLCHAR(bpp);
			if(!(seq & SEG_FIRST)){
				free_p(bpp);    /* not first seg - error! */
			} else {
				/* Put first segment on list */
				axp->segremain = seq & SEG_REM;
				axp->rxasm = (*bpp);
				*bpp = NULL;
			}
		} else {
			/* Normal frame; send upstairs */
			handleit(axp,pid,bpp);
		}
	}
}
/* New-style frame segmenter. Returns queue of segmented fragments, or
 * original packet if small enough
 */
struct mbuf *
segmenter(
struct mbuf **bpp,      /* Complete packet */
uint ssize              /* Max size of frame segments */
){
	struct mbuf *result = NULL;
	struct mbuf *bptmp;
	uint len,offset;
	int segments;

	/* Is it small enough to go in one frame?  len counts the PID that the
	 * caller pushed on a moment ago, and the PID is not part of the
	 * information field that paclen bounds - it sits in front of it, in
	 * the header.  So a datagram of exactly paclen bytes still fits, and
	 * the comparison has to be against paclen+1.  This is bookkeeping,
	 * not slack: the frame that goes out has an information field of at
	 * most paclen bytes either way.
	 */
	len = len_p(*bpp);
	if(len <= ssize+1){
		result = *bpp;
		*bpp = NULL;
		return result;  /* Too small to segment */
	}
	/* Below paclen 2 there is nothing left once the counter has its byte,
	 * and ssize would be zero for the division just below.  "ax25 paclen
	 * 1" is accepted by dopaclen() and used to kill the node with SIGFPE
	 * right here.
	 */
	if(ssize < 2){
		free_p(bpp);
		return NULL;
	}
	/* The segment counter is the first byte OF the information field, so
	 * it comes out of paclen.  The PID_SEGMENT byte does not - §3.4: the
	 * PID is not counted in the information field, it is header.  Hence
	 * one, not two.  Appendix C6.1 instead reads its two octet header as
	 * N1-2 octets of data and would charge both; we keep the extra byte
	 * on purpose, see doc/AX25-MTU-SEGMENTATION.md.
	 */
	ssize -= 1;
	segments = 1 + (len - 1) / ssize;       /* # segments  */
	/* Bit 7 of that counter is SEG_FIRST, so it can count no further than
	 * SEG_REM.  Sending more anyway sets SEG_FIRST on every segment whose
	 * count still has bit 7, and the far end then reassembles a truncated
	 * datagram from the first few and discards all the rest, one by one,
	 * as "not first segment".  Refuse instead: the datagram cannot be
	 * expressed in this protocol at this paclen.
	 *
	 * The largest datagram that still fits is 128 * (paclen-1) - 1, so at
	 * paclen 256 it would take 32640 bytes to get here and the case is
	 * out of reach.  At paclen 12 or less an ordinary MTU of 1500 walks
	 * straight into it.
	 */
	if(segments - 1 > SEG_REM){
		free_p(bpp);
		return NULL;
	}
	offset = 0;

	while(segments != 0){
		offset += dup_p(&bptmp,*bpp,offset,ssize);
		if(bptmp == NULL){
			free_q(&result);
			break;
		}
		/* Make room for segmentation header */
		pushdown(&bptmp,NULL,2);
		bptmp->data[0] = PID_SEGMENT;
		bptmp->data[1] = --segments;
		if(offset == ssize)
			bptmp->data[1] |= SEG_FIRST;
		enqueue(&result,&bptmp);
	}
	free_p(bpp);
	return result;
}

static void
handleit(
struct ax25_cb *axp,
int pid,
struct mbuf **bpp
){
	struct axlink *ipp;
	struct axservice *sp;

	/* Does this protocol get in on this port?  Ahead of everything, as in
	 * the UI path - see pidfilter.c.  A segmented datagram is reassembled
	 * first and only then dropped: the true protocol id is inside, and the
	 * segments carry PID_SEGMENT and say nothing about what they are.
	 */
	if(pid_blocked(axp->iface,PF_IN,pid)){
		free_p(bpp);
		return;
	}

	/* A consumer for this protocol id first: one that is already attached,
	 * or one the configuration says to start.  Only then the node's own
	 * protocols, so that a service configured for a callsign takes what
	 * arrives there without taking the protocol away from the node.
	 */
	if((sp = find_axservice(axp,pid)) == NULL){
		/* PLAIN TEXT ON A LINK WE OPENED FOR SOMETHING ELSE.  We
		 * called this station to speak FlexNet, NET/ROM or IP, and it
		 * greets us in text the way an XNET does.  Starting a login on
		 * that is wrong twice over: the greeting would talk to a shell
		 * that answers it with command errors until the far end gives
		 * up, and where a listener is configured but nobody answers,
		 * axserv_start() sends "*** ... is not answering" into the
		 * link and disconnects - taking the interlink with it.
		 *
		 * Every other protocol id rides on regardless, in both
		 * directions: carrying several at once is what the pid is for.
		 * Text is the exception because it is not a protocol - it has
		 * no frame in which "not for you" could be said.
		 */
		if(pid == PID_NO_L3 && axp->openpid != 0
		   && axp->openpid != PID_NO_L3){
			free_p(bpp);
			return;
		}
		sp = axserv_start(axp,pid);
	}
	if(sp != NULL){
		/* One frame stays one entry on the queue.  Kernel AX.25 was
		 * SOCK_SEQPACKET and the protocols that ride on a connection
		 * rely on it: FBB's compressed forwarding reads the end of an
		 * uncompressed block off the frame boundary.  append() would
		 * melt two frames into one chain and there is no recovering
		 * that afterwards.
		 */
		enqueue(&sp->rxq,bpp);
		if(sp->r_upcall != NULL)
			(*sp->r_upcall)(sp,(int) len_qbytes(sp->rxq));
		return;
	}

	for(ipp = Axlink;ipp->funct != NULL;ipp++){
		if(ipp->pid == pid)
			break;
	}
	if(ipp->funct != NULL){
		/* The counterpart of the ax25recvcnt++ in ax_recv(), and needed for
		 * the same reason: that one counted the frame on its way in
		 * before anything was known about it, and ip_route() inside the
		 * handler below counts the datagram in iprecvcnt, so the same
		 * frame would stand in both numbers.
		 *
		 * Once per DATAGRAM, not once per frame, and that is what makes
		 * a segmented one come out right: the segments were counted
		 * separately as they arrived, only the last of them carried the
		 * frame through here, and so N frames leave N-1 in ax25recvcnt
		 * against the one in iprecvcnt - which is what tot says.
		 *
		 * The pid is read from the reassembled data, which is the
		 * point: it says nothing at all while the frame says
		 * PID_SEGMENT.  See ax25_pid_is_ip().
		 */
		if(ax25_pid_is_ip(pid) && axp->iface != NULL)
			axp->iface->ax25recvcnt--;
		(*ipp->funct)(axp->iface,axp,axp->hdr.dest,axp->hdr.source,bpp,0);
	}
	else
		free_p(bpp);
}

int
busy(
struct ax25_cb *axp)
{
	int held;
	struct axservice *sp;

	if(axp->peer)
		return space_ax25(axp->peer) <= 0;
	/* One window for the whole link: what any consumer has not taken
	 * counts against it.  AX.25 cannot say "not ready for this pid".
	 */
	for(sp = axp->services, held = 0; sp != NULL; sp = sp->next)
		held += (int) len_qbytes(sp->rxq);
	return held >= (int) axp->window;
}

void
ax_t2_timeout(
void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;

	/* The delayed acknowledgement, and for a DAMA slave it stays delayed:
	 * the ack rides out on the answer to the next poll instead.  This is
	 * the same shape as the kernel's AX25_COND_ACK_PENDING - resequence()
	 * already splits "polled, answer now" from "not polled, start T2", so
	 * all that was missing was for T2 to hold its tongue.
	 */
	if (dama_holds(axp)) {
		dama_wait(axp);
		return;
	}
	/* UND ALS MASTER QUITTIEREN WIR NICHT AUSSER DER REIHE.
	 *
	 * Gemessen war: ein Dauersender schickt I-Rahmen ohne Poll, wir
	 * quittieren jeden binnen 0,3 s - und weil die Quittung sein Fenster
	 * wieder oeffnet, kann er endlos senden, ohne je einen Verstoss zu
	 * begehen (Thomas: "dann gilt fuer den Dauersender quasi kein DAMA
	 * mehr").  Das Kriterium "Kommando mit P" fasst ihn nicht, denn er
	 * pollt ja nicht.
	 *
	 * Die Quittung ist das einzige Mittel, das greift, und TNN benutzt
	 * genau dieses: dort geht auch sie durch sdl2fr() und wartet auf den
	 * Zug.  Ohne Quittung ist nach maxframe unquittierten Rahmen Schluss.
	 * Der Master muss sein "Ende" also gar nicht kennen - er fuettert ihn
	 * einfach nicht.
	 */
	if (dama_master_holds(axp)) {
		axp->dama_ackpend = 1;
		return;
	}
	lapb_ack_now(axp);
}

void
lapb_ack_now(struct ax25_cb *axp)
{
	int i;

	if (!axp->flags.rejsent) {
		for (i = 0; i <= EMMASK; i++) {
			if (axp->reseq[i].bp) {
				axp->flags.rejsent = YES;
				sendctl(axp, LAPB_RESPONSE,
					srej_ok(axp) ? SREJ : REJ);
				return;
			}
		}
	}
	sendctl(axp, LAPB_RESPONSE, busy(axp) ? RNR : RR);
}

void
ax_t5_timeout(
void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)p;

	/* The idle disconnect.  A DAMA slave may not send the DISC unbidden,
	 * so try again later rather than dropping it: the link is idle by
	 * definition, and one more idle period costs nothing.
	 */
	if(dama_holds(axp)){
		start_timer(&axp->t5);
		return;
	}

	if(axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY){
		free_q(&axp->txq);
		axp->retries = 0;
		sendctl(axp,LAPB_COMMAND,DISC|PF);
		stop_timer(&axp->t3);
		start_timer(&axp->t1);
		lapbstate(axp,LAPB_DISCPENDING);
	}
}

/* WHICH OF OUR CALLSIGNS, and over which port.  This is the first half of
 * build_path(), and it stands on its own because a link is named by BOTH
 * addresses: until axroute() has stamped the source, one half of the name
 * does not exist yet, so open_ax25() cannot even look the link up.  It works
 * on the caller's header and touches no control block.
 */
void
ax25_resolve_path(
struct ax25 *hdr,
struct iface **ifpp,
const struct ax25_opts *opts)
{
	uint8 wanted[AXALEN];
	struct iface *ifp;

	/* axroute() picks the interface and stamps its callsign over the
	 * source.  Where the caller has chosen either of those, put its choice
	 * back afterwards rather than teaching the router about it - the
	 * routing itself is unchanged.
	 */
	addrcp(wanted, hdr->source);
	axroute(hdr,&ifp,0);
	if (opts) {
		if (opts->iface) {
			ifp = opts->iface;
			/* axroute() stamps the source with the callsign of the
			 * interface it chose; a caller who named a port must
			 * get that port's callsign instead, not the node's.
			 */
			if (!opts->ownsource && ifp->hwaddr)
				addrcp(hdr->source, ifp->hwaddr);
		}
		if (opts->ownsource)
			addrcp(hdr->source, wanted);
	}
	*ifpp = ifp;
}

/* What follows from the path once it is settled, and the same for a link we
 * open and one we answer.
 */
static void
path_timing(struct ax25_cb *axp)
{
	axp->srt = 0;
	axp->mdev = (T1init * (1 + 2 * (axp->hdr.ndigis - axp->hdr.nextdigi)) + 2) / 4;
	set_timer(&axp->t1, 4 * axp->mdev);
	ax25_apply_iface_limits(axp);
}

/* Take a path that has ALREADY been resolved.  Not the same as calling
 * build_path() a second time: axroute() inserts digipeaters and stamps our
 * callsign over the source, so running it twice on one header is not running
 * it once.
 */
/* The called side of a link that never leaves the node.  This is the SABM
 * branch of lapb_input() with everything left out that only makes sense on a
 * channel: no UA to send, no timers to start, no sequence numbers to reset,
 * because nothing here can be lost or arrive twice.  What remains is the part
 * that matters - the state change, so the consumers hear it, and the same
 * question the incoming path asks: is a service configured for the callsign
 * that was called, and does it want to greet before a byte arrives?
 */

void lapb_loop_up(struct ax25_cb *axp)
{
	struct ax_route *axr;

	lapbstate(axp,LAPB_CONNECTED);
	if(axp->services != NULL)
		return;
	/* hdr.source is the callsign that was called - the header of the
	 * called side is already turned round, as after build_path(reverse).
	 */
	axr = ax_routeptr(axp->hdr.dest,0);
	if(axr && axr->jumpstart)
		axserv_start(axp,PID_NO_L3);
	axserv_connected(axp);
}

/*---------------------------------------------------------------------------*/

/* What the caller sends is what the called side receives, and handleit() is
 * where a received frame goes anyway - so the consumers, the listeners and
 * the handover to a client see no difference at all.
 *
 * No segmenting: paclen bounds what fits in a frame on the air, and there is
 * no frame and no air here.  With pid == -1 the protocol id is already on the
 * front of each buffer in the chain, which is how NET/ROM and a relayed leg
 * send, so it is read back off there and each one is delivered on its own.
 */

/* Hand ONE frame over on the next turn of the event loop, then let go of
 * everything.
 *
 * Delivering from inside send_ax25() looked obvious and was wrong: the first
 * frame is what starts the listener on the other side, a listener may refuse
 * - "is not answering" - and the refusal disconnects, which takes this side
 * down with it: control block, service and, for a console session, the
 * session itself.  send_ax25() would then return into a caller that goes on
 * using exactly those.  axclient_parse() does:
 *
 *      send_ax25(Current->cb.ax25, &bp, PID_NO_L3);
 *      if (Current->record) ...              <- Current is gone by now
 *
 * Nobody calling send_ax25() expects sending to tear the link down under
 * them, and a real link never does - what is sent goes on a queue and leaves
 * later.  So this one queues too.
 *
 * One frame per turn, and the timer for the next is started BEFORE the
 * delivery: after handleit() neither block may be touched again, because
 * either of them may be gone.  A block that dies takes its timers with it
 * (del_ax25 stops them), so the one just started dies with it.
 */

static void loop_flush_soon(struct ax25_cb *axp);

static void loop_flush(void *p)
{
	struct ax25_cb *axp = (struct ax25_cb *) p;
	struct ax25_cb *peer = axp->loop;
	struct axservice *sp;
	struct axservice *spnext;
	struct mbuf *bp;
	int pid;
	int room;

	if(peer == NULL){
		free_q(&axp->txq);
		return;
	}
	if((bp = dequeue(&axp->txq)) == NULL)
		return;
	if(axp->txq != NULL)
		loop_flush_soon(axp);
	if((pid = PULLCHAR(&bp)) == -1){
		free_p(&bp);
		return;
	}
	handleit(peer,pid,&bp);

	/* AND NOW SAY THERE IS ROOM AGAIN, which is what the far end's
	 * acknowledgement does on a link that goes out over the air.  Without
	 * it a writer that asks space_ax25() first stops for good: the window
	 * is one frame, so as soon as one sits in the queue there is no room,
	 * and nothing would ever tell it otherwise.  That is a session that
	 * greets and then falls silent.
	 *
	 * After the delivery, because that may have taken the whole link down
	 * - so ask whether this block is still there at all.
	 */
	if(!ax25_alive(axp))
		return;
	/* Everything handed over, and somebody asked to disconnect while it
	 * was still in hand - now it may happen.  disc_ax25() set the flag and
	 * left rather than throw the rest away.
	 */
	if(axp->txq == NULL && axp->flags.closed){
		disc_ax25(axp);
		return;
	}
	room = space_ax25(axp);
	if(room <= 0)
		return;
	for(sp = axp->services;sp != NULL;sp = spnext){
		spnext = sp->next;
		if(sp->t_upcall != NULL)
			(*sp->t_upcall)(sp,room);
	}
}

static void loop_flush_soon(struct ax25_cb *axp)
{
	axp->loop_timer.func = loop_flush;
	axp->loop_timer.arg = axp;
	set_timer(&axp->loop_timer,1);
	start_timer(&axp->loop_timer);
}

int lapb_loop_send(struct ax25_cb *axp,struct mbuf **bpp,int pid)
{
	int len;
	struct mbuf *bp;
	struct mbuf *next;

	if(axp->loop == NULL || bpp == NULL || *bpp == NULL){
		free_p(bpp);
		return -1;
	}
	len = (int) len_p(*bpp);
	/* The protocol id goes on the front, the way it does in a frame on the
	 * air, so the queue holds one kind of thing and the delivery reads it
	 * back off.  With pid == -1 it is already there.
	 */
	if(pid != -1){
		pushdown(bpp,NULL,1);
		(*bpp)->data[0] = (uint8) pid;
		enqueue(&axp->txq,bpp);
	} else {
		for(bp = *bpp;bp != NULL;bp = next){
			struct mbuf *one = bp;

			next = bp->anext;
			one->anext = NULL;
			enqueue(&axp->txq,&one);
		}
		*bpp = NULL;
	}
	loop_flush_soon(axp);
	return len;
}

/*---------------------------------------------------------------------------*/

void
ax25_adopt_path(
struct ax25_cb *axp,
struct iface *ifp,
const struct ax25 *hdr)
{
	axp->routing_changes++;
	axp->iface = ifp;
	axp->hdr = *hdr;
	path_timing(axp);
}

void
build_path(
struct ax25_cb *axp,
struct iface *ifp,
struct ax25 *hdr,
int reverse,
const struct ax25_opts *opts)
{
	int i;

	axp->routing_changes++;
	if(reverse){
		/* hdr->nextdigi, as ntohax25() set it, is the first digi that had
		 * not repeated the frame yet - the REPEATED-bit position on the
		 * wire.  Remember it before the digis are turned round, because
		 * the inverted path gets the mirrored REPEATED marks, the way
		 * the kernel's ax25_digi_invert() derives lastrepeat - who a
		 * listen()/accept() program is to see as already-repeated is
		 * where the marks sat on the air, not where our own station
		 * happens to stand in the chain.
		 */
		int orig_next = hdr->nextdigi;

		addrcp(axp->hdr.dest,hdr->source);
		addrcp(axp->hdr.source,hdr->dest);
		for(i = 0; i < hdr->ndigis; i++)
			addrcp(axp->hdr.digis[i],hdr->digis[hdr->ndigis-1-i]);
		axp->hdr.ndigis = hdr->ndigis;
		/* Mirror of the incoming REPEATED marks: a digi the far side had
		 * already passed becomes, turned round, one that is on our way
		 * back out - so the first (ndigis - orig_next) of the inverted
		 * chain carry the has-been-repeated bit.  htonax25() stamps the
		 * wire bit afresh from nextdigi, so leaving these in the host
		 * digis only feeds what a client is shown, not the air.
		 */
		orig_next = hdr->ndigis - orig_next;
		if(orig_next < 0)
			orig_next = 0;
		for(i = 0; i < hdr->ndigis; i++){
			if(i < orig_next)
				axp->hdr.digis[i][ALEN] |= REPEATED;
			else
				axp->hdr.digis[i][ALEN] &= ~REPEATED;
		}
		axp->hdr.nextdigi = 0;
		for(i = axp->hdr.ndigis - 1; i >= 0; i--)
			if(ismyax25addr(axp->hdr.digis[i])){
				axp->hdr.nextdigi = i + 1;
				break;
			}
		axp->iface = ifp;
	} else {
		struct iface *chosen;

		ax25_resolve_path(hdr,&chosen,opts);
		axp->iface = chosen;
		axp->hdr = *hdr;
	}
	path_timing(axp);
}

/* The window this link may grow to.  Per port if the port says so, else the
 * node's setting, and which of the two numbers applies is decided by the
 * modulus the link agreed on.
 */

int ax25_maxframe(struct ax25_cb *axp)
{
	struct iface *ifp = axp->iface;
	int k;

	k = (axp->mmask == EMMASK)
		? ((ifp && ifp->emaxframe) ? ifp->emaxframe : EMaxframe)
		: ((ifp && ifp->maxframe) ? ifp->maxframe : Maxframe);
	/* Die in der XID genannte Fenstergroesse des Peers ist eine harte
	 * Obergrenze, auch wenn der Port mehr erlaubt. */
	if(axp->peer_k && k > (int)axp->peer_k)
		k = axp->peer_k;
	return k;
}

/* Take the packet length from the port if it has one, and then let the
 * driver's hard limit override both.  Without that last step "ax25 paclen
 * 1024" silences a 6pack port - sixpack_encode() drops what it cannot hold -
 * while every other port keeps working, which is the worst way to find out.
 */

void ax25_apply_iface_limits(struct ax25_cb *axp)
{
	struct iface *ifp = axp->iface;
	int room;

	axp->paclen = (ifp && ifp->paclen) ? ifp->paclen : Paclen;
	if(ifp && ifp->framemax){
		room = ifp->framemax
		     - (2 + axp->hdr.ndigis) * AXALEN   /* addresses */
		     - 2                                /* control, modulo-128 */
		     - 1                                /* PID */
		     - 2;                               /* FCS */
		if(room > 0 && axp->paclen > room)
			axp->paclen = room;
	}
	/* Und die vom Peer in der XID genannte N1 bleibt die harte Grenze,
	 * auch wenn der Port (oder der Treiber) mehr erlaubt. */
	if(axp->peer_n1 && axp->paclen > axp->peer_n1)
		axp->paclen = axp->peer_n1;
}

/* Der Peer hat in seiner XID-Antwort gesagt, was er zu empfangen bereit ist:
 * N1 in Bits (PI=6) und Fenster k in Rahmen (PI=8).  Beides sind Obergrenzen
 * fuer UNSER Senden auf genau dieser Verbindung; wir merken sie am ax25_cb
 * (peer_n1/peer_k), damit ax25_apply_iface_limits() die N1-Grenze und
 * ax25_maxframe() die Fenstergrenze einhaelt.  Nichts davon wandert in eine
 * Route: eine durch einen Digipeater gesehene XID beschreibt das ferne Ende,
 * nicht den Nexthop (xid.h).
 *
 * n1_bits==0 oder k==0 heisst "er hat dazu nichts gesagt" - dann bleibt es
 * beim Bisherigen.
 */
void ax25_apply_peer_xid(struct ax25_cb *axp,unsigned n1_bits,unsigned k)
{
	if(n1_bits){
		unsigned n1 = n1_bits / 8;     /* Bits -> Oktette */

		axp->peer_n1 = n1;
		if(n1 && axp->paclen > n1)
			axp->paclen = n1;
	}
	if(k){
		/* Mehr als die Modulusmaske hergibt, geht nicht: k=127 ist
		 * bei Modulo 8 nicht zu halten. */
		if(k > axp->mmask)
			k = axp->mmask;
		axp->peer_k = k;
		if(axp->maxframe > (int)k)
			axp->maxframe = (int)k;
	}
}

