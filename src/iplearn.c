/* Der Lernfilter fuer IP-Routen und ARP-Eintraege.  Entwurf und Begruendung
 * stehen in TODO.txt unter "LERNFILTER: DIE FORM STEHT".
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "configure.h"
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "ax25.h"
#include "netuser.h"
#include "cmdparse.h"
#include "commands.h"
#include "iplearn.h"

struct iplearn {
	struct iplearn *next;
	int32 prefix;
	int bits;
	int allow;
	int what;                       /* LEARN_ROUTE, LEARN_ARP oder beides */
	uint8 call[AXALEN];
	int hascall;
	/* Der NAME, nicht der Zeiger: ein Interface kann nach dieser Zeile
	 * attached werden, und eine Regel, die dann ins Leere zeigte, waere
	 * schlimmer als eine, die erst spaeter greift.  Dieselbe Ueberlegung
	 * wie bei den Portlisten der Listener.
	 */
	char *ifname;
};

static struct iplearn *Iplearn;

char Iplearn_usage[] =
"ip learn                              the rules, same as \"ip learn list\"\n"
"       ip learn allow|deny <addr>[/<bits>] [call=<call>] [iface=<port>]\n"
"                                          [route|arp]\n"
"       ip learn drop <n>                  remove rule n\n"
"       ip learn test <addr>[/<bits>] [call=<call>] [iface=<port>]\n"
"                                          [route|arp]   try a rule set\n"
"\n"
"  WHAT may go from the traffic into the routing and ARP tables.  Not to be\n"
"  confused with \"ipfilter\", which is a block list for addresses and says\n"
"  whom we talk to at all.\n"
"\n"
"  A rule matches a prefix that lies ENTIRELY WITHIN it, down to a single\n"
"  /32: \"deny 44.130.60.0/24\" also covers an announced /25 or /28.  A\n"
"  LARGER net it does not match - harmless, because one's own, more\n"
"  specific prefix wins in the routing table anyway.\n"
"\n"
"  call=  who claims the address - the sending station, and with INP3 the\n"
"         ANNOUNCING node, not the partner the announcement came through.\n"
"  iface= which port it arrived on.\n"
"  route|arp  without either, both are meant.  \"arp yes, route no\" is the\n"
"         normal case where a net route already stands: a host then needs\n"
"         only the mapping IP -> callsign and no /32 of its own.\n"
"\n"
"  THE MOST SPECIFIC RULE WINS, not the first: longest prefix, and on a tie\n"
"  call= before iface= before no source at all.  With no matching rule the\n"
"  address is learned - an empty list changes nothing.";

/*---------------------------------------------------------------------------*/

/* Nur Ziffern und Punkte - dann ist es eine Adresse und kein Name, und
 * aton() darf ran.  Wie ipfilter.c es haelt, aus demselben Grund.
 */

static int isdottedquad(const char *s)
{
	int c;

	if (s == NULL || *s == '\0') return 0;
	while ((c = *s++) != '\0')
		if ((c < '0' || c > '9') && c != '.') return 0;
	return 1;
}

/*---------------------------------------------------------------------------*/

static int32 maskof(int bits)
{
	return bits ? (int32) (~0U << (32 - bits)) : 0;
}

/* Wie spezifisch ist die Quellenangabe einer Regel?  Nur fuer den Vergleich
 * bei gleich langem Praefix.
 */

