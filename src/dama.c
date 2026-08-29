/* DAMA slave: speak only when spoken to.
 *
 * On a busy access frequency the users hear the node but not each other, so
 * they transmit on top of one another and the weak ones never get through.
 * DAMA answers that by taking the decision away from them: the node polls,
 * and a station transmits only when polled.  Detlef Schmidt, DK4EG, described
 * it in 1989 (TAPR CNC, "DAMA - A New Method of Handling Packets?"), and it
 * needs no new frame types - which is why it can be added to a working AX.25
 * without touching what is on the air for everybody else.
 *
 * THIS FILE IS THE SLAVE HALF.  The master is the larger piece and is not
 * built; see TODO.txt for what it needs that a KISS line does not give.
 *
 * TWO BITS MAKE A POLL, and mistaking one for both is the trap here:
 *
 *   the DAMA bit in the master's SSID octet   this is a DAMA channel at all.
 *                                             Active low on the wire, read
 *                                             into hdr->ext by ntohax25().
 *   command, with the poll bit set            and this frame is the one that
 *                                             hands us the channel.
 *
 * The paper is emphatic that the word "poll" does not mean the P bit - but
 * that is a remark about vocabulary, not about the wire.  Both existing
 * implementations key off exactly the pair above: TNN tests "rxfDA" and then
 * "rxfPF && rxfCR" (l2rx.c), Linux tests "type == AX25_COMMAND && pf"
 * (ax25_ds_in.c).  Two independent implementations, one rule; we follow them
 * rather than the prose.
 *
 * THE WINDOW IS THE PROCESSING OF THE POLLING FRAME, and that turned out to
 * be all the machinery needed.  lapb_input() already ends with a call to
 * lapb_output() - "see if we can send some data, perhaps piggybacking an ack"
 * - so if the gate below is open exactly while a poll is being handled,
 * everything that was waiting goes out at the right moment and nothing else
 * has to be rearranged.  Outside that window the timers fire into a closed
 * gate, which is precisely the rule the protocol asks for.
 *
 * WHAT WE DO NOT DO, and it is a deliberate departure: when the master stops
 * polling, we fall back to ordinary CSMA instead of tearing the connections
 * down.  Linux does the opposite - ax25_ds_timer.c sends DISC to every DAMA
 * connection on the device and switches DAMA off - and that costs the user
 * everything above AX.25: the mailbox login, the article he was halfway
 * through, the lot.  TNN takes our side ("sendok = 1", l2dama.c: when the
 * slave timeout expires, transmit).  Falling back is also self-repairing,
 * because an ordinary T1 then does what it has always done.
 *
 * The watchdog is lazy - a time comparison, not a timer.  Every question that
 * matters is asked from a timer that is already running, T1 above all, so a
 * timer of our own would only be a second way of asking the same thing.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "cmdparse.h"
#include "dama.h"

/* How long a master may be silent before we stop believing in him.  TNN uses
 * two minutes (damaok = 12000 in hundredths, l2rx.c) and that is where this
 * comes from; the specification gives no figure at all, so there is nothing
 * to be faithful to.  Settable per port because it is exactly the kind of
 * number that differs between one digi and the next.
 */
#define DAMA_WATCHDOG_DEFAULT   120

/*---------------------------------------------------------------------------*/

static int dama_watchdog(const struct iface *ifp)
{
	return ifp->dama_watchdog > 0 ? ifp->dama_watchdog : DAMA_WATCHDOG_DEFAULT;
}

/*---------------------------------------------------------------------------*/

/* Is DAMA in force on this port at this moment?
 *
 * Not the same question as "is it configured": a slave follows a master only
 * for as long as one is heard.  The watchdog is checked here rather than run
 * as a timer, and the state change - master lost - happens here too, so that
 * there is one place where it can be seen and counted.
 */

