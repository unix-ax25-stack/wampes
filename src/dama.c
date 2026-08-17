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

/* Our turn has come and the polled link has had its chance.  Give it to one
 * of the others on this port, taken in rotation - the digipeated connection
 * that the master cannot address, or the second link of a multiconnect.
 *
 * Called once, at the end of lapb_input(), after the polled link itself has
 * been through lapb_output().  If that link sent something, the turn is used
 * up and this does nothing.
 */

void dama_serve_others(struct iface *ifp, struct ax25_cb *polled)
{
	struct ax25_cb *axp;
	int eligible = 0;
	int n;

	if (ifp == NULL || !ifp->dama_window)
		return;

	for (axp = Ax25_cb; axp != NULL; axp = axp->next)
		if (axp != polled && axp->iface == ifp && axp->txq != NULL &&
		    (axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY))
			eligible++;
	if (eligible == 0)
		return;

	n = ifp->dama_turn % eligible;
	ifp->dama_turn++;
	for (axp = Ax25_cb; axp != NULL; axp = axp->next)
		if (axp != polled && axp->iface == ifp && axp->txq != NULL &&
		    (axp->state == LAPB_CONNECTED || axp->state == LAPB_RECOVERY))
			if (n-- == 0) {
				lapb_output(axp);
				return;
			}
}

/*---------------------------------------------------------------------------*/

/* Something wanted to go out and was held back.  Make sure T1 is running.
 *
 * The watchdog above is a comparison, not a timer, and that only works if
 * somebody keeps asking.  I had assumed somebody always would - and measured
 * otherwise: ackours() stops T1 the moment nothing is outstanding, T2 had
 * already fired once and gone, T3 is a quarter of an hour away and T5 an
 * hour.  A slave with a held acknowledgement therefore sat silent long past
 * its timeout, because no timer was left alive to notice that the master had
 * gone.
 *
 * T1 is the right one to keep: it is the timer for "waiting to make
 * progress", it is silent while DAMA holds, and recover() rearms it each
 * time.  So one period after the watchdog runs out, the gate opens and
 * whatever was waiting goes out by the ordinary path.
 *
 * Called only where something really is pending - an unanswered
 * acknowledgement, a full send queue, a disconnect that wants to happen.  A
 * link with nothing to say needs no timer, because it has nothing to be
 * woken for.
 */

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
	printf("           dama slave, timeout %ds, master ", dama_watchdog(ifp));
	if (dama_in_force(ifp)) {
		char buf[AXBUF];
		printf("%s", pax25(buf, ifp->dama_master));
	} else
		printf("not heard");
	printf(", found %ld lost %ld polls %ld\n",
	       (long) ifp->dama_entered, (long) ifp->dama_lost,
	       (long) ifp->dama_polls);
}
