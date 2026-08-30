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
 * BEIDE HAELFTEN STEHEN HIER.  Der Slave ist der obere Teil der Datei, der
 * Master der untere; sie teilen sich das Bit, die Rollen und den Zaehler,
 * und das ist der Grund, warum sie beieinander stehen.
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
 * that is a remark about vocabulary, not about the wire.
 *
 * BERICHTIGT AM 2026-08-30, und die Berichtigung schwaecht die Begruendung:
 * hier stand, ZWEI unabhaengige Implementierungen pruefen genau dieses Paar -
 * TNN mit "rxfDA" und "rxfPF && rxfCR", Linux mit "AX25_COMMAND && pf".  Die
 * TNN-Haelfte des Belegs steht aber in l2rx.c INNERHALB von "#ifdef
 * DAMASLAVE", und in include/all.h ist genau dieses DAMASLAVE auskommentiert
 * - der Slave ist also gar nicht uebersetzt.  Ein normal gebautes TNN liest das Bit zwar
 * (rxfDA), loescht es sofort wieder aus dem Kopf und benutzt es nie; seine
 * Master-Seite haengt rein an der Konfiguration (dama(port) = l2mode &
 * MODE_a).
 *
 * Es bleibt also EINE Implementierung - Linux - plus ein abgeschaltetes
 * Fragment.  Die Regel selbst hat sich im Betrieb bewaehrt und bleibt; nur
 * ist sie schwaecher belegt, als hier stand.  Und die Spezifikation selbst
 * (CNC 1989, S. 208) sagt zum Bit nur, es sei "the dormant bit 5 of THE
 * MASTER'S SSID address field", auf 0 gesetzt, um dem TNC des Nutzers zu
 * sagen, er moege DAMA einschalten - einmal beim Aufbau, gueltig bis zum
 * Disconnect.
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
#include "devparam.h"
#include "dama.h"

/* How long a master may be silent before we stop believing in him.  TNN uses
 * two minutes (damaok = 12000 in hundredths, l2rx.c) and that is where this
 * comes from; the specification gives no figure at all, so there is nothing
 * to be faithful to.  Settable per port because it is exactly the kind of
 * number that differs between one digi and the next.
 */
#define DAMA_WATCHDOG_DEFAULT   120

/*---------------------------------------------------------------------------*/

void dama_ui_flush(struct iface *ifp);   /* weiter unten, siehe dort */

/* WIE LANGE EIN MASTER SCHWEIGEN DARF, bevor wir ihm nicht mehr folgen.
 *
 * Was der Sysop sagt, gilt.  Sonst rechnen wir es aus der Bitrate, und der
 * Anker dafuer steht im Papier (CNC 1989, S. 207): der Abstand zweier Polls
 * einer Station ist "usually more than 30 seconds at 1200 baud".  Das ist
 * die Umlaufzeit ueber alle Stationen, und sie skaliert mit der Bitrate -
 * auf 9600 ist dieselbe Runde in gut dreieinhalb Sekunden herum.
 *
 * Viermal die Umlaufzeit als Frist: einen Poll zu verpassen ist normal,
 * zwei kann eine Kollision erklaeren, bei vier ist er weg.  Bei 1200 Baud
 * kommen daraus genau die 120 Sekunden heraus, die vorher als feste
 * Vorgabe dastanden - die Formel gibt also den bisherigen Wert wieder und
 * macht ihn nur fuer schnellere Kanaele richtig.
 *
 * Ohne hf-datarate bleibt es bei der festen Vorgabe.
 */

static int dama_watchdog(const struct iface *ifp)
{
	if (ifp->dama_watchdog > 0)
		return ifp->dama_watchdog;
	if (ifp->hf_datarate > 0) {
		long t = 4L * 36000L / ifp->hf_datarate;   /* 30 s bei 1200 */

		return (int) (t < 5 ? 5 : t);
	}
	return DAMA_WATCHDOG_DEFAULT;
}

/*---------------------------------------------------------------------------*/

/* DEM KANALZUGRIFF SAGEN, DASS ER NICHT MEHR WUERFELN MUSS - ABER WEITER
 * HINHOEREN.
 *
 * Wer gepollt wurde, hat die Erlaubnis und soll SOFORT tasten.  Laeuft
 * dagegen die uebliche p-Persistenz, kostet das bei unseren eigenen
 * Vorgaben (persist 63, slottime 10) im Mittel vier Slots, also gut 400 ms,
 * mit langem Schwanz - und die Spezifikation gibt dem Master nur "around
 * 1/2 second" Geduld (CNC 1989, S. 204).  Wir kaemen also regelmaessig zu
 * spaet, ohne dass irgendetwas kaputt aussieht.
 *
 * PERSISTENCE UND SLOTTIME, NICHT VOLLDUPLEX, und das war mein Fehler im
 * ersten Entwurf (Thomas hat ihn zerlegt):
 *
 *   - fulldup heisst "sende, egal was das DCD sagt - die Frequenz gehoert
 *     mir".  Erdacht wurde es fuer DUPLEX-EINSTIEGE mit zwei Frequenzen,
 *     wo der Master immer hoert und nie uebersprochen wird.  DAMA wird
 *     aber vor allem auf SIMPLEX gesprochen, also auf einer geteilten
 *     Frequenz.
 *   - Es ist ausserdem eine PORTEIGENSCHAFT und kein Zustand fuer die
 *     Dauer eines Polls.  Setzen wir sie, gilt sie auch fuer den SABM, den
 *     ein Slave in CSMA hinauslegt - und genau dort muss er hinhoeren,
 *     sonst uebersprich er den gerade Sendenden, womoeglich den Master.
 *   - Der MASTER darf sie erst recht nicht setzen: er muss am DCD hoeren,
 *     ob der Gepollte noch sendet, und darf ihn nicht abhacken.
 *
 * persist=255 mit slottime=0 laesst das DCD in Kraft und nimmt nur das
 * Wuerfeln weg: sobald der Kanal frei ist, wird getastet.  Genau das tut
 * TNN (l2misc.c: P=255 auf einem DAMA-Port gegen 160 auf einem
 * gewoehnlichen Einstieg, und autopers() setzt zusaetzlich slottime 0).
 * Und fullduplex(port) ist dort eine L1-Eigenschaft, die der DAMA-Code nur
 * LIEST und nie setzt - die Anlage beschreibt sie, nicht das Verfahren.
 *
 * EIN WEG, DREI PORTTYPEN: kiss.c schickt die Parameter an den TNC,
 * sixpack.c setzt seine eigenen (dort macht den Kanalzugriff der HOST),
 * krnlif.c sagt es dem Kernel.  Auf axip und bpqether laeuft es ins Leere,
 * und das ist richtig - es gibt kein CSMA, auf das man verzichten koennte.
 *
 * ZURUECKGENOMMEN WIRD NUR, WAS WIR SELBST GESETZT HABEN.
 */

