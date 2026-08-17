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

void dama_heard_frame(struct iface *ifp)
{
	if (ifp == NULL || ifp->dama != DAMA_SLAVE)
		return;
	if (ifp->dama_heard == 0)
		ifp->dama_entered++;
	ifp->dama_heard = secclock();
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
	if (axp == NULL || axp->dama_polled)
		return 0;
	return dama_in_force(axp->iface);
}

/*---------------------------------------------------------------------------*/

/* Open and close the window around the handling of one received frame.
 *
 * dama_poll_begin() is called for EVERY frame, not only for polls, and clears
 * before it sets.  That is not tidiness: lapb_input() has seven early exits,
 * all of them tearing the link down, and without the clearing a control block
 * could carry a permission out of the invocation that granted it and into a
 * timer that fires later.  Cheaper to make the flag impossible to leak than
 * to prove that none of those seven paths matters.
 */

void dama_poll_begin(struct ax25_cb *axp, int ispoll)
{
	if (axp == NULL)
		return;
	axp->dama_polled = 0;
	if (ispoll && dama_in_force(axp->iface)) {
		axp->dama_polled = 1;
		axp->iface->dama_polls++;
	}
}

void dama_poll_end(struct ax25_cb *axp)
{
	if (axp != NULL)
		axp->dama_polled = 0;
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
	printf("           dama slave, timeout %ds, master %s",
	       dama_watchdog(ifp), dama_in_force(ifp) ? "heard" : "not heard");
	printf(", found %ld lost %ld polls %ld\n",
	       (long) ifp->dama_entered, (long) ifp->dama_lost,
	       (long) ifp->dama_polls);
}
