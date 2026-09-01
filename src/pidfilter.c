/* Which protocols may cross a port, and in which direction.
 *
 * The route filter (routefilter.c) answers "whose routes do we take" and
 * "who hears about whom".  It works INSIDE a protocol: the frame is parsed,
 * the peer exists, we go on talking to him, and only what he says is
 * disregarded.  There was no way to say the blunter thing - that a protocol
 * has no business on a port at all.
 *
 * That is this gate, and it is one question and not several, because there is
 * exactly one place where an AX.25 frame is handed to a protocol:
 *
 *      struct axlink Axlink[] = {              config.c
 *              { PID_IP, axip }, { PID_ARP, axarp },
 *              { PID_FLEXNET, flexnet_input }, { PID_NETROM, axnr }, ...
 *
 * used by ax_recv() for UI frames and by lapb.c for the connected case.  A
 * gate in front of that lookup covers both kinds of frame in one grip, which
 * is why this is per PROTOCOL ID and not per protocol: "no NET/ROM from this
 * port" and "no IP from this port, neither in UI nor in I" are then the same
 * sentence.
 *
 * THE TWO DEPTHS, and they are two rungs of one ladder rather than two keys:
 *
 *   filter ... in none    the frame is parsed, the peer exists, we speak to
 *                         him - we only learn nothing from him
 *   pid ... block         the frame does not reach the protocol; there is no
 *                         peer and no state
 *
 * The route filter can also name a single callsign, which a port gate cannot;
 * so neither replaces the other.  ORDER MATTERS when both are set: the gate
 * acts BEFORE parsing, the filter after.  Look here first, or you will hunt
 * for the fault in a filter while the gate has already dropped the frame.
 *
 * WHAT IT IS NOT ABOUT: traffic we merely repeat.  A frame that is not
 * addressed to us is digipeated in ax_recv() before anybody has looked at a
 * protocol id - there is no pid at that point, only an address to advance -
 * and it stays that way.  This gate is about frames that are ours: addressed
 * to us, or sent by us.
 *
 * Nothing is blocked until somebody says so, as everywhere else here, so a
 * port that was never configured carries what it always carried.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "cmdparse.h"
#include "pidfilter.h"

/* The one table of protocol ids in the node, and it is a table rather than a
 * switch in each place because there are four kinds of place: the gate below,
 * "pid=" on a listen line, the same word on the service socket, and every
 * display that has a protocol id to show.  They used to answer differently -
 * some took a number only, and some printed one where a name would do.
 *
 * "text" for PID_NO_L3 because that is what the rest of the node calls it:
 * "listen" writes pid=text and its display says the same.
 */

static const struct pidname {
	int pid;
	const char *name;
	const char *what;
} Pidnames[] = {
	{ PID_X25,        "x25",       "CCITT X.25 PLP" },
	/* The numbers rather than PID_VJCOMP / PID_VJUNCOMP: those live behind
	 * AX25_VJCOMP, and a frame carrying 0x06 arrives whether or not this
	 * node was built to understand it - so the name has to exist either
	 * way, or the gate could not be told to drop it.
	 */
	{ 0x06,           "vjcomp",    "TCP/IP, Van Jacobson compressed (RFC 1144)" },
	{ 0x07,           "vjuncomp",  "TCP/IP, Van Jacobson uncompressed" },
	{ PID_SEGMENT,    "segment",   "segmentation fragment" },
	{ PID_FLEXTALK,   "flextalk",  "FlexTalk - voice over AX.25" },
	{ PID_TEXNET,     "texnet",    "TEXNET datagram protocol" },
	{ PID_LQ,         "lq",        "link quality protocol" },
	{ PID_APPLETALK,  "appletalk", "Appletalk" },
	{ PID_APPLEARP,   "applearp",  "Appletalk ARP" },
	{ PID_IP,         "ip",        "ARPA Internet Protocol" },
	{ PID_ARP,        "arp",       "ARPA Address Resolution Protocol" },
	{ PID_FLEXNET,    "flexnet",   "FlexNet" },
	{ PID_NETROM,     "netrom",    "NET/ROM" },
	{ PID_NO_L3,      "text",      "no level 3 protocol - a plain connection" },
	{ 0xff,           "escape",    "the next octet carries the protocol" },
	{ 0,              NULL,        NULL }
};