static void dama_channel_access(struct iface *ifp, int on)
{
	if (ifp == NULL || ifp->ioctl == NULL)
		return;
	if (on) {
		int32 p, w;

		if (ifp->dama_ca_set)
			return;                 /* laeuft schon */
		p = (*ifp->ioctl)(ifp, PARAM_PERSIST, 0, 0);
		w = (*ifp->ioctl)(ifp, PARAM_SLOTTIME, 0, 0);
		if (p < 0 && w < 0)
			return;                 /* Port kennt beides nicht */
		ifp->dama_persist_save = p;
		ifp->dama_slot_save = w;
		(*ifp->ioctl)(ifp, PARAM_PERSIST, 1, 255);
		(*ifp->ioctl)(ifp, PARAM_SLOTTIME, 1, 0);
		ifp->dama_ca_set = 1;
	} else if (ifp->dama_ca_set) {
		if (ifp->dama_persist_save >= 0)
			(*ifp->ioctl)(ifp, PARAM_PERSIST, 1,
				      ifp->dama_persist_save);
		if (ifp->dama_slot_save >= 0)
			(*ifp->ioctl)(ifp, PARAM_SLOTTIME, 1,
				      ifp->dama_slot_save);
		ifp->dama_ca_set = 0;
	}
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
	dama_channel_access(ifp, 0);    /* wieder CSMA, also wieder wuerfeln */
	/* HIER, und nicht erst wenn ein Zeitgeber es merkt: das ist der
	 * Augenblick, in dem feststeht, dass kein Fenster mehr kommt.  Was
	 * gewartet hat, hat ab jetzt nichts mehr, worauf es warten koennte
	 * (Thomas).  Ohne das haette ein Rahmen, der kurz vor dem Verstummen
	 * des Masters eingereiht wurde, bis zu einer ZWEITEN Wachhundzeit
	 * gelegen - die seine begann ja erst mit dem Einreihen.
	 */
	dama_ui_flush(ifp);
	return 0;
}

/*---------------------------------------------------------------------------*/

/* WER DIESEN RAHMEN AUF UNSEREM KANAL GESENDET HAT.
 *
 * Nicht die Quelle: kommt er ueber Digipeater, dann hat der LETZTE, der ihn
 * schon wiederholt hat, zuletzt getastet - die Quelle sitzt womoeglich weit
 * weg und war nie auf dieser Frequenz.  Das ist dieselbe Unterscheidung, die
 * dama_station() fuer die Gegenrichtung trifft, nur von der anderen Seite:
 * dort der erste NOCH NICHT wiederholte Digi (wen wir als naechstes ansprechen),
 * hier der letzte SCHON wiederholte (wer uns zuletzt erreicht hat).
 *
 * nextdigi ist der Index des ersten Digis, der noch nicht wiederholt hat -
 * der davor ist also der gesuchte.
 */

static const uint8 *dama_sender(const struct ax25 *hdr)
{
	if (hdr->ndigis > 0 && hdr->nextdigi > 0)
		return hdr->digis[hdr->nextdigi - 1];
	return hdr->source;
}

/*---------------------------------------------------------------------------*/

/* A frame has arrived carrying the master's DAMA bit.  That is all it takes
 * to keep the watchdog fed - any DAMA frame, not only a poll, which is what
 * TNN does too (damaok is set before the poll is even looked at).  It is also
 * how we enter DAMA mode in the first place: nobody has to configure which
 * master, only that this port may follow one.
 */

void dama_heard_frame(struct iface *ifp, const struct ax25 *hdr)
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
	if (ifp->dama_heard == 0) {
		ifp->dama_entered++;
		dama_channel_access(ifp, 1);
	}
	ifp->dama_heard = secclock();
	/* UND WER GESENDET HAT - nicht wer den Rahmen verfasst hat.  Ohne das
	 * wuerde uns jedes Kommando mit P von irgendwem den Kanal in die Hand
	 * druecken, was das Gegenteil des Verfahrens waere.  Gemerkt wird das
	 * Rufzeichen und nicht das Bit, damit ein Master, der nur den Aufbau
	 * markiert - was die Spezifikation erlaubt -, danach trotzdem
	 * erkennbar pollt.
	 *
	 * Es heisst SENDER und nicht MASTER, und das ist keine Wortklauberei:
	 * wer der Master ist, sagt uns die Leitung nicht.  Das hier ist, wer
	 * uns zuletzt erreicht hat - eine Groesse, die es gibt.
	 */
	addrcp(ifp->dama_sender, dama_sender(hdr));
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

