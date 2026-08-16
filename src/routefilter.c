/* Who may teach us routes, and whom we tell others about.
 *
 * Until now neither protocol asked.  FlexNet made a peer of every station
 * that sent it a 0xCE frame (flexnet_input), NET/ROM made a neighbour of
 * every station that sent it an L3 frame (route_packet, broadcast_recv), and
 * from that moment their announcements were ours and ours were theirs.  For a
 * node whose users can reach it on the same port as its link partners that is
 * the wrong default, and there was no way to say otherwise.
 *
 * FOUR THINGS ARE DECIDED when a strange station speaks to us, and only two
 * of them are worth a switch:
 *
 *   A  do we learn that HE exists         he has to be in the table, or we
 *                                         look for him later and do not find
 *                                         him.  Not a switch.
 *   B  do we learn what he says about     the switch: "in"
 *      OTHERS
 *   C  do OTHERS hear about him           the switch: "advert"
 *   D  does HE hear about others          the switch: "feed"
 *
 * ALL THREE ARE INDEPENDENT, and they have to be.  It is tempting to tie D to
 * C - hiding a station and not feeding it sound like one wish - but the two
 * cases that want "advert no" want opposite things at D.  A user is hidden
 * and may still have our table, which is the whole point of letting him speak
 * the protocol.  A node at our own site is hidden and MUST have our table,
 * because it is ours and its users route through it.
 *
 * "FEED NO" IS NOT SILENCE.  He still learns that WE are here - FlexNet
 * greets the link with FLEX_INIT, which is our own callsign range, and
 * NET/ROM still broadcasts on the port, with the identifier and no entries.
 * That is the difference from switching the port off with "netrom broadcast
 * disable", and it is a real one: a neighbour who never hears from us cannot
 * route to us either.
 *
 * The four station classes need different combinations:
 *
 *   link partner    in all       advert yes  feed yes   as before
 *   node at our     in all       advert no   feed yes   his routes are useful;
 *   own site                                            upstream we appear as
 *                                                       one system
 *   user            in none      advert no   feed yes   teaches us nothing,
 *                                                       may have our table -
 *                                                       that is why he called
 *   listen only     in all       advert no   feed no    we learn from him and
 *                                                       give nothing back
 *
 * ADVERT IS ABOUT HIM AND NOTHING ELSE.  What lies behind him was settled by
 * "in": with "in all" we accepted those routes and they are ours to pass on,
 * with "in only-him" they never arrived.  So "advert no" leaves out one
 * entry - his - and nothing more.
 *
 * NAMES, NOT POINTERS, for the interface, and the reason is the one that
 * portlist_set() gives: net.rc is read from the top and nothing says the
 * attach lines come first, so a filter resolved at configuration time would
 * quietly miss every port attached after it.
 *
 * THE MOST SPECIFIC ENTRY WINS: the callsign, else the interface, else
 * "default", else what the node always did.  A configuration that says
 * nothing behaves exactly as it did before - which is why the default is the
 * old behaviour and not the safe one.  One line changes that:
 *
 *      flexnet filter default in only-him advert no
 *
 * and from then on only stations named one by one are full partners.
 */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "iface.h"
#include "ax25.h"
#include "routefilter.h"

/* Exactly one of the three shapes: the default, an interface by name, or a
 * callsign.
 */

enum rf_kind {
	RFK_DEFAULT,
	RFK_PORT,
	RFK_CALL
};

struct rfentry {
	struct rfentry *next;
	enum rf_proto proto;
	enum rf_kind kind;
	char *ifname;                   /* RFK_PORT */
	uint8 call[AXALEN];             /* RFK_CALL */
	enum rf_in in;
	int advert;
	int feed;
};

static struct rfentry *Rfentries;

/*---------------------------------------------------------------------------*/

const char *rf_in_name(enum rf_in in)
{
	switch (in) {
	case RF_IN_ONLY_HIM:    return "only-him";
	case RF_IN_NONE:        return "none";
	default:                return "all";
	}
}

/*---------------------------------------------------------------------------*/