static int dama_in_force(struct iface *ifp)
{
	if (ifp == NULL || ifp->dama != DAMA_SLAVE || ifp->dama_heard == 0)
		return 0;
	if (secclock() - ifp->dama_heard <= dama_watchdog(ifp))
		return 1;
	ifp->dama_heard = 0;
	ifp->dama_lost++;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* A frame has arrived carrying the master's DAMA bit.  That is all it takes
 * to keep the watchdog fed - any DAMA frame, not only a poll, which is what
 * TNN does too (damaok is set before the poll is even looked at).  It is also
 * how we enter DAMA mode in the first place: nobody has to configure which
 * master, only that this port may follow one.
 */

void dama_heard_frame(struct iface *ifp, const uint8 *src)
{
	/* SLAVE, not "not off", and that is load-bearing rather than tidy:
	 * once there is a master role, a port that holds it must NOT be talked
	 * into the slave role by somebody who connects to it with the bit set.
	 * A master is a digi, and XNET names exactly this as the reason its
	 * "ds" parameter exists - "Bei Digis ist diese automatische
	 * Aktivierung des Slave-Modes nicht erwuenscht".
	 *
	 * What a master SHOULD do with a marked frame is something else and
	 * belongs in the master half: it means "this station speaks DAMA", so
	 * it may be held to the poll discipline.  Information, not a change of
	 * role.
	 */
	if (ifp == NULL || ifp->dama != DAMA_SLAVE)
		return;
	if (ifp->dama_heard == 0)
		ifp->dama_entered++;
	ifp->dama_heard = secclock();
	/* And WHO it was.  Without this any neighbour's command with the poll
	 * bit would hand us the channel, which is the opposite of the point:
	 * on a DAMA channel exactly one station decides who transmits.  The
	 * callsign is remembered rather than the bit, so a master that marks
	 * only the connect - which the paper allows - still polls recognisably
	 * afterwards.
	 */
	addrcp(ifp->dama_master, src);
}

/*---------------------------------------------------------------------------*/

/* Must this connection keep quiet right now?
 *
 * The gate.  True means "do not transmit": DAMA is in force on the port and
 * we are not inside the window that a poll opened.  Every caller is a place
 * that would otherwise have transmitted of its own accord - the send queue,
 * the delayed acknowledgement, the idle poll, the retransmission.
 */

int dama_holds(struct ax25_cb *axp)
{
	if (axp == NULL || axp->iface == NULL || axp->iface->dama_window)
		return 0;
	/* Only a link that is itself under DAMA.  A station may call us on a
	 * DAMA port without ever setting the bit - and then the paper's own
	 * rule applies, which is per CONNECTION: the mode is told at connect
	 * time and "would then remain in effect until disconnect".  Gating
	 * such a link by the port would stall a contact that has nothing to do
	 * with the master, until the watchdog let go and put it back a moment
	 * later.  The kernel keeps the same flag in the control block.
	 */
	if (!axp->dama_link)
		return 0;
	return dama_in_force(axp->iface);
}

/*---------------------------------------------------------------------------*/

/* THE PERMISSION BELONGS TO THE STATION, NOT TO THE CONNECTION, and getting
 * that wrong was the first version's real mistake.  Both references say so:
 * TNN's "sendok" is per PORT and l2dama.c rotates over the links of a station
 * with zael/indx, and its manual states the consequence - "USER mit
 * Multiconnect kommen gegenueber USERN mit nur einer Verbindung zum Knoten
 * nicht oefters an die Reihe".  One station, one turn in the cycle, its links
 * sharing it.
 *
 * A window per connection happens to look right while there is only one
 * connection on the port.  It goes wrong as soon as there are two - and it
 * goes wrong in a way that is easy to miss, because the case that exposes it
 * is DIGIPEATING THROUGH THE MASTER.  There the master is not an endpoint of
 * our connection at all, so it cannot poll "that link"; it can only give the
 * station its turn.  ntohax25() reads the DAMA bit from hdr->source and never
 * from a digipeater field, so nothing about such a frame identifies the
 * master either.
 *
 * So: the window is opened by any poll arriving on the port, and one link is
 * served per window, taken in turn.  When the master polls a particular link
 * of ours - the ordinary case - that link is the one served, and the
 * behaviour is what it was.  When it polls the station, the rotation decides,
 * and no multiconnect user takes more of the channel than a single-connection
 * one.
 *
 * The turn is an ordinal rather than a remembered control block, deliberately:
 * a pointer here would dangle the moment a link went away, and a link going
 * away is the normal end of every connection.
 */

void dama_poll_begin(struct iface *ifp, int ispoll, const uint8 *src)
{
	if (ifp == NULL)
		return;
	ifp->dama_window = 0;
	if (ispoll && dama_in_force(ifp) && addreq(src, ifp->dama_master)) {
		ifp->dama_window = 1;
		ifp->dama_polls++;
	}
}

void dama_poll_end(struct iface *ifp)
{
	if (ifp != NULL)
		ifp->dama_window = 0;
}

/*---------------------------------------------------------------------------*/

/* Our turn has come: serve our other links under the SAME CALLSIGN.
 *
 * All of them, not one in rotation - TNN's slave empties every link when the
 * gate opens (l2dama.c, "for (lnkpoi = ...) { damatx(); xmit_damail(); }")
 * and Linux does the same, walking every other connection on the device in
 * ax25_ds_enquiry_response().  Fairness between one station's several
 * connections is the master's business, exercised by how often it polls that
 * station; a slave rationing itself as well would ration a turn that was
 * already rationed.
 *
 * BUT NOT EVERY LINK ON THE PORT, and this is where both references would
 * mislead us.  What a master gives a turn to is a CALLSIGN: several links of
 * one callsign are multiconnect, which it knows about and counts as one
 * station.  Links under a DIFFERENT callsign are a different station to it,
 * and one of those transmitting on somebody else's poll is exactly the
 * unrequested transmission that gets counted and eventually disconnected on a
 * node that enforces DAMA.
 *
 * The references get away with the port because there a device carries one
 * address - Linux compares ax25o->ax25_dev and nothing else.  WAMPES answers
 * on many callsigns per interface; that is what "listen" is for.  So the
 * comparison here is the callsign we were called under, which is
 * axp->hdr.source once build_path() has turned the header round.
 */

void dama_serve_others(struct iface *ifp, struct ax25_cb *polled)
{
	struct ax25_cb *axp;

	if (ifp == NULL || polled == NULL || !ifp->dama_window)
		return;

	/* Vor den ANDEREN Verbindungen, was wartet - so steht xmit_damarl()
	 * bei TNNs Slave auch vor der Link-Schleife.  Der Grund ist derselbe:
	 * das Fenster ist die Gelegenheit, und ein UI-Rahmen, der sie
	 * verpasst, wartet wieder eine ganze Runde.
	 *
	 * Die Antwort auf den Poll selbst ist trotzdem zuerst auf der
	 * Leitung: die schickt die Zustandsmaschine, bevor lapb_input() hier
	 * unten ankommt.  Gemessen sieht ein Master also RR, dann UI, dann
	 * die uebrigen Verbindungen - alles in einem Fenster, und das ist,
	 * worauf es ankommt.
	 */
	dama_ui_flush(ifp);

	for (axp = Ax25_cb; axp != NULL; axp = axp->next)
		if (axp != polled && axp->iface == ifp &&
		    addreq(axp->hdr.source, polled->hdr.source) &&
		    (axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY))
			lapb_output(axp);
}

/* UI IN DAS FENSTER LEGEN, WO EINES KOMMT - und nur dort.
 *
 * DAMA regelt verbundene Verbindungen.  Es VERBIETET nicht-DAMA nicht, es
 * empfiehlt DAMA, und die Spezifikation nimmt Datagramme ausdruecklich aus
 * der Poll-Disziplin heraus: "UI-frames originated by the node are no problem
 * since all stations receive these frames" (doc/DAMA-SLAVE.md).  An dieser
 * Entscheidung aendert sich nichts.
 *
 * Was sich aendert, ist eine Gelegenheit (Thomas): haben wir eine Verbindung
 * und werden gepollt, dann sendet der Knoten in diesem Augenblick ohnehin,
 * der Kanal gehoert ihm, und ein UI-Rahmen kostet dort NICHTS.  Genau der
 * Verlust, den die Spezifikation im selben Absatz einraeumt - "the rare
 * UI-frames will reduce the throughput to the CSMA value" - faellt damit weg.
 * Also: kurz warten, ob ein Fenster kommt, und sonst senden wie bisher.
 *
 * GEWARTET WIRD AUF DEN POLL, nicht auf eine Uhr.  Eine kurze eigene Frist
 * war der erste Entwurf und war falsch (Thomas): ist ein Master in Kraft,
 * dann haben wir eine Verbindung - dama_in_force() speist sich aus
 * lapb_input(), also aus Rahmen, die bei uns ENDEN -, und dann werden wir
 * auch gepollt.  Auf einem 1k2-Kanal liegt der Poll-Abstand aber leicht
 * ueber zwei Sekunden; eine Frist in dieser Groessenordnung haette fast
 * immer zuerst zugeschlagen, und die Sache waere ins Leere gelaufen.
 *
 * Der Notausgang ist deshalb der WACHHUND, und nicht eine zweite Zahl:
 * verstummt der Master, faellt dama_in_force(), es kommt kein Fenster mehr,
 * und dann muss die Schlange leer.  Der Zeitgeber laeuft genau so lange -
 * "ifconfig <iface> damatimeout" stellt beides zugleich.
 *
 * NET/ROM GEHT SOFORT: ein Nodes-Rundspruch sind viele Rahmen auf einmal,
 * und Routing-Information, deren Zeitpunkt ihre Bedeutung ist, soll nicht
 * auf eine Zeitscheibe warten.  ARP steht hier NICHT mehr - das ist an der
 * Quelle geloest, "dama slave" schaltet das Fragen auf dem Port ab
 * (iface->noarp), und was nicht entsteht, muss auch nicht gehalten werden.
 *
 * Der Rest - Baken, APRS, IP ueber UI, was am Dienstsocket als Datagramm
 * hereinkommt - wartet auf das Fenster.
 */

#define DAMA_UI_MAX     8               /* mehr wird nicht gestapelt */

struct dama_ui {
	struct dama_ui *next;
	struct iface *ifp;
	struct mbuf *q;
	int n;
	struct timer t;
};

static struct dama_ui *Dama_ui;

static void dama_ui_expire(void *arg);

static struct dama_ui *dama_ui_port(struct iface *ifp, int create)
{
	struct dama_ui *up;

	for (up = Dama_ui; up; up = up->next)
		if (up->ifp == ifp)
			return up;
	if (!create)
		return NULL;
	up = (struct dama_ui *) callocw(1, sizeof(struct dama_ui));
	up->ifp = ifp;
	up->next = Dama_ui;
	up->t.func = dama_ui_expire;
	up->t.arg = up;
	Dama_ui = up;
	return up;
}

/*---------------------------------------------------------------------------*/

/* Alles hinaus, was wartet.  Aus dem Fenster gerufen und aus der Frist, und
 * beide Male ist es dasselbe: die Rahmen sind fertig, sie gehen an den
 * Treiber.
 */

void dama_ui_flush(struct iface *ifp)
{
	struct dama_ui *up;
	struct mbuf *bp;

	if (ifp == NULL || (up = dama_ui_port(ifp, 0)) == NULL)
		return;
	stop_timer(&up->t);
	while ((bp = dequeue(&up->q)) != NULL)
		(*ifp->raw)(ifp, &bp);
	up->n = 0;
}

static void dama_ui_expire(void *arg)
{
	struct dama_ui *up = (struct dama_ui *) arg;

	dama_ui_flush(up->ifp);
}

/*---------------------------------------------------------------------------*/

/* Der PID eines fertigen UI-Rahmens, oder -1.
 *
 * Das Adressfeld wird von Hand abgeschritten: der Rahmen ist schon gebaut,
 * und ihn dafuer wieder auseinanderzunehmen waere teurer als die paar
 * Schritte.  Nur ein UI-Rahmen kommt ueberhaupt in Frage - alles andere geht
 * durch lapb und ist dort schon im Fenster.
 */

static int dama_ui_pid(struct mbuf *bp)
{
	int n;
	int off;
	uint8 buf[AXALEN * (MAXDIGIS + 2) + 2];

	struct mbuf *p;

	/* Der Kopf kann ueber mehrere mbufs verteilt sein, also abschreiten
	 * statt indizieren - und ohne den Rahmen anzufassen, er soll ja noch
	 * gesendet werden.
	 */
	for (n = 0, p = bp; p != NULL && n < (int) sizeof(buf); p = p->next) {
		int k = (int) p->cnt;

		if (k > (int) sizeof(buf) - n)
			k = (int) sizeof(buf) - n;
		memcpy(buf + n, p->data, (size_t) k);
		n += k;
	}
	if (n < AXALEN * 2 + 2)
		return -1;
	for (off = 0; off + AXALEN <= n; off += AXALEN)
		if (buf[off + ALEN] & E) {
			off += AXALEN;
			break;
		}
	if (off + 1 >= n)
		return -1;
	if ((buf[off] & ~PF) != UI)
		return -1;
	return buf[off + 1];
}

/*---------------------------------------------------------------------------*/

/* 1: uebernommen, der Aufrufer ist den Rahmen los.  0: er sendet selbst. */

int dama_defer_ui(struct iface *ifp, struct mbuf **bpp)
{
	int pid;
	struct dama_ui *up;

	if (ifp == NULL || bpp == NULL || *bpp == NULL || ifp->raw == NULL)
		return 0;
	/* Kein DAMA-Kanal, oder kein Master in Kraft: es kommt kein Fenster,
	 * auf das zu warten waere.
	 */
	if (!dama_in_force(ifp))
		return 0;
	/* Das Fenster ist gerade offen - dann ist Warten sinnlos, der Rahmen
	 * geht sofort und richtig.
	 */
	if (ifp->dama_window)
		return 0;
	if ((pid = dama_ui_pid(*bpp)) < 0)
		return 0;
	if (pid == PID_NETROM)
		return 0;
	up = dama_ui_port(ifp, 1);
	if (up->n >= DAMA_UI_MAX)
		return 0;
	if (!up->n) {
		set_timer(&up->t, dama_watchdog(ifp) * 1000L);
		start_timer(&up->t);
	}
	enqueue(&up->q, bpp);
	up->n++;
	return 1;
}

/*---------------------------------------------------------------------------*/

/* Announce that WE speak DAMA, by setting the bit in our own source address.
 *
 * Thomas' reading, and it is the one that makes digipeating work at all: when
 * a user connects to DL1AAA THROUGH the master rather than connecting to the
 * master and working onwards, the master is only a digipeater for that link
 * and has nothing that tells it we are a DAMA station.  Marking our own SABM
 * tells it, and a master that tracks every connection hop by hop - which is
 * what WAMPES does, and what its master half will do - can then put us in its
 * polling list.
 *
 * Marked on a port the sysop has declared a DAMA channel, not on one where a
 * master merely happens to have been heard.  That is the difference between
 * "this channel works this way" and "somebody out there does", and only the
 * first is a statement we are entitled to make about ourselves.
 */

void dama_mark(struct ax25_cb *axp)
{
	if (axp == NULL || axp->iface == NULL)
		return;
	if (axp->iface->dama == DAMA_SLAVE)
		axp->hdr.ext |= SSID_DAMA;
	else
		axp->hdr.ext &= ~SSID_DAMA;
}

/*---------------------------------------------------------------------------*/

void dama_wait(struct ax25_cb *axp)
{
	if (axp != NULL && !run_timer(&axp->t1))
		start_timer(&axp->t1);
}

/*---------------------------------------------------------------------------*/

/*      ifconfig <if> dama off | slave
 *      ifconfig <if> damatimeout <seconds>
 *
 * Two keys rather than one with an optional second word, because ifconfig
 * reads its line as key/value PAIRS - "for(i=2;i<argc-1;i+=2)" in iface.c -
 * and a third word would silently become the next key.  Found by writing
 * "dama slave 5" and watching the 5 go nowhere.
 *
 * Master and master-enforce are named here rather than left out, so that the
 * refusal says what is missing instead of "unknown".
 */

int ifdama(int argc, char *argv[], void *p)
{
	struct iface *ifp = (struct iface *) p;

	if (!strcmp(argv[1], "off")) {
		ifp->dama = DAMA_OFF;
		ifp->dama_heard = 0;
		return 0;
	}
	if (!strcmp(argv[1], "master") || !strcmp(argv[1], "master-enforce")) {
		printf("Only the slave side is built - see TODO.txt\n");
		return 1;
	}
	if (strcmp(argv[1], "slave")) {
		printf("Dama must be off or slave\n");
		return 1;
	}
	ifp->dama = DAMA_SLAVE;
	/* Not "heard" yet: being told to follow a master is not the same as
	 * having found one, and until one is found nothing may hold back what
	 * this port sends.
	 */
	ifp->dama_heard = 0;
	/* UND DAS FRAGEN NACH ARP GEHT AUS (Thomas).  Eine ARP-Anfrage ist ein
	 * Rundspruch an QST; auf einem DAMA-Kanal kostet das den Kanal, und
	 * wer dort IP im Datagramm-Modus faehrt, hat seinen Partner ohnehin
	 * eingetragen - dann entsteht sie erst gar nicht.  Gesagt wird es,
	 * weil eine Nebenwirkung, die niemand sieht, die schlechtere Art ist,
	 * Voreinstellungen zu treffen; und zurueckgenommen ist sie mit einer
	 * Zeile.  Antworten tun wir weiter.
	 */
	if (!ifp->noarp) {
		ifp->noarp = 1;
		printf("%s: arp requests switched off - a request is a "
		       "broadcast to QST, and it\n"
		       "  costs the channel; a partner that is configured "
		       "needs none.  \"ifconfig %s\n"
		       "  arp on\" puts it back.  Incoming requests are "
		       "answered either way.\n", ifp->name, ifp->name);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

int ifdamatimeout(int argc, char *argv[], void *p)
{
	struct iface *ifp = (struct iface *) p;
	int n = atoi(argv[1]);

	if (n < 1 || n > 3600) {
		printf("Dama timeout must be 1..3600 seconds\n");
		return 1;
	}
	ifp->dama_watchdog = n;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* What "ifconfig <if>" adds about DAMA.  Silent on a port that has none, so
 * that the display of an ordinary node does not grow a line about something
 * it is not doing.
 */

void dama_show(struct iface *ifp)
{
	if (ifp->dama != DAMA_SLAVE)
		return;
	printf("           dama slave, timeout %ds, arp %s, master ",
	       dama_watchdog(ifp), ifp->noarp ? "off" : "on");
	if (dama_in_force(ifp)) {
		char buf[AXBUF];
		printf("%s", pax25(buf, ifp->dama_master));
	} else
		printf("not heard");
	printf(", found %ld lost %ld polls %ld\n",
	       (long) ifp->dama_entered, (long) ifp->dama_lost,
	       (long) ifp->dama_polls);
}