void dama_poll_begin(struct iface *ifp, int ispoll, const struct ax25 *hdr)
{
	if (ifp == NULL)
		return;
	ifp->dama_window = 0;
	if (ispoll && dama_in_force(ifp) &&
	    addreq(dama_sender(hdr), ifp->dama_sender)) {
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
 * auch gepollt.  Wie lange das dauert, sagt der Kanal: ein Master pollt
 * seine Stationen reihum, und bei sechzehn Stationen zu je zwei bis fuenf
 * Sekunden ist man erst nach einer Minute wieder dran (Thomas).  Eine Frist
 * von Sekunden haette also fast immer zuerst zugeschlagen und die Sache
 * waere ins Leere gelaufen.  Fuer eine Bake ist eine Minute der richtige
 * Preis - fuer ARP und NET/ROM waere er der falsche, und genau deshalb
 * warten die nicht mit.
 *
 * Der Notausgang ist deshalb der WACHHUND und keine zweite Zahl: verstummt
 * der Master, faellt dama_in_force(), es kommt kein Fenster mehr - und DORT
 * wird geleert, im selben Augenblick, in dem das feststeht.  Der Zeitgeber
 * daneben laeuft dieselbe Zeit und ist nur der Rueckhalt fuer einen Port,
 * den ueberhaupt niemand mehr fragt.  "ifconfig <iface> damatimeout" stellt
 * beides zugleich, und das ist richtig so: es ist derselbe Sachverhalt.
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
	/* Beide Rollen markieren, und aus demselben Grund: das Bit sagt "diese
	 * Station spricht DAMA".  Beim Master ist es sogar das, WORAN ein
	 * Slave den Kanal erkennt - ohne es wuerde ihn niemand als Master
	 * annehmen.
	 */
	if (axp->iface->dama != DAMA_OFF)
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
/*                          DER MASTER
 *---------------------------------------------------------------------------*/

/* Reihum, EINE STATION je Zug - und Station heisst hier nicht Verbindung.
 *
 * Wer auf unserem Kanal wirklich sendet, ist der letzte Digipeater, der noch
 * aussteht, sonst die Gegenstelle selbst.  TNN fuehrt dafuer eigens einen
 * Zeiger, mit Kommentar (l2misc.c): "das Rufzeichen, das in Wirklichkeit der
 * Ansprechpartner dieses Linkes ist ... das Erste Rufzeichen im via-Feld ohne
 * H-Bit oder das Ziel-Rufzeichen selbst", und multiconn() gruppiert danach.
 * Mehrere Verbindungen einer Station sind so EINE Station im Umlauf, und
 * mehrere Benutzer hinter einem Digi ebenfalls - was auch das einzig
 * Sinnvolle ist: gepollt wird, wer sendet.
 */

static const uint8 *dama_station(const struct ax25_cb *axp)
{
	if (axp->hdr.ndigis > 0 && axp->hdr.nextdigi < axp->hdr.ndigis)
		return axp->hdr.digis[axp->hdr.nextdigi];
	return axp->hdr.dest;
}

/*---------------------------------------------------------------------------*/

/* WIE LANGE EINE STATION DEN KANAL HAT, wenn sie nicht von selbst fertig
 * wird.  TNN nimmt dafuer den Traeger (dama_dcd(), "DAMA nur weiter, wenn
 * letztes Frame gesendet wurde"), und den haben wir nicht: auf axip, auf
 * bpqether und auf den meisten KISS-Leitungen weiss niemand, ob gerade
 * jemand sendet.  Was wir haben, ist die Antwort selbst - der Gepollte
 * schliesst seinen Zug mit einem Rahmen, der das F-Bit traegt.  Bleibt der
 * aus, weil die Station weg ist oder der Rahmen verloren ging, muss die
 * Runde trotzdem weiterlaufen, und dafuer ist diese Frist da.
 *
 * Fuenf Sekunden: auf 1k2 ist ein volles Fenster von sieben Rahmen zu 256
 * Byte gut fuenfzehn Sekunden, ein einzelner RR ein Zwanzigstel davon.  Die
 * Frist soll den Zug nicht abschneiden, sondern eine STUMME Station
 * ueberspringen - deshalb naeher am Rahmen als am Fenster.
 */
#define DAMA_SLOT_DEFAULT       5000L   /* ms */

/* UND EIN MINDESTABSTAND ZWISCHEN ZWEI POLLS, ohne den die Runde rast.
 *
 * Gemessen, als der erste echte Slave gegenueberstand: er beantwortet den
 * Poll, das beendet den Zug, der naechste begann sofort - 221794 Polls in
 * 25 Sekunden, bei EINER Station.  Mit mehreren waere es dasselbe, nur
 * abwechselnd.
 *
 * TNN fuehrt dafuer dama_init, Vorgabe 100 in Einheiten von 10 ms, also eine
 * Sekunde (config.c; einstellbar bis 1000, das waeren zehn).  Dieselbe Zahl
 * hier, und aus demselben Grund: der Abstand ist es, der aus "so schnell wie
 * die Leitung kann" eine Runde macht - und er laesst dem Kanal Luft fuer
 * das, was NICHT gepollt wird, den Verbindungsaufbau naemlich, der nach der
 * Spezifikation in CSMA laeuft.
 */
#define DAMA_GAP_DEFAULT        1000L   /* ms */

struct dama_m {
	struct dama_m *next;
	struct iface *ifp;
	uint8 turn[AXALEN];             /* wer gerade dran ist */
	int busy;                       /* 0: zwischen zwei Zuegen */
	int answered;                   /* der Gepollte hat sich gemeldet */
	struct timer t;
};

static struct dama_m *Dama_m;

static void dama_master_next(void *arg);
static void dama_kick_marked(struct iface *ifp);

static struct dama_m *dama_m_port(struct iface *ifp, int create)
{
	struct dama_m *mp;

	for (mp = Dama_m; mp; mp = mp->next)
		if (mp->ifp == ifp)
			return mp;
	if (!create)
		return NULL;
	mp = (struct dama_m *) callocw(1, sizeof(struct dama_m));
	mp->ifp = ifp;
	mp->t.func = dama_master_next;
	mp->t.arg = mp;
	mp->next = Dama_m;
	Dama_m = mp;
	return mp;
}

/*---------------------------------------------------------------------------*/

/* Die naechste Station nach <after> auf diesem Port, reihum.
 *
 * Gesucht wird ueber die Verbindungen, nicht ueber eine eigene Liste: eine
 * zweite Buchhaltung waere eine zweite Gelegenheit, mit der ersten
 * auseinanderzulaufen, und die Verbindungen sind ohnehin das, was der Master
 * bedient.  <after> leer heisst "fang vorne an".
 */

static int dama_next_station(struct iface *ifp, const uint8 *after, uint8 *out)
{
	const uint8 *first = NULL;
	const uint8 *next = NULL;
	int seen_after = 0;
	struct ax25_cb *axp;

	for (axp = Ax25_cb; axp != NULL; axp = axp->next) {
		const uint8 *st;

		if (axp->iface != ifp || axp->peer != NULL)
			continue;
		if (axp->state != LAPB_CONNECTED && axp->state != LAPB_RECOVERY)
			continue;
		st = dama_station(axp);
		if (first == NULL)
			first = st;
		if (after == NULL || after[0] == '\0') {
			next = st;
			break;
		}
		if (seen_after && !addreq(st, after)) {
			next = st;
			break;
		}
		if (addreq(st, after))
			seen_after = 1;
	}
	if (next == NULL)
		next = first;           /* einmal herum, oder nur einer */
	if (next == NULL)
		return 0;               /* niemand da */
	addrcp(out, next);
	return 1;
}

/*---------------------------------------------------------------------------*/

/* Der Zug: alles ausgeben, was fuer diese Station wartet, und ihn mit einem
 * KOMMANDO MIT P-BIT schliessen - das ist es, was den Kanal uebergibt.
 *
 * Das P kommt als eigenes RR und nicht auf dem letzten I-Rahmen, obwohl das
 * einen Rahmen sparen wuerde: lapb_output() sendet I-Rahmen ohne PF und
 * weiss nicht, welcher der letzte des Zuges ist.  Erst wenn der Master
 * laeuft, lohnt es, das dort hineinzureichen - vorher waere es eine Aenderung
 * am Sendeweg fuer eine Ersparnis, die noch niemand gemessen hat.
 */

/* WIE LANGE WIR AUF DIE ERSTE ANTWORT WARTEN - und das ist NICHT die Laenge
 * des Zuges.  Die Spezifikation trennt beides (CNC 1989, S. 204):
 *
 *   "if the user does not respond within a given time frame (say around
 *    1/2 second) then the master assumes the poll got clobbered"
 *   "Once permission is granted several frames might be transmitted in a
 *    block."
 *
 * Bis hierher hatten wir EINEN Zeitgeber fuer beides, und der stand auf der
 * groesseren Zahl - eine stumme Station kostete uns fuenf Sekunden, wo eine
 * halbe genuegt haette.
 *
 * WORAUS DIE FRIST BESTEHT: TX-Delay des Gepollten, ein kurzer Rahmen, und
 * etwas Luft.  Nur der mittlere Teil haengt an der Bitrate - das TX-Delay
 * ist die Einschwingzeit des Senders und wird bei 9600 nicht kuerzer.  Bei
 * 1200 Baud kommt damit 250 + 133 + 100 = 483 ms heraus, also genau die
 * "around 1/2 second" des Papiers; bei 9600 sind es 367 ms.
 *
 * OHNE hf-datarate bleibt es bei der alten Zeitscheibe.  Raten waere hier
 * schlechter als das bisherige Verhalten.
 */

static int32 dama_answer_time(struct iface *ifp)
{
	int32 txd = 250;                /* ms, wenn der Port nichts sagt */
	int32 v;

	if (ifp->hf_datarate <= 0)
		return DAMA_SLOT_DEFAULT;
	if (ifp->ioctl != NULL &&
	    (v = (*ifp->ioctl)(ifp, PARAM_TXDELAY, 0, 0)) > 0)
		txd = v * 10;           /* KISS zaehlt in 10 ms */
	return txd + (20 * 8 * 1000) / ifp->hf_datarate + 100;
}

/*---------------------------------------------------------------------------*/

/* WIE LANGE EINE STATION DEN KANAL HALTEN DARF, wenn sie einmal angefangen
 * hat.  Nicht dieselbe Frage wie die Antwortfrist darueber: dort geht es
 * darum, ob sie ueberhaupt anfaengt, hier darum, wann genug ist.
 *
 * Die Antwort steht im Fenster: mehr als maxframe Rahmen zu paclen Oktetten
 * darf sie in einem Zug ohnehin nicht senden.  Bei 1200 Baud mit sieben mal
 * 256 sind das rund 13 Sekunden - und genau diese Groesse nannte schon der
 * Kommentar zur alten festen Zeitscheibe, die deshalb ABSICHTLICH kuerzer
 * war: sie musste damals auch die stumme Station ueberspringen.  Diese
 * Aufgabe hat jetzt die Antwortfrist, also darf die Zugdauer sein, was sie
 * sein soll.
 *
 * Ohne hf-datarate bleibt es bei der alten Zahl.
 */

static int32 dama_turn_time(const struct iface *ifp)
{
	long bits;
	int mf = ifp->maxframe > 0 ? ifp->maxframe : 7;
	int pl = ifp->paclen > 0 ? ifp->paclen : 256;

	if (ifp->hf_datarate <= 0)
		return DAMA_SLOT_DEFAULT;
	bits = (long) mf * (pl + 20) * 8;
	return (int32) (bits * 1000L / ifp->hf_datarate + 500);
}

/*---------------------------------------------------------------------------*/

static void dama_master_turn(struct dama_m *mp)
{
	struct ax25_cb *axp;
	struct ax25_cb *last = NULL;

	for (axp = Ax25_cb; axp != NULL; axp = axp->next) {
		if (axp->iface != mp->ifp || axp->peer != NULL)
			continue;
		if (axp->state != LAPB_CONNECTED && axp->state != LAPB_RECOVERY)
			continue;
		if (!addreq(dama_station(axp), mp->turn))
			continue;
		lapb_output(axp);
		last = axp;
	}
	if (last == NULL) {             /* zwischendurch weggegangen */
		mp->busy = 0;
		return;
	}
	sendctl(last, LAPB_COMMAND, RR | PF);
	mp->ifp->dama_polls++;
	mp->busy = 1;
	mp->answered = 0;
	set_timer(&mp->t, dama_answer_time(mp->ifp));
	start_timer(&mp->t);
}

/*---------------------------------------------------------------------------*/

/* Weiter zur naechsten Station.  Gerufen, wenn ein Zug zu Ende ist - durch
 * das F-Bit des Gepollten oder durch die Frist.
 */

/* Der Zug ist zu Ende - durch das F-Bit des Gepollten oder durch die
 * Zeitscheibe.  Jetzt die Pause, nicht sofort der naechste Poll.
 */

static void dama_master_gap(struct dama_m *mp)
{
	stop_timer(&mp->t);
	mp->busy = 0;
	set_timer(&mp->t, DAMA_GAP_DEFAULT);
	start_timer(&mp->t);
}

/*---------------------------------------------------------------------------*/

/* Der Zeitgeber, und er hat zwei Bedeutungen - welche, sagt mp->busy:
 * laeuft ein Zug, ist die Zeitscheibe abgelaufen (der Gepollte schweigt);
 * laeuft keiner, ist die Pause vorbei und die naechste Station ist dran.
 */

static void dama_master_next(void *arg)
{
	struct dama_m *mp = (struct dama_m *) arg;
	uint8 who[AXALEN];

	dama_kick_marked(mp->ifp);      /* hier ist kein Rahmen in Arbeit */
	stop_timer(&mp->t);
	if (mp->ifp->dama != DAMA_MASTER) {
		mp->busy = 0;
		return;
	}
	if (mp->busy) {
		/* Zwei Bedeutungen, je nachdem: hat er sich nie gemeldet, war
		 * es die Antwortfrist - der Poll ging verloren oder die
		 * Station ist weg.  Hat er sich gemeldet, war es die
		 * Zugdauer, und der Block war zu lang.  Beides endet den Zug.
		 */
		dama_master_gap(mp);
		return;
	}
	if (!dama_next_station(mp->ifp, mp->turn, who)) {
		memset(mp->turn, 0, AXALEN);  /* niemand verbunden: Runde ruht */
		return;
	}
	addrcp(mp->turn, who);
	dama_master_turn(mp);
}

/*---------------------------------------------------------------------------*/

/* Jeder Rahmen, der auf einem Master-Port bei uns endet.
 *
 * Zwei Dinge geschehen hier.  Erstens endet der Zug, wenn der Gepollte mit
 * gesetztem F antwortet - dann ist er fertig, und die Runde muss nicht die
 * Frist abwarten.  Zweitens wird gezaehlt, was ein Slave nicht tun darf: ein
 * KOMMANDO MIT P.  Auf einem DAMA-Kanal entscheidet genau eine Station, wer
 * sendet; wer selbst pollt, hat das Verfahren nicht verstanden oder faehrt
 * es nicht.  TNN zaehlt dasselbe (polDAMA), warnt und trennt irgendwann.
 * Wir zaehlen und sagen es - trennen tun wir nicht, aus demselben Grund, aus
 * dem unser Slave bei einem verstummten Master nicht auflegt: das kostet den
 * Benutzer alles oberhalb von AX.25, und ein Kanal, der auch CSMA vertraegt,
 * ist die kleinere Strafe.
 */

/* WIE OFT EINE STATION VERSTOSSEN HAT, und "je Station" heisst hier wirklich
 * je Station und nicht je Port (Thomas).  TNNs Schwelle - "sind mehr
 * Verwarnungen noetig, als unter DAMA-MaxPol angegeben, so wird der User
 * disconnected" - meint den einzelnen Benutzer; ein Portzaehler wuerde die
 * fuenfte Verwarnung irgendwem zustellen.  Gezaehlt wird auf das Rufzeichen,
 * das den Kanal BELEGT hat, also auf dama_station() und nicht auf den Urheber
 * des Rahmens - bei einer digipeateten Sitzung sind das verschiedene.
 *
 * Die Liste bleibt klein, weil sie bei jedem Verstoss aufgeraeumt wird: eine
 * Station ohne stehende Verbindung auf ihrem Port ist fertig, und ihr Zaehler
 * mit ihr - dieselbe Semantik wie TNNs "im QSO verwarnt".  Aufgeraeumt wird
 * dabei nur, wenn ueberhaupt jemand verstoesst; hoeren alle auf, bleibt der
 * letzte Eintrag stehen, bis der naechste Verstoss ihn mitnimmt.  Das ist
 * eine Handvoll Oktette und keinen eigenen Zeitgeber wert.
 */

/* UND ER ALTERT AUCH OHNE VERBINDUNGSENDE (Thomas' Frage).  Das Ende der
 * Verbindung allein genuegt nicht: ein Link zwischen zwei Knoten steht
 * wochenlang, und ein Zaehler, der dort nur waechst, laeuft irgendwann in
 * jede Schwelle - bestraft wuerde Verhalten, das ueber Tage verteilt war.
 *
 * Zehn Minuten, und die Zahl ist nicht beliebig: eine Station, die das
 * Verfahren nicht fahrt, sendet unaufgefordert im Sekundenabstand.  Ein
 * Schub ist damit in Sekunden bei der Schwelle, und zehn Minuten Ruhe sind
 * zwei Groessenordnungen daneben - wer so lange still war, hat aufgehoert.
 */
#define DAMA_VIOL_FORGET        600L    /* Sekunden ohne Verstoss */

struct dama_v {
	struct dama_v *next;
	struct iface *ifp;
	uint8 call[AXALEN];
	long n;
	int32 last;                     /* secclock des letzten Verstosses */
	int32 at_poll;                  /* ifp->dama_polls dabei - siehe unten */
	int fresh;                      /* dieser Aufruf hat hochgezaehlt */
	int32 ban_until;                /* secclock, 0: kein Bann */
	int32 ban_said;                 /* wann wir es zuletzt sagten */
	int kick;                       /* zu trennen, sobald es sicher ist */
};

static struct dama_v *Dama_v;

static int dama_station_linked(struct iface *ifp, const uint8 *call)
{
	struct ax25_cb *axp;

	for (axp = Ax25_cb; axp != NULL; axp = axp->next)
		if (axp->iface == ifp && axp->peer == NULL &&
		    (axp->state == LAPB_CONNECTED ||
		     axp->state == LAPB_RECOVERY) &&
		    addreq(dama_station(axp), call))
			return 1;
	return 0;
}

static struct dama_v *dama_violation(struct iface *ifp, const uint8 *call)
{
	struct dama_v *vp;
	struct dama_v **pp;

	int32 now = secclock();

	for (pp = &Dama_v; (vp = *pp) != NULL; ) {
		if (dama_station_linked(vp->ifp, vp->call) &&
		    now - vp->last < DAMA_VIOL_FORGET) {
			pp = &vp->next;
		} else {
			*pp = vp->next;
			free(vp);
		}
	}
	for (vp = Dama_v; vp != NULL; vp = vp->next)
		if (vp->ifp == ifp && addreq(vp->call, call)) {
			vp->last = now;
			/* EIN ZUG IST EIN VERSTOSS, nicht ein Rahmen (Thomas).
			 * Wer mit maxframe 7 sieben Rahmen schickt, hat sich
			 * den Kanal EINMAL genommen; ihn siebenmal zu zaehlen
			 * verbraucht die Schwelle in einem Wimpernschlag und
			 * schickt Verwarnungen, die niemand mehr lesen kann,
			 * bevor der Disconnect kommt.
			 *
			 * Die Grenze ist unser eigener POLL und keine Uhr: er
			 * hat ausserhalb seines Zuges gesendet, und der
			 * naechste Poll eroeffnet die naechste Gelegenheit,
			 * es wieder zu tun.  Eine Zeitschranke waere hier
			 * falsch - auf 1k2 dauert ein voller Zug aus sieben
			 * Rahmen zu 256 Oktetten gut fuenfzehn Sekunden.
			 */
			if (vp->at_poll == ifp->dama_polls) {
				vp->fresh = 0;
				return vp;
			}
			vp->at_poll = ifp->dama_polls;
			vp->n++;
			vp->fresh = 1;
			return vp;
		}
	vp = (struct dama_v *) callocw(1, sizeof(struct dama_v));
	vp->ifp = ifp;
	addrcp(vp->call, call);
	vp->n = 1;
	vp->last = now;
	vp->at_poll = ifp->dama_polls;
	vp->fresh = 1;
	vp->next = Dama_v;
	Dama_v = vp;
	return vp;
}

/* TRENNEN, ABER NICHT HIER UND JETZT.
 *
 * Der erste Entwurf legte den DISC direkt in dama_master_input() - und der
 * Knoten stuerzte beim fuenften Verstoss ab.  Zu Recht: diese Funktion
 * laeuft FRUEH in lapb_input(), und danach arbeitet die Zustandsmaschine
 * mit demselben Kontrollblock weiter.  Wer eine Verbindung mitten aus ihrer
 * eigenen Empfangsverarbeitung herausreisst, zieht ihr den Boden weg.
 *
 * Der Master hat aber einen sicheren Ort: seinen Rundentimer.  Dort ist
 * kein Rahmen in Arbeit, und die Runde geht ohnehin ueber alle Links.
 */

static void dama_kick_marked(struct iface *ifp)
{
	struct ax25_cb *axp;
	struct ax25_cb *next;
	struct dama_v *vp;

	for (vp = Dama_v; vp != NULL; vp = vp->next) {
		if (vp->ifp != ifp || !vp->kick)
			continue;
		vp->kick = 0;
		for (axp = Ax25_cb; axp != NULL; axp = next) {
			next = axp->next;
			if (axp->iface != ifp || axp->peer != NULL)
				continue;
			if (axp->state != LAPB_CONNECTED &&
			    axp->state != LAPB_RECOVERY)
				continue;
			if (!addreq(dama_station(axp), vp->call))
				continue;
			/* Die Begruendung stand vorher im UI, der DISC darf
			 * also wortlos sein.
			 */
			free_q(&axp->txq);
			axp->retries = 0;
			sendctl(axp, LAPB_COMMAND, DISC | PF);
			stop_timer(&axp->t3);
			start_timer(&axp->t1);
			lapbstate(axp, LAPB_DISCPENDING);
		}
	}
}

/*---------------------------------------------------------------------------*/

/* NACH WIE VIELEN VERSTOESSEN GETRENNT WIRD, in "enforce".  Fuenf, wie TNNs
 * par 6 (DAMA-MaxPol).  Noch fest; einstellbar zu machen lohnt erst, wenn
 * jemand einen Grund hat, es zu aendern.
 */
#define DAMA_MAXVIOL    5

/* WIE LANGE NACH EINEM ZWANGSDISCONNECT NICHTS MEHR ANGENOMMEN WIRD.
 *
 * Nicht das Sperren ist der Zweck, sondern das Ende des Kreislaufs, den
 * Thomas beschrieben hat: verbinden, irgendwo einloggen, fliegen, wieder
 * verbinden, weitermachen wo man war, wieder fliegen.  Der kostet mehr
 * Kanal als der Bann.
 *
 * Fuenf Minuten (Thomas).  TNN hat dafuer kein Vorbild - dort gibt es nach
 * dem Zwangsdisconnect gar nichts, der Nutzer kommt sofort wieder.
 */
#define DAMA_BAN        300L    /* Sekunden */

/* EIN TEXT ALS UI AN EINE STATION.  Verwarnung und Abweisung nehmen
 * denselben Weg - beide liegen ausserhalb der Sitzung, und genau das ist
 * ihr Zweck.
 */

static void dama_ui(struct iface *ifp, const uint8 *call,
		    const char *text, int len)
{
	struct ax25 hdr;
	struct mbuf *bp;

	if (ifp == NULL || len <= 0 || (bp = alloc_mbuf((uint) len)) == NULL)
		return;
	memcpy(bp->data, text, (size_t) len);
	bp->cnt = (uint) len;
	memset(&hdr, 0, sizeof(hdr));
	addrcp(hdr.dest, call);
	hdr.cmdrsp = LAPB_COMMAND;
	ax_send_ui(ifp, &hdr, PID_NO_L3, &bp);
}

/*---------------------------------------------------------------------------*/

/* DIE VERWARNUNG GEHT ALS UI, NICHT IN DIE SITZUNG - und das war Thomas'
 * Einwand gegen meinen ersten Vorschlag:
 *
 *     "Nein, keine Meldung in PID=text.  Sonst steht in Mails, die er
 *      abruft: 'Schoene Gruesse aus ... tolles Wetter und *** 6
 *      DAMA-Verstoesse'."
 *
 * Text in eine AX.25-Sitzung ist NICHT ausserhalb des Bandes: er landet im
 * Bytestrom, den die Anwendung liest, also mitten in der Mail.  Das ist
 * keine Verwarnung, sondern eine Beschaedigung der Nutzdaten - und zwar
 * auch auf einem reinen Text-Link, wo die openpid-Regel nichts dagegen
 * hat.  TNNs Weg ("im QSO verwarnt") hat genau dieses Problem.
 *
 * Das UI liegt dagegen ausserhalb der Sitzung, kann nichts zerstoeren, und
 * es funktioniert unabhaengig davon, was der Link sonst traegt (NET/ROM,
 * FlexNet, IP).  Dass es unquittiert ist, wiegt leicht: wer ploetzlich
 * rausfliegt, schaltet den Monitor ein, um zu sehen warum - und genau dann
 * wartet sie dort auf ihn (Thomas).
 *
 * ADRESSIERT AN DEN, DER GESENDET HAT, nicht an den Urheber des Rahmens:
 * bei einer digipeateten Sitzung ist das nicht dasselbe.
 *
 * DER WORTLAUT IST VORLAEUFIG - Thomas will ihn noch festlegen.
 */

static void dama_warn(struct iface *ifp, const uint8 *call, long n, int last)
{
	char buf[160];
	char zahl[24];
	int len;

	if (ifp == NULL)
		return;
	/* snprintf und ein Puffer mit Luft.  Der erste Entwurf nahm sprintf
	 * und char[80] - und der Text war mit ", disconnecting" GENAU EIN
	 * OKTETT zu lang.  Clang macht daraus einen trap, also SIGILL, und
	 * zwar nur in "enforce" beim Erreichen der Schwelle: ein Absturz, der
	 * ausschliesslich im seltensten Zweig zuschlaegt.
	 */
	/* DIE ZAEHLUNG NACH VORN, nicht als Fussnote ans Ende (Thomas) - und
	 * die Schwelle nur dort, wo es sie gibt: in "permissive" wird nie
	 * getrennt, ein "1/5" waere dort eine Drohung ohne Deckung.  Der Ton
	 * unterscheidet sich aus demselben Grund: "please use" ist eine Bitte,
	 * "you need to enable" eine Ansage, und nur eine davon ist gedeckt.
	 */
	/* "x/y" sagt: y Meldungen gibt es.  Ist die Schwelle ueberschritten,
	 * waere "6/5" schief - dann ist die Zaehlung vorbei und es wird nur
	 * noch abgewiesen (Thomas).
	 */
	if (ifp->dama_policy != DAMA_ENFORCE)
		snprintf(zahl, sizeof(zahl), "%ld", n);
	else if (n > DAMA_MAXVIOL)
		snprintf(zahl, sizeof(zahl), "%d/%d", DAMA_MAXVIOL, DAMA_MAXVIOL);
	else
		snprintf(zahl, sizeof(zahl), "%ld/%d", n, DAMA_MAXVIOL);
	/* DREI STUFEN, und der Name sagt, was folgt (Thomas): "Notice" ist
	 * eine Mitteilung ohne Konsequenz, "Warning" kuendigt eine an,
	 * "Fatal" vollzieht sie.  Wer nicht trennt, warnt auch nicht - in
	 * "permissive" bleibt es bei der Mitteilung.
	 *
	 * Und "me, the master" statt "a DAMA master": das Wort DAMA stand
	 * dreimal in einem Satz, und das "a" meinte ohnehin uns selbst.
	 */
	len = snprintf(buf, sizeof(buf),
		       "DAMA %s (%s): this channel is controlled by me, the "
		       "master, who polls you - %s%s\r",
		       ifp->dama_policy != DAMA_ENFORCE ? "Notice" :
		       last ? "Fatal" : "Warning", zahl,
		       ifp->dama_policy == DAMA_ENFORCE
		       ? "you need to enable DAMA" : "please use DAMA",
		       last ? ", disconnecting" : "");
	if (len < 0)
		len = 0;
	if (len > (int) sizeof(buf))
		len = (int) sizeof(buf);
	dama_ui(ifp, call, buf, len);
}

/*---------------------------------------------------------------------------*/

/* DARF DIESER VERBINDUNGSAUFBAU HEREIN?  1 heisst nein, der Aufrufer soll
 * mit DM antworten.
 *
 * Gefragt wird nur auf einem Master-Port und nur, wenn diese Station nach
 * zu vielen Verstoessen hinausgeflogen ist.  Der Sinn ist nicht das
 * Sperren, sondern das Ende des Kreislaufs (Thomas): verbinden, einloggen,
 * fliegen, wieder verbinden, weitermachen, wieder fliegen.
 *
 * ZWEI AUSWEGE, und der erste ist der eigentliche Zweck der Sache:
 *
 *   - WER MIT GESETZTEM DAMA-BIT ANKLOPFT, kommt sofort wieder herein und
 *     der Bann faellt.  Er hat getan, worum ihn die Verwarnung gebeten
 *     hat; ihn dann noch warten zu lassen waere Strafe ohne Zweck.
 *   - Nach Ablauf der Frist ohnehin.
 *
 * BEI JEDER ABWEISUNG GEHT EIN UI HINAUS mit Grund und Restzeit.  Waehrend
 * des Banns bekommt er von uns keine Sitzung - auch keine zu unserem
 * eigenen Infodienst, denn sonst haetten wir "enforce" eingeschaltet und
 * enforcten nicht (Thomas): Mailbox, DX-Cluster, Convers und Login sind ja
 * auch bei uns.  Die Auskunft steckt deshalb in der Meldung.
 */

int dama_connect_refused(struct iface *ifp, const struct ax25 *hdr)
{
	struct dama_v *vp;
	const uint8 *who;
	char buf[160];
	int len;
	long rest;

	if (hdr == NULL)
		return 0;

	/* ERSTENS: IST DER GERUFENE GESPERRT?  Ihn erst aufzubauen, um ihn
	 * gleich wieder zu trennen, waere doppelte Arbeit auf dem Kanal - und
	 * in fuenf Minuten ist seine Haltezeit ohnehin abgelaufen (Thomas).
	 * Der Rufer bekommt sein DM sofort, und das ist die richtige Antwort
	 * im richtigen Protokoll: "Verbindung abgelehnt", an den, der sie
	 * aufgebaut hat und es anders versuchen kann.
	 *
	 * KEIN UI dabei.  An den Rufer eine Meldung ueber einen Dritten zu
	 * schicken hilft ihm nicht, und der Gerufene kann nichts dafuer, dass
	 * er gerufen wurde.
	 *
	 * Gesucht wird ueber ALLE Ports: der Bann steht auf dem DAMA-Kanal,
	 * der Ruf kommt ueber den Link herein.
	 */
	for (vp = Dama_v; vp != NULL; vp = vp->next)
		if (vp->ban_until && addreq(vp->call, hdr->dest) &&
		    vp->ban_until > secclock())
			return 1;

	/* ZWEITENS: KLOPFT EIN GESPERRTER SELBST AN? */
	if (ifp == NULL || ifp->dama != DAMA_MASTER)
		return 0;
	who = dama_sender(hdr);
	for (vp = Dama_v; vp != NULL; vp = vp->next)
		if (vp->ifp == ifp && addreq(vp->call, who))
			break;
	if (vp == NULL || vp->ban_until == 0)
		return 0;
	if (hdr->ext & SSID_DAMA) {     /* er hat es eingeschaltet */
		vp->ban_until = 0;
		vp->n = 0;
		return 0;
	}
	if ((rest = vp->ban_until - secclock()) <= 0) {
		vp->ban_until = 0;
		vp->n = 0;
		return 0;
	}
	/* HOECHSTENS EINMAL JE MINUTE.  Gemessen: eine Station, die alle zwei
	 * Sekunden ein SABM wiederholt, holte sich in fuenf Minuten rund 150
	 * Abweisungs-UIs ab - genau der Verkehr, den DAMA sparen soll.  Die
	 * Auskunft muss ihn erreichen, aber einmal genuegt; der DM sagt ihm
	 * bei jedem Versuch, dass es nicht geht.
	 */
	if (vp->ban_said == 0 || secclock() - vp->ban_said >= 60) {
		vp->ban_said = secclock();
		len = snprintf(buf, sizeof(buf),
			       "DAMA Fatal: rejecting - this channel is "
			       "controlled by me, the master, who polls you; "
			       "enable DAMA and call again, or wait %lds\r",
			       rest);
		if (len > 0)
			dama_ui(ifp, who, buf, len > (int) sizeof(buf)
				? (int) sizeof(buf) : len);
	}
	return 1;
}

/*---------------------------------------------------------------------------*/

void dama_master_input(struct iface *ifp, struct ax25_cb *axp,
		       const struct ax25 *hdr, int isu, int ispoll, int isfinal)
{
	struct dama_m *mp;

	if (ifp == NULL || ifp->dama != DAMA_MASTER || axp == NULL)
		return;
	/* EIN VERBINDUNGSAUFBAU IST KEIN VERSTOSS, und das war der erste
	 * Messfehler: ein SABM ist ein Kommando mit P, also sah es aus wie ein
	 * Poll.  Die Spezifikation legt den Aufbau aber ausdruecklich in CSMA
	 * (doc/DAMA-SLAVE.md) - eine Station ohne Link wird nie gepollt und
	 * koennte sonst nie hereinkommen.  Gezaehlt wird deshalb nur, was auf
	 * einer STEHENDEN Verbindung pollt, und keine U-Rahmen: SABM, DISC und
	 * ihresgleichen tragen das P aus anderen Gruenden.
	 */
	if (ispoll && !isu &&
	    (axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY)) {
		const uint8 *who = dama_station(axp);
		struct dama_v *vp = dama_violation(ifp, who);
		long n = vp->n;

		ifp->dama_violations++;         /* Summe fuer die Statuszeile */
		/* ALLES WEITERE NUR, WENN WIRKLICH HOCHGEZAEHLT WURDE.  Sonst
		 * haengt die Meldung am RAHMEN statt an der Zaehlung, und ein
		 * Zug aus sieben Rahmen erzeugt sieben gleiche Verwarnungen
		 * auf einen Schlag - Laerm, den niemand liest, auf genau dem
		 * Kanal, den wir schonen wollen (Thomas).
		 */
		if (vp->fresh && (n == 1 || !(n % 10))) {
			char buf[AXBUF];

			/* WER GESENDET HAT, nicht wer den Rahmen verfasst hat
			 * (Thomas).  Bei einer digipeateten Sitzung ist
			 * hdr->source der Benutzer am fernen Ende - der war
			 * nie auf unserem Kanal und kann fuer nichts.  Den
			 * Kanal belegt hat der Digipeater, und das ist genau
			 * die Station, die dama_station() liefert; die Regel
			 * dafuer steht schon im Kommentar dort.  Dieselbe
			 * Adresse ist spaeter der Empfaenger der Verwarnung.
			 */
			/* EINZEILIG, und der Port zuerst.  Wer zwei Ports als
			 * Master fahrt, will am Log sehen, auf welchem
			 * Einstieg der Stoerer sitzt (Thomas) - und eine
			 * zweite Zeile ohne Portnamen waere im Wechsel
			 * zweier Kanaele nicht mehr zuzuordnen.
			 */
			printf("%s: %s polled us - on a DAMA channel only the "
			       "master polls (%ld so far)\n",
			       ifp->name, pax25(buf, who), n);
		}
		/* WAS DARAUS FOLGT, entscheidet der Sysop.  Gezaehlt wird in
		 * allen drei Betriebsarten - der Zaehler ist auch Statistik -,
		 * nur die Folge unterscheidet sich.
		 */
		if (vp->fresh && ifp->dama_policy != DAMA_LAZY &&
		    (n == 1 || !(n % 10) ||
		     (ifp->dama_policy == DAMA_ENFORCE && n >= DAMA_MAXVIOL)))
			dama_warn(ifp, who, n,
				  ifp->dama_policy == DAMA_ENFORCE &&
				  n >= DAMA_MAXVIOL);
		if (ifp->dama_policy == DAMA_ENFORCE && n >= DAMA_MAXVIOL) {
			vp->kick = 1;   /* vollzogen wird im Rundentimer */
			vp->ban_until = secclock() + DAMA_BAN;
			vp->ban_said = 0;
		}
	}
	if ((mp = dama_m_port(ifp, 0)) == NULL || !mp->busy)
		return;
	if (!addreq(dama_station(axp), mp->turn))
		return;
	if (!mp->answered) {
		/* Er hat sich gemeldet, also gilt ab jetzt die ZUGDAUER und
		 * nicht mehr die Antwortfrist: was er in einem Block sendet,
		 * darf laenger dauern als das Warten darauf, dass er
		 * ueberhaupt anfaengt.
		 */
		mp->answered = 1;
		stop_timer(&mp->t);
		set_timer(&mp->t, dama_turn_time(ifp));
		start_timer(&mp->t);
	}
	if (isfinal)
		dama_master_gap(mp);
}

/*---------------------------------------------------------------------------*/

/* Die Runde anstossen, wenn sie ruht.  Gerufen, sobald eine Verbindung auf
 * dem Port steht - vorher gibt es niemanden zu pollen.
 */

void dama_master_kick(struct iface *ifp)
{
	struct dama_m *mp;

	if (ifp == NULL || ifp->dama != DAMA_MASTER)
		return;
	mp = dama_m_port(ifp, 1);
	if (mp->busy || run_timer(&mp->t))
		return;
	dama_master_next(mp);           /* busy ist 0: das pollt sofort */
}

/*---------------------------------------------------------------------------*/

void dama_master_stop(struct iface *ifp)
{
	struct dama_m *mp;

	if ((mp = dama_m_port(ifp, 0)) == NULL)
		return;
	stop_timer(&mp->t);
	mp->busy = 0;
	memset(mp->turn, 0, AXALEN);
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

/* "ifconfig <iface> dama off | slave [timeout <sec>]"
 *
 * Die Frist gehoert syntaktisch zur Rolle (Thomas), und das ist mehr als
 * Kosmetik: sie MEINT etwas, das nur ein Slave hat - wie lange ein Master
 * schweigen darf, bevor wir ihm nicht mehr folgen.  Ein Master hat andere
 * Zeiten (Rundenabstand, Wartezeit auf den Gepollten), und die eine hier
 * fuer die andere zu recyceln waere derselbe Name fuer zwei Sachverhalte.
 * Als eigener Befehl "damatimeout" stand sie vorher daneben und sagte
 * nicht, wozu sie gehoert.
 *
 * VORGABE 120 SEKUNDEN, und dafuer gibt es jetzt zwei Gruende.  TNN nimmt
 * dieselben zwei Minuten (damaok = 12000 Hundertstel, l2rx.c), und die
 * Spezifikation nennt gar keine Zahl.  Der bessere Grund ist aber der
 * Kanal selbst: ein Master pollt seine Stationen reihum, und bei sechzehn
 * Stationen zu je zwei bis fuenf Sekunden ist eine Runde eine Minute lang
 * (Thomas).  Eine kuerzere Frist erklaerte einen Master fuer verloren, der
 * bloss seine Liste abarbeitet.
 */

/* Ist irgendein Port Master?  Dann darf der Digipeat-Modus nicht auf 1 -
 * siehe die Warnung in ifdama().  Die Reihenfolge der Konfiguration ist
 * beliebig, also muss die Warnung von BEIDEN Seiten kommen: wer erst den
 * Master setzt und dann den Modus, wuerde sie sonst nie sehen.
 */

const char *dama_master_port(void)
{
	struct iface *ifp;

	for (ifp = Ifaces; ifp != NULL; ifp = ifp->next)
		if (ifp->dama == DAMA_MASTER)
			return ifp->name;
	return NULL;
}

/*---------------------------------------------------------------------------*/

int ifdama(int argc, char *argv[], void *p)
{
	struct iface *ifp = (struct iface *) p;
	int i;

	if (!strcmp(argv[1], "off")) {
		ifp->dama = DAMA_OFF;
		ifp->dama_heard = 0;
		dama_channel_access(ifp, 0);
		dama_master_stop(ifp);
		/* Das Fragen nach ARP kommt zurueck, WENN wir es waren, die es
		 * abgeschaltet haben (Thomas' Frage).  Der Grund war DAMA;
		 * faellt der Grund weg, faellt die Folge weg.  Hat der Sysop
		 * "arp off" selbst gesagt, bleibt es - eine Ansage von ihm
		 * nehmen wir ihm nicht wieder aus der Hand.
		 */
		if (ifp->noarp && ifp->noarp_auto) {
			ifp->noarp = 0;
			ifp->noarp_auto = 0;
			printf("%s: arp requests switched back on\n", ifp->name);
		}
		return 0;
	}
	if (!strcmp(argv[1], "master")) {
		/* REFUSED, NOT WARNED, and that is Thomas' call: in a net.rc
		 * nobody sees a warning scroll past, and the node would go on
		 * running a configuration that cannot work.  The same refusal
		 * guards the other order in dodigipeat(), because which of the
		 * two lines comes first is not obvious to anybody.
		 */
		if (Digipeat != 2) {
			printf("%s: refused - dama mode master needs digi mode "
			       "2 (\"ax25 digipeat 2\")\n  Repeating verbatim "
			       "hands the called station an UNMARKED connect: "
			       "it answers\n  without DAMA, never having seen "
			       "it, and we then hold that against it -\n  while "
			       "the session misses the time slots.  Only "
			       "terminating and setting\n  up our own link "
			       "marks it (dama_mark in sendframe), and only "
			       "digipeat 2\n  does that.\n", ifp->name);
			return 1;
		}
		/* VORGABE PERMISSIVE, nicht lazy und nicht enforce.  Wer
		 * nichts sagt, bekommt den mittleren Weg: informieren, aber
		 * niemandem die Verbindung nehmen.
		 */
		ifp->dama_policy = DAMA_PERMISSIVE;
		if (argc > 2) {
			if (!strcmp(argv[2], "lazy"))
				ifp->dama_policy = DAMA_LAZY;
			else if (!strcmp(argv[2], "permissive"))
				ifp->dama_policy = DAMA_PERMISSIVE;
			else if (!strcmp(argv[2], "enforce"))
				ifp->dama_policy = DAMA_ENFORCE;
			else {
				printf("ifconfig %s dama master "
				       "[lazy|permissive|enforce]\n"
				       "  lazy        nimmt jeden stillschweigend an\n"
				       "  permissive  informiert per UI, trennt nicht (default)\n"
				       "  enforce     verwarnt und trennt nach %d Verstoessen\n",
				       ifp->name, DAMA_MAXVIOL);
				return 1;
			}
		}
		ifp->dama = DAMA_MASTER;
		ifp->dama_heard = 0;
		/* Auch der Master, und aus demselben Grund wie bei TNN: ein
		 * DAMA-Port bekommt dort P=255 gegen 160 auf einem
		 * gewoehnlichen Einstieg.  Das DCD bleibt in Kraft - er hoert
		 * also weiter, ob der Gepollte noch sendet, und hackt ihn
		 * nicht ab (Thomas).  Raum fuer neue Clients schafft die
		 * PAUSE zwischen zwei Polls, nicht die Wuerfelei.
		 */
		dama_channel_access(ifp, 1);
		dama_master_kick(ifp);
		return 0;
	}
	if (strcmp(argv[1], "slave")) {
		printf("ifconfig %s dama off | slave [timeout <seconds>]"
		       " | master [lazy|permissive|enforce]\n",
		       ifp->name);
		return 1;
	}

	for (i = 2; i < argc; i++) {
		if (!strcmp(argv[i], "timeout")) {
			long n;

			if (++i >= argc) {
				printf("\"timeout\" wants a number of seconds\n");
				return 1;
			}
			n = atol(argv[i]);
			if (n < 0 || n > 86400L) {
				printf("timeout %s is outside 0..86400 "
				       "(0 = the built-in %d)\n",
				       argv[i], DAMA_WATCHDOG_DEFAULT);
				return 1;
			}
			ifp->dama_watchdog = (int) n;
			continue;
		}
		printf("ifconfig %s dama off | slave [timeout <seconds>]"
		       " | master [lazy|permissive|enforce]\n",
		       ifp->name);
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
	 * wer dort IP im Datagramm-Modus faehrt, traegt seine Partner ein -
	 * dann entsteht sie erst gar nicht.  Gesagt wird es, weil eine
	 * Nebenwirkung, die niemand sieht, die schlechtere Art ist,
	 * Voreinstellungen zu treffen.  Antworten tun wir weiter.
	 */
	if (!ifp->noarp) {
		ifp->noarp = 1;
		ifp->noarp_auto = 1;
		printf("%s: arp requests switched off - on a DAMA channel a "
		       "request is a broadcast\n"
		       "  to QST and costs the channel.  Enter the partners "
		       "instead:\n"
		       "        arp add <ip> ax25 <call>\n"
		       "  and none is needed.  \"ifconfig %s arp on\" puts it "
		       "back; incoming\n"
		       "  requests are answered either way.\n",
		       ifp->name, ifp->name);
	}
	return 0;
}
/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/

/* What "ifconfig <if>" adds about DAMA.  Silent on a port that has none, so
 * that the display of an ordinary node does not grow a line about something
 * it is not doing.
 */

void dama_show(struct iface *ifp)
{
	if (ifp->dama == DAMA_MASTER) {
		char buf[AXBUF];
		struct dama_m *mp = dama_m_port(ifp, 0);

		printf("           dama master (%s), answer %ldms turn %ldms "
		       "gap %lds, ",
		       ifp->dama_policy == DAMA_LAZY ? "lazy" :
		       ifp->dama_policy == DAMA_ENFORCE ? "enforce" :
		       "permissive",
		       (long) dama_answer_time(ifp),
		       (long) dama_turn_time(ifp),
		       DAMA_GAP_DEFAULT / 1000L);
		/* "serving" und nicht noch einmal "turn": die Zeile nennt
		 * schon eine Zugdauer, und dasselbe Wort fuer zwei Dinge in
		 * einer Zeile liest sich als Fehler.
		 */
		if (mp != NULL && mp->busy)
			printf("serving %s", pax25(buf, mp->turn));
		else
			printf("idle");
		printf(", polls %ld, non-dama polls heard %ld\n",
		       (long) ifp->dama_polls, (long) ifp->dama_violations);
		return;
	}
	if (ifp->dama != DAMA_SLAVE)
		return;
	printf("           dama slave, timeout %ds, ", dama_watchdog(ifp));
	if (dama_in_force(ifp)) {
		char buf[AXBUF];
		printf("heard from %s", pax25(buf, ifp->dama_sender));
	} else
		printf("nothing heard");
	printf(", found %ld lost %ld polls %ld\n",
	       (long) ifp->dama_entered, (long) ifp->dama_lost,
	       (long) ifp->dama_polls);
}
