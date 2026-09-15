/* Which AX.25 frame classes may cross a port, and in which direction.
 *
 * The protocol gate (pidfilter.c) cannot tell UI from I: "text" (PID_NO_L3)
 * rides in both, and the pid is not even looked at until the frame type has
 * decided which path the frame takes.  "This is a UI port" - a beacon or
 * APRS frequency where nothing but connectionless frames may move - and
 * "this user port carries only connections" are therefore not expressible
 * with the pid gate alone.  This is that gate, and it asks one question:
 * may UI frames cross here, and may connected-mode frames.
 *
 * THE TWO CLASSES and nothing finer, and AX.25 itself makes the split: a
 * frame is either UI or it belongs to the LAPB sublayer (SABM, I, S, UA, DM,
 * FRMR, DISC - the whole connected world).  What we do with the frames we
 * let through is the pid gate's and the protocols' business, as before.
 *
 * WHAT IT IS ABOUT: frames that are ours, addressed to us or sent by us -
 * the very "ours" the pid gate means.  Traffic we merely repeat is
 * digipeated before the frame type is looked at and stays untouched; a digi
 * is not the sender and does not edit other people's traffic.  A port-wide
 * statement - filtering what we repeat as well - is a possible extension,
 * and would be one condition more in ax_recv()'s digipeat branch.
 *
 * A connection refused incoming on a "conn"-blocked port is answered with DM
 * (lapb.c), like the DAMA and EAX25 refusals, so the caller falls back at
 * once instead of retrying into silence.
 *
 * "conn" is a CONNECTION, and a connection is two-way: the direction it is
 * about is the direction in which it is OPENED.  "in" answers the question
 * "who may call US" and "out" the question "which calls may we place".  That
 * is all either direction governs: a port with "in block conn" refuses the
 * calls that come to it, but the traffic of a connection that is actually
 * alive - the UA answering the SABM we sent, the frames of a link we run -
 * crosses the port because it is neither a call coming in nor a call going
 * out.  Only the call's BEGINNING is gated: ifconfig does not tear links
 * down either, that is what "reset" and "kill" are for.
 *
 * A connection we may not open is refused in open_ax25() so the caller hears
 * it immediately.
 *
 * The "digiarp" widening of the ARP multicast is unaffected: it lives in
 * ax_output(), where the UI gate for this port runs before it, and says
 * which of the frames that get through may leave as QST-0 ARP.
 *
 * Nothing is blocked until somebody says so, as everywhere else here, so a
 * port that was never configured carries what it always carried.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "cmdparse.h"
#include "pidfilter.h"          /* printf, and PF_IN, PF_OUT, PF_NDIR */
#include "framefilter.h"

static const char *
frame_name(int bit)
{
	return bit == FRF_UI ? "ui" : "conn";
}

int
frame_blocks(
const struct iface *ifp,
int dir,
int class
){
	if (ifp == NULL || dir < 0 || dir >= PF_NDIR)
		return 0;
	return (ifp->framemask[dir] & class) != 0;
}

/* 1, wenn sich wirklich etwas geaendert hat - dieselbe Bescheidenheit wie
 * pid_set() in pidfilter.c: ein "allow", das nichts freigibt, weil nichts
 * gesperrt war, soll auch nichts behaupten (Thomas).
 */

static int
frame_set(
struct iface *ifp,
int dir,
int class,
int block
){
	if (block) {
		if (ifp->framemask[dir] & class)
			return 0;
		ifp->framemask[dir] |= (uint8) class;
	} else {
		if (!(ifp->framemask[dir] & class))
			return 0;
		ifp->framemask[dir] &= (uint8) ~class;
	}
	return 1;
}

static void
frame_show(
const struct iface *ifp,
int dir,
int indent
){
	int bit;
	int n = 0;

	/* In "verbose" gehoert die Zeile in den Block und nicht an den Rand -
	 * und der Portname steht dort schon in der Ueberschrift.
	 */
	if (indent)
		printf("           frame %s ", dir == PF_IN ? "in" : "out");
	else
		printf("%s frame %s ", ifp->name, dir == PF_IN ? "in" : "out");
	if (!ifp->framemask[dir]) {
		printf("blocked: (nothing)\n");
		return;
	}
	printf("blocked:");
	for (bit = FRF_UI; bit <= FRF_CONN; bit <<= 1)
		if (ifp->framemask[dir] & bit)
			printf("%s %s", n++ ? "," : "", frame_name(bit));
	putchar('\n');
}

void
frame_show_verbose(
const struct iface *ifp
){
	if (ifp == NULL)
		return;
	if (ifp->framemask[PF_IN])
		frame_show(ifp, PF_IN, 1);
	if (ifp->framemask[PF_OUT])
		frame_show(ifp, PF_OUT, 1);
}

/*---------------------------------------------------------------------------*/