/*---------------------------------------------------------------------------*/

/* The name, or the number written out when there is none.  buf is only used
 * in the second case, so the caller has to provide one either way.
 */

const char *pid_name(int pid, char *buf, size_t buflen)
{
	const struct pidname *pn;

	for (pn = Pidnames; pn->name; pn++)
		if (pn->pid == pid)
			return pn->name;
	snprintf(buf, buflen, "0x%02x", pid & 0xff);
	return buf;
}

/*---------------------------------------------------------------------------*/

/* A name, or a number in any of the usual notations - 0xcf, 0207, 207.  -1 if
 * it is neither.
 *
 * Not atoi(): a word that is not a number would come out as 0, which is a
 * perfectly legal protocol id, so a typing mistake would not be refused but
 * would name a protocol - and not the one that was meant.
 */

int pid_number(const char *word)
{
	char *end;
	const struct pidname *pn;
	long val;

	if (word == NULL || !*word)
		return -1;
	for (pn = Pidnames; pn->name; pn++)
		if (!stricmp((char *) word, (char *) pn->name))
			return pn->pid;
	val = strtol(word, &end, 0);
	if (*end || val < 0 || val > 255)
		return -1;
	return (int) val;
}

/*---------------------------------------------------------------------------*/

/* "ax25 pid-info [<protocol>]" - the table, or one entry of it.
 *
 * The numbers are not ours to invent: they are AX.25 2.2 figure 3.1, and a
 * number that stands for nothing there is still legal on the wire.  So an
 * unknown one is answered with what CAN be said about it rather than with
 * "unknown", and that is the layer 3 rule from the same figure.
 */

int pid_info(int argc, char *argv[], void *p)
{
	const struct pidname *pn;
	int pid;

	if (argc < 2) {
		printf(" Pid   Name       Carries\n");
		for (pn = Pidnames; pn->name; pn++)
			printf("0x%02x   %-9s  %s\n", pn->pid, pn->name,
			       pn->what);
		printf("\nAny other value with bits 5-4 set to 01 or 10 means "
		       "\"AX.25 layer 3\nimplemented\"; the rest are not "
		       "assigned.  These names are what \"pid=\" and\n"
		       "\"ifconfig <iface> pid\" accept.\n");
		return 0;
	}

	if ((pid = pid_number(argv[1])) < 0) {
		printf("\"%s\" is not a protocol name and not a number "
		       "0..255\n", argv[1]);
		return 1;
	}
	for (pn = Pidnames; pn->name; pn++)
		if (pn->pid == pid) {
			printf("0x%02x   %-9s  %s\n", pn->pid, pn->name,
			       pn->what);
			return 0;
		}
	printf("0x%02x   -          %s\n", pid,
	       (pid & 0x30) == 0x10 || (pid & 0x30) == 0x20
	       ? "no name here; bits 5-4 say \"AX.25 layer 3 implemented\""
	       : "no name here, and not assigned by AX.25 2.2");
	return 0;
}

/*---------------------------------------------------------------------------*/

/* The question the send and receive paths ask.  ifp may be null - the loopback
 * and the netrom pseudo interface both hand frames about without one - and
 * then nothing is blocked.
 */

int pid_blocked(const struct iface *ifp, int dir, int pid)
{
	if (!ifp || pid < 0 || pid > 255 || dir < 0 || dir >= PF_NDIR)
		return 0;
	if (!ifp->pidblocked[dir])              /* the usual case, one test */
		return 0;
	return (ifp->pidblock[dir][pid >> 5] >> (pid & 31)) & 1;
}

/*---------------------------------------------------------------------------*/

/* 1, wenn sich wirklich etwas geaendert hat.  Der Aufrufer sagt es dem
 * Sysop: ein "allow" auf einen PID, der gar nicht gesperrt war, tat bisher
 * nichts und SAGTE nichts - und wer das eingibt, glaubt danach, es sei
 * etwas passiert (Thomas).
 */