static int srcrank(const struct iplearn *p)
{
	if (p->hascall) return 2;
	if (p->ifname)  return 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

int ip_may_learn(int32 target, int bits, int what,
		 const uint8 *call, struct iface *ifp)
{
	struct iplearn *p;
	struct iplearn *best = NULL;

	for (p = Iplearn; p != NULL; p = p->next) {
		/* Liegt das, was gelernt werden soll, ganz in dieser Regel? */
		if (bits < p->bits) continue;
		if ((target & maskof(p->bits)) != p->prefix) continue;
		if (!(p->what & what)) continue;
		if (p->hascall &&
		    (call == NULL || !addreq(p->call, (uint8 *) call)))
			continue;
		if (p->ifname != NULL &&
		    (ifp == NULL || strcmp(ifp->name, p->ifname) != 0))
			continue;
		if (best == NULL || p->bits > best->bits ||
		    (p->bits == best->bits && srcrank(p) > srcrank(best)))
			best = p;
	}
	return best != NULL ? best->allow : 1;
}

/*---------------------------------------------------------------------------*/

static void showlearn(void)
{
	struct iplearn *p;
	char praefix[32];
	char quelle[32];
	char buf[AXBUF];
	int n;

	if (Iplearn == NULL) {
		printf("No rules - everything is learned.  \"ip learn ?\" explains.\n");
		return;
	}
	printf(" #  rule   prefix               source              for\n");
	for (p = Iplearn, n = 1; p != NULL; p = p->next, n++) {
		if (p->bits == 0)
			strcpy(praefix, "any");     /* inet_ntoa(0) sagt "*" */
		else
			sprintf(praefix, "%s/%d", inet_ntoa(p->prefix), p->bits);
		if (p->hascall)
			sprintf(quelle, "call=%s", pax25(buf, p->call));
		else if (p->ifname != NULL)
			sprintf(quelle, "iface=%.20s", p->ifname);
		else
			strcpy(quelle, "-");
		printf("%2d  %-5s  %-20s %-19s %s\n", n,
		       p->allow ? "allow" : "deny", praefix, quelle,
		       p->what == LEARN_BOTH ? "route+arp" :
		       p->what == LEARN_ROUTE ? "route" : "arp");
	}
}

/*---------------------------------------------------------------------------*/

static int addlearn(int argc, char *argv[], int allow)
{
	struct iplearn *p;
	struct iplearn **pp;
	char *bitp;
	int i;
	int bits = 32;
	int32 addr;

	if (argc < 2) {
		printf("%s\n", Iplearn_usage);
		return 1;
	}

	if (!(p = (struct iplearn *) calloc(1, sizeof(struct iplearn)))) {
		printf("%s", Nospace);
		return 1;
	}
	p->allow = allow;
	p->what = LEARN_BOTH;

	/* DAS PRAEFIX DARF FEHLEN.  "deny iface=xnet" ist eine Aussage ueber
	 * eine Quelle und ueber alle Adressen - dann gilt 0.0.0.0/0, und die
	 * Optionen fangen schon bei argv[1] an.
	 */
	i = 1;
	if (strncmp(argv[1], "call=", 5) && strncmp(argv[1], "iface=", 6) &&
	    strcmp(argv[1], "route") && strcmp(argv[1], "arp")) {
		if ((bitp = strchr(argv[1], '/')) != NULL) {
			*bitp++ = '\0';
			bits = atoi(bitp);
			if (bits < 0 || bits > 32) {
				printf("A prefix length from 0 to 32\n");
				free(p);
				return 1;
			}
		}
		if (!strcmp(argv[1], "default") || !strcmp(argv[1], "any")) {
			addr = 0;
			bits = 0;
		} else if (isdottedquad(argv[1])) {
			/* Nicht ueber resolve(): das gibt fuer "0.0.0.0" eine
			 * Null zurueck, und die ist von seinem Fehlerwert nicht
			 * zu unterscheiden - "deny 0.0.0.0/0" waere also als
			 * unbekannter Host abgelehnt worden.
			 */
			addr = aton(argv[1]);
		} else if (!(addr = resolve(argv[1]))) {
			printf(Badhost, argv[1]);
			free(p);
			return 1;
		}
		p->bits = bits;
		p->prefix = addr & maskof(bits);
		i = 2;
	}

	for (; i < argc; i++) {
		if (!strncmp(argv[i], "call=", 5)) {
			if (setcall(p->call, argv[i] + 5)) {
				printf("Invalid callsign \"%s\"\n", argv[i] + 5);
				free(p);
				return 1;
			}
			p->hascall = 1;
			continue;
		}
		if (!strncmp(argv[i], "iface=", 6)) {
			/* Nicht abgelehnt, wenn der Port noch nicht da ist -
			 * gesagt schon, denn bis dahin trifft die Regel nichts.
			 */
			if (if_lookup(argv[i] + 6) == NULL)
				printf("Note: no interface \"%s\" yet - the rule "
				       "matches nothing until there is one\n",
				       argv[i] + 6);
			p->ifname = strdup(argv[i] + 6);
			continue;
		}
		if (!strcmp(argv[i], "route")) { p->what = LEARN_ROUTE; continue; }
		if (!strcmp(argv[i], "arp"))   { p->what = LEARN_ARP;   continue; }
		printf("%s\n", Iplearn_usage);
		free(p->ifname);
		free(p);
		return 1;
	}

	/* Hinten anhaengen, damit die Nummern in der Anzeige stabil bleiben:
	 * fuer die AUSWAHL ist die Reihenfolge ohne Bedeutung - es gewinnt der
	 * spezifischste Eintrag -, fuer "ip learn drop <n>" aber sehr wohl.
	 */
	for (pp = &Iplearn; *pp != NULL; pp = &(*pp)->next)
		;
	*pp = p;
	return 0;
}

/*---------------------------------------------------------------------------*/

static int droplearn(int argc, char *argv[])
{
	struct iplearn *p;
	struct iplearn **pp;
	int n;
	int want;

	if (argc < 2 || (want = atoi(argv[1])) < 1) {
		printf("ip learn drop <n>, die Nummer aus \"ip learn\"\n");
		return 1;
	}
	for (pp = &Iplearn, n = 1; *pp != NULL; pp = &(*pp)->next, n++) {
		if (n == want) {
			p = *pp;
			*pp = p->next;
			free(p->ifname);
			free(p);
			return 0;
		}
	}
	printf("No rule %d\n", want);
	return 1;
}

/*---------------------------------------------------------------------------*/

/* "ip learn test <addr>[/<bits>] [call=..] [iface=..] [route|arp]"
 *
 * Fragt dieselbe Funktion, die der Lernpfad fragt, und sagt das Ergebnis.
 * Ohne das sieht man erst im Betrieb, was eine Liste tut - und dann steht
 * man vor einer Route, die da ist oder fehlt, ohne zu wissen, welche Regel
 * es war.
 */

static int testlearn(int argc, char *argv[])
{
	char *bitp;
	char buf[AXBUF];
	int bits = 32;
	int i;
	int what = LEARN_ROUTE;
	int hascall = 0;
	int32 addr;
	struct iface *ifp = NULL;
	uint8 call[AXALEN];

	if (argc < 2) {
		printf("ip learn test <addr>[/<bits>] [call=<call>] "
		       "[iface=<port>] [route|arp]\n");
		return 1;
	}
	if ((bitp = strchr(argv[1], '/')) != NULL) {
		*bitp++ = '\0';
		bits = atoi(bitp);
	}
	if (isdottedquad(argv[1]))
		addr = aton(argv[1]);
	else if (!(addr = resolve(argv[1]))) {
		printf(Badhost, argv[1]);
		return 1;
	}
	for (i = 2; i < argc; i++) {
		if (!strncmp(argv[i], "call=", 5)) {
			if (setcall(call, argv[i] + 5)) {
				printf("Invalid callsign \"%s\"\n", argv[i] + 5);
				return 1;
			}
			hascall = 1;
			continue;
		}
		if (!strncmp(argv[i], "iface=", 6)) {
			if ((ifp = if_lookup(argv[i] + 6)) == NULL) {
				printf("Interface \"%s\" unknown\n", argv[i] + 6);
				return 1;
			}
			continue;
		}
		if (!strcmp(argv[i], "route")) { what = LEARN_ROUTE; continue; }
		if (!strcmp(argv[i], "arp"))   { what = LEARN_ARP;   continue; }
		printf("ip learn test <addr>[/<bits>] [call=<call>] "
		       "[iface=<port>] [route|arp]\n");
		return 1;
	}

	printf("%s/%d %s from %s over %s: %s\n",
	       inet_ntoa(addr), bits,
	       what == LEARN_ARP ? "arp" : "route",
	       hascall ? pax25(buf, call) : "anyone",
	       ifp != NULL ? ifp->name : "any port",
	       ip_may_learn(addr, bits, what,
			    hascall ? call : NULL, ifp) ? "LEARNED" : "refused");
	return 0;
}

/*---------------------------------------------------------------------------*/

int doiplearn(int argc, char *argv[], void *p)
{
	(void) p;

	if (argc < 2 || !strcmp(argv[1], "list")) {
		showlearn();
		if (argc < 2)
			printf("\"ip learn ?\" explains the syntax.\n");
		return 0;
	}
	if (!strcmp(argv[1], "allow"))
		return addlearn(argc - 1, argv + 1, 1);
	if (!strcmp(argv[1], "deny"))
		return addlearn(argc - 1, argv + 1, 0);
	if (!strcmp(argv[1], "drop"))
		return droplearn(argc - 1, argv + 1);
	if (!strcmp(argv[1], "test"))
		return testlearn(argc - 1, argv + 1);
	printf("%s\n", Iplearn_usage);
	return 1;
}