char Frame_filter_usage[] =
	"ifconfig <iface> frame-filter             what is blocked here\n"
	"       ifconfig <iface> frame-filter in|out block <class>...\n"
	"       ifconfig <iface> frame-filter in|out allow <class>...\n"
	"       ifconfig <iface> frame-filter in|out clear       allow everything again\n"
	"\n"
	"  <class> is \"ui\" (connectionless frames) or \"conn\" (the connected-mode\n"
	"  whole: SABM/SABME, I, S, UA, DM, FRMR, DISC), or \"any\" for both.\n"
	"  \"block any\" then \"allow <class>\" is how one says \"nothing but this\":\n"
	"\n"
	"       ifconfig aprs2m frame-filter in block any\n"
	"       ifconfig aprs2m frame-filter in allow ui\n"
	"\n"
	"  \"reset\", \"defaults\" and the older \"none\" all mean \"clear\".\n"
	"\n"
	"  \"in\" is who may call us, \"out\" which calls we may place - a\n"
	"  connection is two-way, and the gate governs only the calling, at the\n"
	"  BEGINNING of the connection.  The traffic of a connection that is\n"
	"  alive gets through a refused port (the UA answering our own SABM must\n"
	"  come home); it is \"reset\" and \"kill\" that tear links down.  Only\n"
	"  frames that are ours - addressed to us, or sent by us - are gated;\n"
	"  traffic we merely digipeat is untouched, as it is for \"pid-filter\".  A\n"
	"  connection refused incoming on a \"conn\"-blocked port is answered with\n"
	"  DM, so the caller gives up at once instead of retrying.";

/*---------------------------------------------------------------------------*/

static int
frame_class(
const char *word)
{
	if (!strcmp(word, "ui"))
		return FRF_UI;
	if (!strcmp(word, "conn"))
		return FRF_CONN;
	return 0;
}

/* ifconfig <iface> frame-filter [in|out [block|allow <class>...]]
 *
 * This one takes a list, so doifconfig() hands it the rest of the line
 * instead of the usual name/value pair - see the note there.
 */

int
if_frame_filter(
int argc,
char *argv[],
void *p
){
	int block;
	int class;
	int dir;
	int i;
	int n;
	struct iface *ifp = (struct iface *) p;

	if (argc < 2 || !strcmp(argv[1], "?") || !strcmp(argv[1], "help")) {
		if (argc >= 2) {
			printf("Usage: %s\n", Frame_filter_usage);
			return 0;
		}
		frame_show(ifp, PF_IN, 0);
		frame_show(ifp, PF_OUT, 0);
		return 0;
	}

	if (!strcmp(argv[1], "in"))
		dir = PF_IN;
	else if (!strcmp(argv[1], "out"))
		dir = PF_OUT;
	else {
		printf("\"%s\": the direction is \"in\" or \"out\"\n", argv[1]);
		printf("Usage: %s\n", Frame_filter_usage);
		return 1;
	}

	if (argc < 3) {
		frame_show(ifp, dir, 0);
		return 0;
	}

	/* ZURUECKSETZEN, mit denselben Namen wie der pid-Gate - "none" bleibt
	 * als stiller Zweitname, damit eine Zeile, die sich an dem anderen
	 * orientiert, auch hier geht.
	 */
	if (!strcmp(argv[2], "clear") || !strcmp(argv[2], "reset") ||
	    !strcmp(argv[2], "defaults") || !strcmp(argv[2], "none")) {
		n = frame_set(ifp, dir, FRF_UI | FRF_CONN, 0);
		printf("%s frame %s: %d frame class%s unblocked\n", ifp->name,
		       dir == PF_IN ? "in" : "out", n, n == 1 ? "" : "es");
		return 0;
	}
	if (!strcmp(argv[2], "block"))
		block = 1;
	else if (!strcmp(argv[2], "allow"))
		block = 0;
	else {
		printf("\"%s\": say \"block\", \"allow\" or \"clear\"\n", argv[2]);
		printf("Usage: %s\n", Frame_filter_usage);
		return 1;
	}

	if (argc < 4) {
		printf("Which frame class?\n");
		printf("Usage: %s\n", Frame_filter_usage);
		return 1;
	}

	/* Everything that can be refused is refused before anything is
	 * changed, so a line with one bad word in it does not half apply.
	 */
	for (i = 3; i < argc; i++)
		if (strcmp(argv[i], "any") && frame_class(argv[i]) == 0) {
			printf("\"%s\" is not a frame class - \"ui\" or "
			       "\"conn\"\n", argv[i]);
			printf("Usage: %s\n", Frame_filter_usage);
			return 1;
		}
	/* "any" MEINT BEIDE KLASSEN, und der Reihe nach abgearbeitet wie beim
	 * pid-Gate: "block any allow ui" sagt "nichts als UI".
	 */
	n = 0;
	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "any"))
			n += frame_set(ifp, dir, FRF_UI | FRF_CONN, block);
		else
			n += frame_set(ifp, dir, frame_class(argv[i]), block);
	}
	/* WAS SICH GEAENDERT HAT, UND WENN NICHTS, DANN DAS. */
	if (!n)
		printf("%s frame %s: nothing changed - %s\n", ifp->name,
		       dir == PF_IN ? "in" : "out",
		       block ? "already blocked" : "was not blocked");
	return 0;
}