static struct rfentry *rf_find(enum rf_proto proto, enum rf_kind kind,
			       const char *ifname, const uint8 *call)
{
	struct rfentry *rp;

	for (rp = Rfentries; rp; rp = rp->next) {
		if (rp->proto != proto || rp->kind != kind)
			continue;
		switch (kind) {
		case RFK_DEFAULT:
			return rp;
		case RFK_PORT:
			if (!strcmp(rp->ifname, ifname))
				return rp;
			break;
		case RFK_CALL:
			if (addreq(rp->call, call))
				return rp;
			break;
		}
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

/* The lookup, and the only thing the protocols call.  Callsign, interface,
 * default, and finally what the node did before anybody configured anything.
 */

static const struct rfentry *rf_lookup(enum rf_proto proto, const uint8 *call,
				       const struct iface *ifp)
{
	struct rfentry *rp;

	if (call && (rp = rf_find(proto, RFK_CALL, 0, call)))
		return rp;
	if (ifp && ifp->name && (rp = rf_find(proto, RFK_PORT, ifp->name, 0)))
		return rp;
	return rf_find(proto, RFK_DEFAULT, 0, 0);
}

/*---------------------------------------------------------------------------*/

enum rf_in rf_in(enum rf_proto proto, const uint8 *call, const struct iface *ifp)
{
	const struct rfentry *rp = rf_lookup(proto, call, ifp);

	return rp ? rp->in : RF_IN_ALL;
}

/*---------------------------------------------------------------------------*/

int rf_advert(enum rf_proto proto, const uint8 *call, const struct iface *ifp)
{
	const struct rfentry *rp = rf_lookup(proto, call, ifp);

	return rp ? rp->advert : 1;
}

/*---------------------------------------------------------------------------*/

int rf_feed(enum rf_proto proto, const uint8 *call, const struct iface *ifp)
{
	const struct rfentry *rp = rf_lookup(proto, call, ifp);

	return rp ? rp->feed : 1;
}

/*---------------------------------------------------------------------------*/

/* Is there any filter at all for this protocol?  The announcing loops ask
 * before they start, so that a node without filters walks its table exactly
 * as it did before instead of asking a question per entry.
 */

int rf_configured(enum rf_proto proto)
{
	struct rfentry *rp;

	for (rp = Rfentries; rp; rp = rp->next)
		if (rp->proto == proto)
			return 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

static void rf_list(enum rf_proto proto)
{
	char buf[AXBUF];
	struct rfentry *rp;
	int seen = 0;

	for (rp = Rfentries; rp; rp = rp->next) {
		if (rp->proto != proto)
			continue;
		if (!seen++)
			printf("Station           In         Advert  Feed\n");
		switch (rp->kind) {
		case RFK_DEFAULT:
			printf("%-16s  ", "default");
			break;
		case RFK_PORT:
			printf("port=%-11s  ", rp->ifname);
			break;
		case RFK_CALL:
			printf("%-16s  ", pax25(buf, rp->call));
			break;
		}
		printf("%-9s  %-6s  %s\n", rf_in_name(rp->in),
		       rp->advert ? "yes" : "no", rp->feed ? "yes" : "no");
	}
	if (!seen)
		printf("No filters - every station is a full partner\n");
}

/*---------------------------------------------------------------------------*/

static void rf_delete(struct rfentry *entry)
{
	struct rfentry *rp;
	struct rfentry **rpp;

	for (rpp = &Rfentries; (rp = *rpp); rpp = &rp->next)
		if (rp == entry) {
			*rpp = rp->next;
			free(rp->ifname);
			free(rp);
			return;
		}
}

/*---------------------------------------------------------------------------*/

/*      <proto> filter
 *      <proto> filter default | port=<name> | <call>
 *                     [in <mode>] [advert yes|no] [feed yes|no]
 *      <proto> filter --delete default | port=<name> | <call>
 *
 * "port=" rather than a bare name, because an interface may perfectly well be
 * called "hf1" and so may a station: setcall() accepts both and there would
 * be no telling which was meant.  The same "port=" as in "listen".
 */

int rf_cmd(enum rf_proto proto, int argc, char *argv[], void *p)
{
	char *spec = 0;
	enum rf_in in = RF_IN_ALL;
	enum rf_kind kind = RFK_DEFAULT;
	int advert = 1;
	int advert_set = 0;
	int del = 0;
	int feed = 1;
	int feed_set = 0;
	int i;
	int in_set = 0;
	struct rfentry *rp;
	uint8 call[AXALEN];

	memset(call, 0, sizeof(call));

	for (i = 1; i < argc; i++) {
		char *cp = argv[i];

		if (!strcmp(cp, "--delete")) { del = 1; continue; }
		if (!strcmp(cp, "in")) {
			if (++i >= argc) {
				printf("\"in\" without a mode\n");
				return 1;
			}
			if (!strcmp(argv[i], "all"))
				in = RF_IN_ALL;
			else if (!strcmp(argv[i], "only-him"))
				in = RF_IN_ONLY_HIM;
			else if (!strcmp(argv[i], "none"))
				in = RF_IN_NONE;
			else {
				printf("In must be all, only-him or none\n");
				return 1;
			}
			in_set = 1;
			continue;
		}
		if (!strcmp(cp, "advert") || !strcmp(cp, "feed")) {
			int yes;

			if (++i >= argc) {
				printf("\"%s\" without yes or no\n", cp);
				return 1;
			}
			if (!strcmp(argv[i], "yes"))
				yes = 1;
			else if (!strcmp(argv[i], "no"))
				yes = 0;
			else {
				printf("%c%s must be yes or no\n",
				       Xtoupper(*cp), cp + 1);
				return 1;
			}
			if (*cp == 'a') {
				advert = yes;
				advert_set = 1;
			} else {
				feed = yes;
				feed_set = 1;
			}
			continue;
		}
		if (!strncmp(cp, "--", 2)) {
			printf("Unknown option \"%s\"\n", cp);
			return 1;
		}
		if (spec) {
			printf("Only one station per line\n");
			return 1;
		}
		spec = cp;
	}

	if (!spec) {
		if (del) {
			printf("Nothing to delete\n");
			return 1;
		}
		rf_list(proto);
		return 0;
	}

	/* Everything that can be refused is refused before anything is
	 * created or changed.
	 */
	if (!strcmp(spec, "default")) {
		kind = RFK_DEFAULT;
	} else if (!strncmp(spec, "port=", 5)) {
		if (!spec[5]) {
			printf("\"port=\" without a name\n");
			return 1;
		}
		kind = RFK_PORT;
	} else {
		if (setcall(call, spec)) {
			printf("Invalid call \"%s\"\n", spec);
			return 1;
		}
		kind = RFK_CALL;
	}

	/* NET/ROM announces per broadcast entry, one UI frame for everybody on
	 * the port - there is no such thing as announcing to one station.  A
	 * "feed" written against a callsign would look as though it worked.
	 */
	if (proto == RF_NETROM && kind == RFK_CALL && feed_set) {
		printf("NET/ROM announces per port, not per station - "
		       "write \"feed\" against port=<name> or default\n");
		return 1;
	}

	if (del) {
		if (in_set || advert_set || feed_set) {
			printf("--delete takes no settings\n");
			return 1;
		}
		if (!(rp = rf_find(proto, kind, spec + 5, call))) {
			printf("No filter for \"%s\"\n", spec);
			return 1;
		}
		rf_delete(rp);
		return 0;
	}

	if (!(rp = rf_find(proto, kind, spec + 5, call))) {
		if (!(rp = (struct rfentry *) calloc(1, sizeof(struct rfentry)))) {
			printf("%s", Nospace);
			return 1;
		}
		if (kind == RFK_PORT && !(rp->ifname = strdup(spec + 5))) {
			free(rp);
			printf("%s", Nospace);
			return 1;
		}
		rp->proto = proto;
		rp->kind = kind;
		addrcp(rp->call, call);
		/* A new entry that says nothing is the old behaviour, not a
		 * half-written rule: naming a station alone changes nothing.
		 */
		rp->in = RF_IN_ALL;
		rp->advert = 1;
		rp->feed = 1;
		rp->next = Rfentries;
		Rfentries = rp;
	}
	if (in_set)
		rp->in = in;
	if (advert_set)
		rp->advert = advert;
	if (feed_set)
		rp->feed = feed;
	return 0;
}