static int pid_set(struct iface *ifp, int dir, int pid, int block)
{
	uint32 bit = ((uint32) 1) << (pid & 31);
	uint32 *word = &ifp->pidblock[dir][pid >> 5];

	if (block) {
		if (*word & bit)
			return 0;
		*word |= bit;
		ifp->pidblocked[dir]++;
		return 1;
	} else {
		if (!(*word & bit))
			return 0;
		*word &= ~bit;
		ifp->pidblocked[dir]--;
		return 1;
	}
}

/*---------------------------------------------------------------------------*/

static void pid_show(const struct iface *ifp, int dir, int indent)
{
	char buf[16];
	int n = 0;
	int pid;

	/* In "verbose" gehoert die Zeile in den Block und nicht an den Rand -
	 * und der Portname steht dort schon in der Ueberschrift.
	 */
	if (indent)
		printf("           pid %-3s ", dir == PF_IN ? "in" : "out");
	else
		printf("%s pid %-3s ", ifp->name, dir == PF_IN ? "in" : "out");
	if (!ifp->pidblocked[dir]) {
		printf("blocked: (nothing)\n");
		return;
	}
	/* ANDERSHERUM AUFZAEHLEN, SOBALD MEHR GESPERRT ALS ERLAUBT IST.
	 * Nach "block any, allow text" waeren es sonst 255 Namen, und der
	 * eine, auf den es ankommt, stuende irgendwo darin.
	 */
	if (ifp->pidblocked[dir] > 128) {
		if (ifp->pidblocked[dir] >= 256) {
			printf("blocked: EVERYTHING\n");
			return;
		}
		printf("blocked: everything except");
		for (pid = 0; pid < 256; pid++)
			if (!pid_blocked(ifp, dir, pid))
				printf("%s %s", n++ ? "," : "",
				       pid_name(pid, buf, sizeof(buf)));
		putchar('\n');
		return;
	}
	printf("blocked:");
	for (pid = 0; pid < 256; pid++)
		if (pid_blocked(ifp, dir, pid))
			printf("%s %s", n++ ? "," : "",
			       pid_name(pid, buf, sizeof(buf)));
	putchar('\n');
}

/*---------------------------------------------------------------------------*/

void pid_show_verbose(const struct iface *ifp)
{
	if (ifp == NULL)
		return;
	if (ifp->pidblocked[PF_IN])
		pid_show(ifp, PF_IN, 1);
	if (ifp->pidblocked[PF_OUT])
		pid_show(ifp, PF_OUT, 1);
}

/*---------------------------------------------------------------------------*/

char Pid_usage[] =
	"ifconfig <iface> pid                       what is blocked here\n"
	"       ifconfig <iface> pid in|out block <protocol>...\n"
	"       ifconfig <iface> pid in|out allow <protocol>...\n"
	"       ifconfig <iface> pid in|out clear           allow everything again\n"
	"\n"
	"  <protocol> is a name or a number: ip arp netrom flexnet text vjcomp\n"
	"  vjuncomp segment x25 flextalk texnet lq appletalk applearp, or 0xcf,\n"
	"  or \"any\" for all 256.  \"block any\" then \"allow <protocol>\" is how\n"
	"  one says \"nothing but this\":\n"
	"\n"
	"       ifconfig xnet pid in block any\n"
	"       ifconfig xnet pid in allow text\n"
	"\n"
	"  \"reset\", \"defaults\" and the older \"none\" all mean \"clear\".\n"
	"\n"
	"  \"in\" is what may reach a protocol here, \"out\" what we may send here.\n"
	"  Both are about frames that are ours - traffic we merely digipeat is\n"
	"  untouched.  The gate acts before the frame is parsed, so it comes\n"
	"  BEFORE \"netrom filter\" and \"flexnet filter\", which act after.";

/*---------------------------------------------------------------------------*/

/* ifconfig <iface> pid [in|out [block|allow <protocol>...]]
 *
 * This one takes a list, so doifconfig() hands it the rest of the line
 * instead of the usual name/value pair - see the note there.
 */

int ifpid(int argc, char *argv[], void *p)
{
	int block;
	int dir;
	int i;
	int n;
	int pid;
	struct iface *ifp = (struct iface *) p;

	if (argc < 2 || !strcmp(argv[1], "?") || !strcmp(argv[1], "help")) {
		if (argc >= 2) {
			printf("Usage: %s\n", Pid_usage);
			return 0;
		}
		pid_show(ifp, PF_IN, 0);
		pid_show(ifp, PF_OUT, 0);
		return 0;
	}

	if (!strcmp(argv[1], "in"))
		dir = PF_IN;
	else if (!strcmp(argv[1], "out"))
		dir = PF_OUT;
	else {
		printf("\"%s\": the direction is \"in\" or \"out\"\n", argv[1]);
		printf("Usage: %s\n", Pid_usage);
		return 1;
	}

	if (argc < 3) {
		pid_show(ifp, dir, 0);
		return 0;
	}

	/* ZURUECKSETZEN.  "none" las sich wie das Gegenteil dessen, was es tut
	 * - "keine PID herein" statt "alles wieder erlauben" (Thomas).  Die
	 * sprechenden Namen sind jetzt clear/reset/defaults; "none" bleibt
	 * als stiller Zweitname, damit bestehende net.rc nicht brechen.
	 */
	if (!strcmp(argv[2], "clear") || !strcmp(argv[2], "reset") ||
	    !strcmp(argv[2], "defaults") || !strcmp(argv[2], "none")) {
		int n = 0;

		for (pid = 0; pid < 256; pid++)
			n += pid_set(ifp, dir, pid, 0);
		printf("%s pid %s: %d protocol%s unblocked\n", ifp->name,
		       dir == PF_IN ? "in" : "out", n, n == 1 ? "" : "s");
		return 0;
	}
	if (!strcmp(argv[2], "block"))
		block = 1;
	else if (!strcmp(argv[2], "allow"))
		block = 0;
	else {
		printf("\"%s\": say \"block\", \"allow\" or \"none\"\n", argv[2]);
		printf("Usage: %s\n", Pid_usage);
		return 1;
	}

	if (argc < 4) {
		printf("Which protocol?\n");
		printf("Usage: %s\n", Pid_usage);
		return 1;
	}

	/* Everything that can be refused is refused before anything is
	 * changed, so a line with one bad word in it does not half apply.
	 */
	for (i = 3; i < argc; i++)
		if (strcmp(argv[i], "any") && pid_number(argv[i]) < 0) {
			printf("\"%s\" is not a protocol name and not a "
			       "number 0..255\n", argv[i]);
			printf("Usage: %s\n", Pid_usage);
			return 1;
		}
	/* "any" MEINT ALLE 256, und mehr braucht es dafuer nicht: gespeichert
	 * ist eine Bitkarte, nicht eine Liste.  Damit faellt
	 * "erst alles verbieten, dann Ausnahmen" von selbst heraus -
	 *
	 *     ifconfig xnet pid in block any
	 *     ifconfig xnet pid in allow text
	 *
	 * denn "allow" heisst ohnehin schon "Bit loeschen".  (Ich hatte im
	 * TODO vermutet, das aendere die Bedeutung von allow und verlange
	 * eine andere Datenstruktur - das war falsch, weil ich eine Liste
	 * angenommen hatte.)
	 *
	 * Der Reihenfolge nach abgearbeitet, nicht als Sonderfall vorweg: so
	 * tut "block any allow" nichts Ueberraschendes, und wer "allow any
	 * block netrom" schreibt, bekommt genau das.
	 */
	n = 0;
	for (i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "any")) {
			for (pid = 0; pid < 256; pid++)
				n += pid_set(ifp, dir, pid, block);
		} else
			n += pid_set(ifp, dir, pid_number(argv[i]), block);
	}
	/* WAS SICH GEAENDERT HAT, UND WENN NICHTS, DANN DAS.  Ein "allow" auf
	 * etwas, das gar nicht gesperrt war, sah bisher wie ein Erfolg aus.
	 */
	if (!n)
		printf("%s pid %s: nothing changed - %s\n", ifp->name,
		       dir == PF_IN ? "in" : "out",
		       block ? "already blocked" : "was not blocked");
	return 0;
}
