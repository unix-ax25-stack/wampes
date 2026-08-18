/* @(#) $Id: flexnet.c,v 1.14 2000/03/04 18:31:13 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "flexnet.h"
#include "routefilter.h"
#include "cmdparse.h"
#include "commands.h"

#define DEFAULTDELAY    600     /* Default delay (1 minute) */
#define FLEXBUF         13      /* Size of flexnet call in ascii */
#define FLEXLEN         (AXALEN+1)      /* Length of flexnet call */
#define LENPOLL         201     /* Total length of poll packet */
#define LENROUT         256     /* Maximum length of rout packet */
#define MAXDELAY        3000    /* Maximum delay (5 minutes) */
#define MAXHOPCNT       30      /* Maximum allowed hop count */
#define MAXQCALLS       50      /* Maximum # of calls in qury packet */
#define POLLINTERVAL    (5*60*1000L)    /* Time between polls (5 minutes) */

#define FLEX_INIT       '0'     /* Link initialization */
#define FLEX_RPRT       '1'     /* Poll answer */
#define FLEX_POLL       '2'     /* Poll */
#define FLEX_ROUT       '3'     /* Routing information */
#define FLEX_QURY       '6'     /* Path query */
#define FLEX_RSLT       '7'     /* Query result */

enum e_token {
	NOTOKEN,                /* I do not have token */
	WANTTOKEN,              /* I do not have token, but did request it */
	HAVETOKEN               /* I have token */
};

struct peer {
	struct peer *next;      /* Linked-list pointer */
	uint8 call[FLEXLEN];    /* Flexnet call */
	int remdelay;           /* Delay measured remotely (100ms steps) */
	int locdelay;           /* Delay measured locally (100ms steps) */
	double delay;           /* Smoothed delay (100ms units) */
	int32 lastpolltime;     /* Time of last poll (ms) */
	struct ax25_cb *axp;    /* AX.25 control block pointer */
	int id;                 /* Last AX.25 control block id */
	enum e_token token;     /* Token state */
	int permanent;          /* True if peer was created manually */
	struct iface *iface;    /* Port he was last heard on.  A peer is a
				 * callsign and nothing here says which port it
				 * is on; this is what a filter written per
				 * port has to match against. */
};

struct quality {
	struct quality *next;   /* Linked-list pointer */
	struct peer *peer;      /* Peer pointer */
	int delay;              /* Delay via this peer */
	int lastdelay;          /* Last delay reported to this peer */
};

struct dest {
	struct dest *next;      /* Linked-list pointer */
	uint8 call[FLEXLEN];    /* Flexnet call */
	struct quality *qualities;      /* List of qualities */
};

struct querypkt {
	int hopcnt;             /* Hop count */
	int qsonum;             /* QSO number */
	uint8 srccall[AXALEN];  /* Source call in AX.25 format */
	uint8 destcall[AXALEN]; /* Destination call in AX.25 format */
	int numcalls;           /* Number of calls in bufs array */
	char bufs[MAXQCALLS][AXBUF];    /* Calls in ascii format */
};

static struct dest *Dests;      /* List of destinations */
static struct peer *Peers;      /* List of peers */
static struct timer Polltimer;  /* Poll timer */

/* Our own SSID range, the way FlexNet knows a station: a callsign and a span
 * of SSIDs.  The two halves travel apart - the start is the callsign our links
 * run under, the stop is the single number in FLEX_INIT - so both are kept
 * here and both are the NODE's, not the port's.  XNET writes the same two down
 * as "my call db0blo-4" and "ro fl pa ssid 15".
 *
 * -1 means nothing was configured, and then everything behaves exactly as it
 * did: the source comes from whichever port the route picked and the stop is
 * that same SSID, so each port announces a station of its own.
 */
static int Flex_start = -1;
static int Flex_stop = -1;

/*---------------------------------------------------------------------------*/

/* The callsign our FlexNet links run under, or nothing if none was set.  The
 * base is Mycall's: one node, one callsign - a different one would be a
 * station we do not answer to.
 */

static int flex_mycall(uint8 *call)
{
	if (Flex_start < 0)
		return 0;
	addrcp(call, Mycall);
	call[ALEN] = (uint8) (0x60 | (Flex_start << 1));
	return 1;
}

/*---------------------------------------------------------------------------*/

#define iround(d)               ((int) ((d) + 0.5))

/*---------------------------------------------------------------------------*/

#define flexaddrcp(to, from)    memcpy(to, from, FLEXLEN)

/*---------------------------------------------------------------------------*/

static int flexsetcall(uint8 *call, const char *ascii)
{
	if (setcall(call, ascii)) {
		printf("Invalid call \"%s\"\n", ascii);
		return -1;
	}
	call[ALEN + 1] = call[ALEN];
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct peer *find_peer(const uint8 *call)
{
	struct peer *pp;

	for (pp = Peers; pp; pp = pp->next)
		if (addreq(pp->call, call))
			return pp;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Is this callsign a FlexNet link partner?  Asked before a text service is
 * started on an incoming link: a partner called to speak L3, not to be
 * greeted.  See axserv_connected().
 */

int flexnet_is_peer(const uint8 *call)
{
	return find_peer(call) != NULL;
}

/*---------------------------------------------------------------------------*/

/* What the filter says about this peer.  The port is the one he was last
 * heard on, so that "filter port=useraccess2m" covers everybody who turns up
 * there without having to be named one by one.
 */

static enum rf_in peer_in(const struct peer *pp)
{
	return rf_in(RF_FLEXNET, pp->call, pp->iface);
}

static int peer_advert(const struct peer *pp)
{
	return rf_advert(RF_FLEXNET, pp->call, pp->iface);
}

static int peer_feed(const struct peer *pp)
{
	return rf_feed(RF_FLEXNET, pp->call, pp->iface);
}

/*---------------------------------------------------------------------------*/

static struct dest *find_dest(const uint8 *call)
{
	struct dest *pd;

	for (pd = Dests; pd; pd = pd->next)
		if (addreq(pd->call, call))
			return pd;
	return 0;
}

/*---------------------------------------------------------------------------*/

static struct quality *find_quality(struct dest *pd, struct peer *pp)
{
	struct quality *pq;

	for (pq = pd->qualities; pq && pq->peer != pp; pq = pq->next) ;
	if (!pq) {
		pq = (struct quality *) calloc(1, sizeof(struct quality));
		if (pq) {
			pq->peer = pp;
			pq->next = pd->qualities;
			pd->qualities = pq;
		}
	}
	return pq;
}

/*---------------------------------------------------------------------------*/

static struct quality *find_best_quality(const struct dest *pd)
{

	struct quality *pqbest = 0;
	struct quality *pq;

	for (pq = pd->qualities; pq; pq = pq->next)
		if (pq->delay && (!pqbest || pqbest->delay >= pq->delay))
			pqbest = pq;
	return pqbest;
}

/*---------------------------------------------------------------------------*/

static void update_axroute(const struct dest *pd)
{

	int dest_is_peer;
	struct ax_route *rpd;
	struct ax_route *rpp;
	struct ax_route *rp;
	struct quality *pq;
	uint8 call[AXALEN];

	if (!(pq = find_best_quality(pd)))
		return;
	if (!(rpp = ax_routeptr(pq->peer->call, 0)))
		return;
	dest_is_peer = addreq(pd->call, pq->peer->call);
	for (addrcp(call, pd->call);; call[ALEN] += 2) {
		if (!(rpd = ax_routeptr(call, 1)))
			return;
		for (rp = rpp; rp && rp != rpd; rp = rp->digi) ;
		if (!rp && !rpd->perm) {
			if (dest_is_peer) {
				rpd->digi = rpp->digi;
				rpd->ifp = rpp->ifp;
			} else {
				rpd->digi = rpp;
				rpd->ifp = 0;
			}
			for (; rpd; rpd = rpd->digi)
				rpd->time = secclock();
		}
		if ((call[ALEN] & SSID) >= (pd->call[ALEN + 1] & SSID))
			break;
	}
}

/*---------------------------------------------------------------------------*/

static void delete_unreachables(void)
{

	struct dest *pd;
	struct dest **ppd;
	struct quality *pq;
	struct quality **ppn;

	for (ppd = &Dests; (pd = *ppd);) {
		for (ppn = &pd->qualities; (pq = *ppn);) {
			if (!pq->delay && !pq->lastdelay) {
				*ppn = pq->next;
				free(pq);
			} else {
				ppn = &pq->next;
			}
		}
		if (!pd->qualities) {
			*ppd = pd->next;
			free(pd);
		} else {
			ppd = &pd->next;
		}
	}
}

/*---------------------------------------------------------------------------*/

static void update_delay(const uint8 *destcall, struct peer *pp, int delay)
{

	struct dest *pd;
	struct quality *pq;

	if (!(pd = find_dest(destcall))) {
		if (!delay)
			return;
		if (!(pd = (struct dest *) calloc(1, sizeof(struct dest))))
			 return;
		pd->next = Dests;
		Dests = pd;
		flexaddrcp(pd->call, destcall);
	}
	pq = find_quality(pd, pp);
	if (pq)
		pq->delay = delay;
}

/*---------------------------------------------------------------------------*/

static void send_init(const struct peer *pp)
{

	struct mbuf *bp;
	int stop;
	uint8 *cp;

	/* The one number FlexNet has room for: the highest SSID we serve.  The
	 * lowest is not in the frame at all - the far end takes it from the
	 * callsign this link runs under.  Unconfigured we send that same SSID,
	 * which says "I serve exactly myself" and is what the node did before
	 * there was anything to set.
	 */
	stop = Flex_stop >= 0 ? Flex_stop
			      : ((pp->axp->hdr.source[ALEN] & SSID) >> 1);

	if ((bp = alloc_mbuf(6))) {
		cp = bp->data;
		*cp++ = FLEX_INIT;
		*cp++ = '0' + stop;
		*cp++ = ' ';
		*cp++ = ' ';
		*cp++ = ' ' + 1;
		*cp++ = '\r';
		bp->cnt = 6;
		send_ax25(pp->axp, &bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

/* The round trip measurement: a 201 byte packet, every five minutes, and the
 * answer to it is what we time.
 *
 * ONLY TO PEERS SOMEBODY ENTERED.  Until now every station that sent us one
 * 0xCE frame became a peer and was polled at once - measured: FLEX_INIT and
 * 201 bytes back within a second, and five minutes later a DISC, because the
 * user had not answered a poll he never asked for.  That is a lot of air time
 * spent on somebody who is not a link partner, and the measurement is worth
 * nothing to us: we do not route through our users.  So the poll goes to
 * peers from "flexnet link add" and to nobody else.
 */

static void send_poll(struct peer *pp)
{

	int i;
	struct mbuf *bp;
	uint8 *cp;

	if (!pp->permanent)
		return;
	if ((bp = alloc_mbuf(LENPOLL))) {
		pp->lastpolltime = msclock();
		cp = bp->data;
		*cp++ = FLEX_POLL;
		for (i = 0; i < LENPOLL - 2; i++)
			*cp++ = ' ';
		*cp++ = '\r';
		bp->cnt = LENPOLL;
		send_ax25(pp->axp, &bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static void clear_all_via_peer(struct peer *pp, int including_peer)
{

	struct dest *pd;
	struct quality *pq;

	for (pd = Dests; pd; pd = pd->next) {
		if (including_peer || !addreq(pd->call, pp->call)) {
			pq = find_quality(pd, pp);
			if (pq) {
				pq->delay = pq->lastdelay = 0;
			}
		}
	}
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *setaxp(struct peer *pp)
{
	struct ax25 hdr;
	struct ax25_opts opts;

	if (!(pp->axp = find_ax25(pp->call))) {
		memset(&hdr, 0, sizeof(struct ax25));
		memset(&opts, 0, sizeof(opts));
		addrcp(hdr.dest, pp->call);
		/* One callsign for every FlexNet link of ours, and not the one
		 * the chosen port happens to carry.  FlexNet knows a station
		 * as a callsign and a range, and the range starts at the
		 * callsign the link runs under - so a callsign per port is a
		 * STATION per port to everybody else.  axroute() stamps the
		 * interface's callsign over the source, which is why the
		 * choice has to be put back afterwards; that is what
		 * ownsource is for.
		 */
		if (flex_mycall(hdr.source))
			opts.ownsource = 1;
		if (!(pp->axp = open_ax25(&hdr, AX_ACTIVE,
					  opts.ownsource ? &opts : 0)))
			return 0;
	}
	if (pp->id != pp->axp->id) {
		pp->id = pp->axp->id;
		pp->token = memcmp(pp->axp->hdr.dest, pp->axp->hdr.source, AXALEN) < 0 ?
			NOTOKEN : HAVETOKEN;
		clear_all_via_peer(pp, 0);
		send_init(pp);
		send_poll(pp);
	}
	return pp->axp;
}

/*---------------------------------------------------------------------------*/

/* May we tell the others about this destination?
 *
 * Only a station we speak to ourselves can be hidden.  Anything further away
 * came in through "in", and what "in" accepted is our own route now, which we
 * pass on like any other - that is what makes "advert no" a statement about
 * one entry and not about everything behind it.
 */

static int dest_advertised(const struct dest *pd)
{
	struct peer *pp;

	if (!(pp = find_peer(pd->call)))
		return 1;
	return peer_advert(pp);
}

/*---------------------------------------------------------------------------*/

static void send_rout(struct peer *pp)
{

	int delay;
	int feeds;
	int i;
	int lastdelay;
	struct dest *pd;
	struct mbuf *bp = 0;
	struct quality *pqbest;
	struct quality *pq;
	uint8 *cp = 0;

	if (!setaxp(pp))
		return;
	/* "feed no" is not silence: setaxp() has already greeted him with
	 * FLEX_INIT, which is our own callsign range, so he still knows we are
	 * here and can reach us.  What he does not get is our destinations -
	 * and he gets them WITHDRAWN rather than merely withheld, because
	 * every destination now counts as unreachable to him.  The loop below
	 * sends one round of delay 0 and then falls quiet by itself; simply
	 * returning here would leave whatever we last told him standing at his
	 * end until it aged out.
	 */
	feeds = peer_feed(pp);
	for (pd = Dests; pd; pd = pd->next) {
		if (addreq(pd->call, pp->call))
			continue;
		pq = find_quality(pd, pp);
		if (!pq)
			break;
		pqbest = find_best_quality(pd);
		if (!feeds || !dest_advertised(pd))
			/* Unreachable rather than skipped, and the difference
			 * matters: skipping would leave whatever we last told
			 * him standing until it aged out.  This way the change
			 * travels - the loop below sends one delay 0, which is
			 * "link down", and then falls quiet by itself.
			 */
			delay = MAXDELAY + 1;
		else if (pqbest && pqbest != pq)
			delay = pqbest->delay;
		else
			delay = MAXDELAY + 1;
		if (!(lastdelay = pq->lastdelay))
			lastdelay = MAXDELAY + 1;
		if (delay > lastdelay || iround(1.25 * delay) < lastdelay) {
#if 0
			if (pp->token != HAVETOKEN) {
				if (pp->token != WANTTOKEN &&
				    (bp = alloc_mbuf(3))) {
					cp = bp->data;
					*cp++ = FLEX_ROUT;
					*cp++ = '+';
					*cp++ = '\r';
					bp->cnt = 3;
					send_ax25(pp->axp, &bp, PID_FLEXNET);
					pp->token = WANTTOKEN;
				}
				return;
			}
#endif
			if (bp && (cp - bp->data) > LENROUT - 14) {
				*cp++ = '\r';
				bp->cnt = cp - bp->data;
				send_ax25(pp->axp, &bp, PID_FLEXNET);
				bp = 0;
			}
			if (!bp) {
				if (!(bp = alloc_mbuf(LENROUT)))
					return;
				cp = bp->data;
				*cp++ = FLEX_ROUT;
			}
			for (i = 0; i < ALEN; i++)
				*cp++ = (pd->call[i] >> 1) & 0x7f;
			*cp++ = '0' + ((pd->call[ALEN] & SSID) >> 1);
			*cp++ = '0' + ((pd->call[ALEN + 1] & SSID) >> 1);
			if (delay > MAXDELAY)
				delay = 0;
			sprintf((char *) cp, "%d ", pq->lastdelay = delay);
			while (*cp)
				cp++;
		}
	}
	if (bp) {
		*cp++ = '\r';
		bp->cnt = cp - bp->data;
		send_ax25(pp->axp, &bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static char *sprintflexcall(char *buf, const uint8 *call)
{

	char *cp;
	int chr;
	int i;
	int max_ssid;
	int min_ssid;

	cp = buf;
	for (i = 0; i < ALEN; i++) {
		chr = (call[i] >> 1) & 0x7f;
		if (chr != ' ') {
			*cp++ = chr;
		}
	}
	min_ssid = (call[ALEN    ] & SSID) >> 1;
	max_ssid = (call[ALEN + 1] & SSID) >> 1;
	if (!min_ssid && !max_ssid) {
		*cp = 0;
	} else if (min_ssid == max_ssid) {
		sprintf(cp, "-%d", min_ssid);
	} else {
		sprintf(cp, "-%d-%d", min_ssid, max_ssid);
	}
	return buf;
}

/*---------------------------------------------------------------------------*/

static void process_changes(void)
{

	struct dest *pd;
	struct peer *pp;

	for (pp = Peers; pp; pp = pp->next)
		send_rout(pp);
	delete_unreachables();
	for (pd = Dests; pd; pd = pd->next)
		update_axroute(pd);
}

/*---------------------------------------------------------------------------*/

static void delete_peer(struct peer *peer)
{

	struct dest *pd;
	struct peer *pp;
	struct peer **ppp;
	struct quality *pq;
	struct quality **ppn;

	for (ppp = &Peers; (pp = *ppp);) {
		if (pp == peer) {
			for (pd = Dests; pd; pd = pd->next) {
				for (ppn = &pd->qualities; (pq = *ppn);) {
					if (pq->peer == pp) {
						*ppn = pq->next;
						free(pq);
						break;
					} else {
						ppn = &pq->next;
					}
				}
			}
			if ((pp->axp = find_ax25(pp->call)))
				disc_ax25(pp->axp);
			*ppp = pp->next;
			free(pp);
			if (!Peers)
				stop_timer(&Polltimer);
			return;
		} else {
			ppp = &pp->next;
		}
	}
}

/*---------------------------------------------------------------------------*/

static void polltimer_expired(void *unused)
{

	struct ax25_cb *axp;
	struct peer *ppnext = 0;
	struct peer *pp;

	start_timer(&Polltimer);
	for (pp = Peers; pp; pp = ppnext) {
		ppnext = pp->next;
		/* A peer nobody entered is not polled, so an unanswered poll
		 * can no longer be what tells us he has gone.  His link is:
		 * he exists because he called us, and when the connection is
		 * gone so is he.  Without this he would stay in the list for
		 * ever - and worse, setaxp() below would call HIM every five
		 * minutes, which is the last thing a user wants from us.
		 */
		if (!pp->permanent) {
			axp = find_ax25(pp->call);
			if (!axp || axp->state == LAPB_DISCONNECTED)
				delete_peer(pp);
			continue;
		}
		if (pp->lastpolltime)
			clear_all_via_peer(pp, 1);
	}
	for (pp = Peers; pp; pp = pp->next) {
		if (!pp->permanent)
			continue;
		if (setaxp(pp) && !pp->lastpolltime)
			send_poll(pp);
	}
	process_changes();
}

/*---------------------------------------------------------------------------*/

/* "permanent" has to be known here and not set afterwards: setaxp() greets
 * the link straight away, and whether that greeting carries a poll depends on
 * it.  Setting it after the fact would leave an entered link partner
 * unmeasured until the next poll interval.
 */

static struct peer *create_peer(const uint8 *call, int permanent)
{
	struct peer *pp;

	if ((pp = (struct peer *) calloc(1, sizeof(struct peer)))) {
		if (!Peers) {
			Polltimer.func = polltimer_expired;
			Polltimer.arg = 0;
			set_timer(&Polltimer, POLLINTERVAL);
		}
		start_timer(&Polltimer);
		pp->next = Peers;
		Peers = pp;
		flexaddrcp(pp->call, call);
		pp->permanent = permanent;
		/* A peer we never poll has no measured delay, and his delay is
		 * what we add to everything he tells us about.  Left at zero
		 * he would look like the fastest link we have, so a route
		 * through a user would beat the route through a partner.
		 * DEFAULTDELAY is the value the protocol itself uses for "no
		 * idea" - recv_poll() sends it, recv_rprt() ignores it on the
		 * way in - so an unmeasured link is simply an unattractive
		 * one.  It belongs here and not in recv_rout(): recv_rprt()
		 * rescales what it has already stored by the DIFFERENCE in
		 * pp->delay, so a value added anywhere else would stay in the
		 * table for ever.  Measured: with the guess in recv_rout(),
		 * a delay of 623 became 637 on the first poll answer instead
		 * of 37.
		 */
		if (!permanent)
			pp->delay = DEFAULTDELAY;
		setaxp(pp);
	}
	return pp;
}

/*---------------------------------------------------------------------------*/

static int doflexnetdest(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	int curr;
	int next;
	struct dest *pd1;
	struct dest *pd;
	struct quality *pq;
	uint8 call[FLEXLEN];

	if (argc > 1) {
		if (flexsetcall(call, argv[1]))
			return 1;
		if (!(pd1 = find_dest(call))) {
			printf("Destination \"%s\" not found\n", argv[1]);
			return 1;
		}
	} else
		pd1 = 0;
	printf("Call          Neighbors\n");
	for (pd = Dests; pd; pd = pd->next) {
		if (!pd1 || pd == pd1) {
			printf("%-12s", sprintflexcall(buf, pd->call));
			for (curr = 1; curr != (next = 0x7fffffff); curr = next)
				for (pq = pd->qualities; pq; pq = pq->next)
					if (pq->delay == curr)
						printf("  %s (%d)", sprintflexcall(buf, pq->peer->call), pq->delay);
					else if (pq->delay > curr && pq->delay < next)
						next = pq->delay;
			putchar('\n');
		}
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetdestdebug(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	struct dest *pd;
	struct quality *pq;

	printf("Call          Neighbors\n");
	for (pd = Dests; pd; pd = pd->next) {
		printf("%-12s", sprintflexcall(buf, pd->call));
		for (pq = pd->qualities; pq; pq = pq->next)
			printf("  %s (D=%d L=%d)", sprintflexcall(buf, pq->peer->call), pq->delay, pq->lastdelay);
		putchar('\n');
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkadd(int argc, char *argv[], void *p)
{

	struct peer *pp;
	uint8 call[FLEXLEN];

	if (flexsetcall(call, argv[1]))
		return 1;
	if (ismyax25addr(call)) {
		printf("Cannot add link to myself\n");
		return 1;
	}
	if (!(pp = find_peer(call)) && !(pp = create_peer(call, 1))) {
		printf("%s", Nospace);
		return 1;
	}
	/* He may have turned up by himself first, in which case this is the
	 * moment he becomes a link partner.  Start the link over rather than
	 * just setting the flag: he carries the DEFAULTDELAY guess from
	 * create_peer(), and everything learned through him was scaled with
	 * it.  Forgetting that and greeting him afresh is what setaxp() does
	 * for a new connection anyway, so it is a path that already works.
	 */
	if (!pp->permanent) {
		pp->permanent = 1;
		pp->delay = 0;
		pp->id = 0;
		setaxp(pp);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinkdel(int argc, char *argv[], void *p)
{

	struct peer *pp;
	uint8 call[FLEXLEN];

	if (flexsetcall(call, argv[1]))
		return 1;
	if (!(pp = find_peer(call))) {
		printf("Link \"%s\" not found\n", argv[1]);
		return 1;
	}
	delete_peer(pp);
	process_changes();
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlinklist(int argc, char *argv[], void *p)
{

	char buf[FLEXBUF];
	int state;
	static const char tokenstr[] = "NWY";
	struct ax25_cb *axp;
	struct peer *pp;

	/* In and Advert are what is IN FORCE for this peer, not what was
	 * written about him: the rule may have been written for his port or
	 * be the default, and working that out by hand across three levels is
	 * exactly what the sysop should not have to do.
	 */
	printf("Call         Remote  Local Smooth P T In        Adv Feed State\n");
	for (pp = Peers; pp; pp = pp->next) {
		state = (axp = find_ax25(pp->call)) ? axp->state : LAPB_DISCONNECTED;
		printf("%-12s %6d %6d %6d %c %c %-8s  %-3s %-4s %s\n",
		       sprintflexcall(buf, pp->call),
		       pp->remdelay,
		       pp->locdelay,
		       iround(pp->delay),
		       pp->permanent ? 'P' : ' ',
		       tokenstr[pp->token],
		       rf_in_name(peer_in(pp)),
		       peer_advert(pp) ? "yes" : "no",
		       peer_feed(pp) ? "yes" : "no",
		       Ax25states[state]);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetlink(int argc, char *argv[], void *p)
{

	static struct cmds Flexlinkcmds[] = {
		{ "add",    doflexnetlinkadd,  0, 2, "flexnet link add <call>" },
		{ "delete", doflexnetlinkdel,  0, 2, "flexnet link delete <call>" },
		{ 0,        0,                 0, 0, 0 }
	};

	if (argc < 2)
		return doflexnetlinklist(argc, argv, p);
	return subcmd(Flexlinkcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int addrmatch(const uint8 *axcall, const uint8 *flexcall)
{
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	if (*axcall++ != *flexcall++) return 0;
	return ((*axcall & SSID) >= (flexcall[0] & SSID) &&
		(*axcall & SSID) <= (flexcall[1] & SSID));
}

/*---------------------------------------------------------------------------*/

static int decode_query_packet(struct mbuf **bpp, struct querypkt *qp)
{

	char *cp;
	int chr;
	int gotsrc;
	int i;

	if ((chr = PULLCHAR(bpp)) == -1)
		goto discard;
	qp->hopcnt = chr - ' ';
	if (qp->hopcnt < 0 || qp->hopcnt > MAXHOPCNT)
		goto discard;
	qp->qsonum = 0;
	for (i = 0; i < 5; i++) {
		chr = PULLCHAR(bpp);
		if (chr == ' ')
			continue;
		if (chr < '0' || chr > '9')
			goto discard;
		qp->qsonum = qp->qsonum * 10 + chr - '0';
	}
	gotsrc = 0;
	qp->numcalls = 0;
	cp = qp->bufs[qp->numcalls];
	for (;;) {
		chr = PULLCHAR(bpp);
		if ((chr == -1 || chr == ' ' || chr == '\r') && cp != qp->bufs[qp->numcalls]) {
			*cp = 0;
			if (strcmp(qp->bufs[qp->numcalls], "???") &&
			    setcall(qp->destcall, qp->bufs[qp->numcalls]))
				goto discard;
			qp->numcalls++;
			if (!gotsrc) {
				addrcp(qp->srccall, qp->destcall);
				gotsrc = 1;
			}
			cp = qp->bufs[qp->numcalls];
		}
		if (chr == ' ') {
			continue;
		} else if (chr == -1 || chr == '\r') {
			break;
		} else if ((chr >= '0' && chr <= '9') ||
			   (chr >= 'A' && chr <= 'Z') ||
			   (chr >= 'a' && chr <= 'z') ||
			   (chr == '-') ||
			   (chr == '?')) {
			if (qp->numcalls >= MAXQCALLS)
				goto discard;
			*cp++ = chr;
			if (cp - qp->bufs[qp->numcalls] >= AXBUF)
				goto discard;
		} else {
			goto discard;
		}
	}
	free_p(bpp);
	return 0;

      discard:
	free_p(bpp);
	return -1;
}

/*---------------------------------------------------------------------------*/

static void print_query_packet(const struct querypkt *qp)
{
	int i;

	printf("Flexnet routing:");
	for (i = 0; i < qp->numcalls; i++)
		printf(" %s", qp->bufs[i]);
	putchar('\n');
}

/*---------------------------------------------------------------------------*/

static int send_query_packet(int type, const struct querypkt *qp, const uint8 *call)
{

	int i;
	struct dest *pd;
	struct mbuf *bp;
	struct quality *pq;
	uint8 *cp;

	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(call, pd->call))
			break;
	if (!pd)
		return -1;
	if (!(pq = find_best_quality(pd)))
		return -1;
	if (!setaxp(pq->peer))
		return -1;
	if (!(bp = alloc_mbuf(LENROUT)))
		return -1;
	cp = bp->data;
	*cp++ = type;
	*cp++ = ' ' + qp->hopcnt;
	sprintf((char *) cp, "%5d", qp->qsonum);
	cp += 5;
	for (i = 0; i < qp->numcalls; i++) {
		if (cp - bp->data > LENROUT - 11) {
			free_p(&bp);
			return -1;
		}
		if (i > 0)
			*cp++ = ' ';
		strcpy((char *) cp, qp->bufs[i]);
		while (*cp)
			cp++;
	}
	*cp++ = '\r';
	bp->cnt = cp - bp->data;
	send_ax25(pq->peer->axp, &bp, PID_FLEXNET);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetquery(int argc, char *argv[], void *p)
{

	char buf[AXBUF];
	char *cp;
	int n;
	struct ax_route *rp;
	struct ax_route *rp_stack[20];
	struct dest *pd;
	struct iface *ifp;
	struct quality *pq;
	struct querypkt querypkt;

	memset(&querypkt, 0, sizeof(struct querypkt));
	if (setcall(querypkt.destcall, argv[1])) {
		printf("Invalid call \"%s\"\n", argv[1]);
		return 1;
	}
	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(querypkt.destcall, pd->call))
			break;
	if (!pd) {
		printf("Destination \"%s\" not found\n", argv[1]);
		return 1;
	}
	if (!(pq = find_best_quality(pd))) {
		printf("Destination \"%s\" not reachable\n", argv[1]);
		return 1;
	}
	if (!(rp = ax_routeptr(pq->peer->call, 0))) {
		printf("No AX.25 route for link \"%s\"\n", pax25(buf, pq->peer->call));
		return 1;
	}
	ifp = 0;
	for (n = 0; rp; rp = rp->digi) {
		rp_stack[n++] = rp;
		ifp = rp->ifp;
	}
	addrcp(querypkt.srccall, ifp ? ifp->hwaddr : Mycall);
	pax25(querypkt.bufs[0], querypkt.srccall);
	querypkt.numcalls = 1;
	while (--n >= 0) {
		if (querypkt.numcalls >= MAXQCALLS) {
			printf("Too many hops\n");
			return 1;
		}
		cp = querypkt.bufs[querypkt.numcalls++];
		pax25(cp, rp_stack[n]->target);
		if (n > 0)
			for (; (*cp = Xtolower(*cp)); cp++) ;
	}
	if (addreq(pd->call, pq->peer->call)) {
		print_query_packet(&querypkt);
		return 0;
	}
	if (querypkt.numcalls >= MAXQCALLS) {
		printf("Too many hops\n");
		return 1;
	}
	pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
	if (send_query_packet(FLEX_QURY, &querypkt, pq->peer->call)) {
		printf("Could not send query to %s\n", pax25(buf, pq->peer->call));
		return 1;
	}
	printf("Query sent to %s\n", pax25(buf, pq->peer->call));
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Which of our SSIDs do we serve, and which do we announce?
 *
 * FlexNet says a station is a callsign and a RANGE of SSIDs, and it says it in
 * two halves that come from different places:
 *
 *      start   the callsign the link runs under
 *      stop    one number, sent in FLEX_INIT at link setup
 *
 * XNET writes both down in one place - "my call db0blo-4" and
 * "ro fl pa ssid 15" - so the range is a property of the NODE and every link
 * announces the same one.  Ours is a property of the LINK: axroute() stamps
 * the interface's callsign over the source (build_path), and send_init() puts
 * that same SSID in as the stop.  Two ports with callsigns of their own
 * therefore announce two stations - measured, DL9SAU-4-4 and DL9SAU-6-6 - and
 * neither range covers anything else we answer to.
 *
 * With no arguments it SHOWS both, and the showing is half the point: what we
 * serve is derivable (the ports' callsigns and the "listen" entries), what we
 * announce is not the same thing, and until the two stand side by side nobody
 * can see the gap.
 *
 * With a range it SETS both, node-wide.  The start becomes the callsign every
 * FlexNet link of ours is opened under - see setaxp() - and the stop goes into
 * FLEX_INIT.
 *
 * ONLY LINKS WE OPEN.  A partner who calls US reaches whichever callsign he
 * dialled, and the answer has to carry that or he would not recognise it; the
 * range he derives is then his choice, not ours.  In FlexNet that case does
 * not arise, because a partner is configured with the one callsign the node
 * uses ("rou flex add 3 igateb"), so a link arriving elsewhere is a
 * misconfiguration at his end.  The display says so rather than hiding it.
 *
 * AND IT TAKES EFFECT ON THE NEXT LINK, not at once.  Re-sending FLEX_INIT
 * would look tidier and is a trap: recv_init() at the far end calls
 * clear_all_via_peer(), so he forgets every route he learned from us - while
 * our own send_rout() only ever sends CHANGES, so we would not tell him again.
 * Better a range that arrives when the link next comes up than a table that
 * quietly empties.
 *
 * The range is a DECLARATION, not a promise, and that is not our licence but
 * FlexNet practice: DB0BLO announces 4-15 while -6 and -7 exist nowhere.  So
 * the gaps below are worth showing but are not faults.
 */

static int doflexnetssid(int argc, char *argv[], void *p)
{
	char buf[AXBUF];
	int announced[16];
	int n = 0;
	int run_start = -1;
	int seen_start[16];
	int served[16];
	int ssid;
	int starts = 0;
	struct peer *pp;
	uint8 call[AXALEN];

	if (argc >= 2) {
		char *cp;
		long start;
		long stop;

		if (!strcmp(argv[1], "none") || !strcmp(argv[1], "off")) {
			Flex_start = Flex_stop = -1;
			if (Peers)
				printf("Cleared.  Links already up keep the "
				       "callsign they were opened under.\n");
			return 0;
		}
		start = strtol(argv[1], &cp, 10);
		if (cp == argv[1] || *cp != '-') {
			printf("The range is written <start>-<stop>, e.g. "
			       "\"flexnet ssid 0-15\"\n");
			return 1;
		}
		stop = strtol(++cp, &cp, 10);
		if (*cp || start < 0 || stop > 15 || start > stop) {
			printf("Both ends are 0..15 and the start is not "
			       "above the stop\n");
			return 1;
		}
		Flex_start = (int) start;
		Flex_stop = (int) stop;
		/* Deliberately no announcement here - see the note above. */
		if (Peers)
			printf("Set.  Links already up keep what they "
			       "announced; this reaches a partner when his "
			       "link next comes up.\n");
		return 0;
	}

	memset(served, 0, sizeof(served));
	memset(announced, 0, sizeof(announced));
	memset(seen_start, 0, sizeof(seen_start));

	printf("SSIDs of %.6s this node answers to:", pax25(buf, Mycall));
	/* One walk over all sixteen, asking the two sources that decide it -
	 * the ports' own callsigns and the listeners.  Cheaper than reaching
	 * into either list, and it cannot fall out of step with them.
	 */
	for (ssid = 0; ssid <= 16; ssid++) {
		if (ssid < 16) {
			addrcp(call, Mycall);
			call[ALEN] = (uint8) (0x60 | (ssid << 1));
			served[ssid] = ismyax25addr(call) != NULL ||
				       axlisten_active(call);
		}
		if (ssid < 16 && served[ssid] && run_start < 0)
			run_start = ssid;
		else if ((ssid == 16 || !served[ssid]) && run_start >= 0) {
			if (run_start == ssid - 1)
				printf("%s %d", n++ ? "," : "", run_start);
			else
				printf("%s %d-%d", n++ ? "," : "",
				       run_start, ssid - 1);
			run_start = -1;
		}
	}
	if (!n)
		printf(" (none)");
	putchar('\n');

	if (Flex_start < 0) {
		printf("Configured range: none - each link announces the "
		       "callsign of its own port\n");
	} else {
		printf("Configured range: %.6s-%d-%d\n", pax25(buf, Mycall),
		       Flex_start, Flex_stop);
		if (!served[Flex_start])
			printf("  the start is an SSID this node does not "
			       "answer to - a caller reaching it finds "
			       "nothing\n");
	}

	/* And what actually goes out, per link.  The start is the source
	 * callsign of the link, which is ours only where we opened it, and the
	 * stop is what send_init() sent.
	 */
	if (!Peers) {
		printf("No FlexNet links - the range is announced in "
		       "FLEX_INIT when one comes up\n");
		return 0;
	}
	printf("Announced, per link:\n");
	for (pp = Peers; pp; pp = pp->next) {
		char peerbuf[FLEXBUF];
		int stop;

		printf("  %-12s ", sprintflexcall(peerbuf, pp->call));
		if (!pp->axp) {
			printf("(no link)\n");
			continue;
		}
		ssid = (pp->axp->hdr.source[ALEN] & SSID) >> 1;
		stop = Flex_stop >= 0 ? Flex_stop : ssid;
		printf("as %-10s -> %.6s-%d-%d%s\n",
		       pax25(buf, pp->axp->hdr.source),
		       pax25(buf, pp->axp->hdr.source), ssid, stop,
		       (Flex_start >= 0 && ssid != Flex_start)
		         ? "   he called this callsign" : "");
		for (n = ssid; n <= stop; n++)
			announced[n] = 1;
		if (!seen_start[ssid]) {
			seen_start[ssid] = 1;
			starts++;
		}
	}

	/* Name what the two lists say together, rather than leaving it to be
	 * read off.  Neither line is a fault by itself - a range with gaps is
	 * ordinary FlexNet - but two ranges for one node are two stations to
	 * everybody else, and that one is.
	 */
	if (starts > 1)
		printf("Two or more ranges for one node: to the network that is "
		       "%d stations, not one.\n", starts);
	for (ssid = 0, n = 0; ssid < 16; ssid++)
		if (served[ssid] && !announced[ssid])
			n++;
	if (n)
		printf("%d served SSID%s outside every announced range - not "
		       "reachable by FlexNet routing.\n", n, n == 1 ? "" : "s");
	return 0;
}

/*---------------------------------------------------------------------------*/

static int doflexnetfilter(int argc, char *argv[], void *p)
{
	int ret = rf_cmd(RF_FLEXNET, argc, argv, p);

	/* Take effect now, not at the next incoming frame.  send_rout() only
	 * runs from process_changes(), so without this a filter set at the
	 * console did nothing at all until a peer happened to say something or
	 * the five minute poll came round - measured, and long enough that it
	 * looks broken.  It also matters what travels: a route that stops
	 * being announced has to be WITHDRAWN with a delay of 0, and that
	 * withdrawal is a send like any other.
	 */
	if (!ret)
		process_changes();
	return ret;
}

/*---------------------------------------------------------------------------*/

int doflexnet(int argc, char *argv[], void *p)
{

	static struct cmds Flexnetcmds[] = {
		{ "dest",      doflexnetdest,      0, 0,
		  "flexnet dest [<call>]                 destinations and their delay" },
		{ "destdebug", doflexnetdestdebug, 0, 0,
		  "flexnet destdebug                     the same with the raw delays" },
		{ "filter",    doflexnetfilter,    0, 0, Rf_usage_flexnet },
		{ "link",      doflexnetlink,      0, 0,
		  "flexnet link                          list the link partners\n"
		  "       flexnet link add|delete <call>" },
		{ "query",     doflexnetquery,     0, 2, "flexnet query <call>" },
		{ "ssid",      doflexnetssid,      0, 0,
		  "flexnet ssid                          show what we serve and\n"
		  "                                             what we announce\n"
		  "       flexnet ssid <start>-<stop>           set both, node-wide\n"
		  "       flexnet ssid none                     back to per-port\n"
		  "\n"
		  "  FlexNet knows a station as a callsign and a RANGE of SSIDs.  The\n"
		  "  start is the callsign our links run under, the stop is the one\n"
		  "  number in FLEX_INIT - XNET writes the same two as \"my call\" and\n"
		  "  \"ro fl pa ssid\".  Unset, we take both from whichever port the\n"
		  "  route picked, so ports with callsigns of their own announce one\n"
		  "  station each.\n"
		  "\n"
		  "  The range is a declaration, not a promise: gaps in it are\n"
		  "  ordinary FlexNet.  It applies to links WE open and reaches a\n"
		  "  partner when his link next comes up." },
		{ 0,           0,                  0, 0, 0 }
	};

	return subcmd(Flexnetcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int pullnumber(struct mbuf **bpp, int *val)
{
	int chr;

	*val = 0;
	for (;;) {
		chr = PULLCHAR(bpp);
		if (chr < '0' || chr > '9')
			return chr;
		*val = *val * 10 + chr - '0';
	}
}

/*---------------------------------------------------------------------------*/

static int pullflexcall(struct mbuf **bpp, int chr, uint8 *call)
{
	int i;

	for (i = 0; i < ALEN; i++) {
		if (chr == -1) {
			chr = PULLCHAR(bpp);
			if (chr == -1)
				return -1;
		}
		*call++ = chr << 1;
		chr = -1;
	}
	for (i = 0; i < 2; i++) {
		chr = PULLCHAR(bpp);
		if (chr == -1)
			return -1;
		*call++ = ((chr << 1) & SSID) | 0x60;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static void recv_init(struct peer *pp, struct mbuf **bpp)
{
	int chr;

	if ((chr = PULLCHAR(bpp)) != -1)
		pp->call[ALEN + 1] = ((chr << 1) & SSID) | 0x60;
	free_p(bpp);
	pp->token = memcmp(pp->axp->hdr.dest, pp->axp->hdr.source, AXALEN) < 0 ?
		NOTOKEN : HAVETOKEN;
	clear_all_via_peer(pp, 0);
}

/*---------------------------------------------------------------------------*/

static void recv_rprt(struct peer *pp, struct mbuf **bpp)
{

	int delay;
	int olddelay;
	struct dest *pd;
	struct quality *pq;

	if (pullnumber(bpp, &delay) == -1)
		return;
	free_p(bpp);
	olddelay = iround(pp->delay);
	if (delay > 0 && delay != DEFAULTDELAY) {
		pp->remdelay = delay;
		if (pp->delay)
			pp->delay = (7.0 * pp->delay + pp->remdelay) / 8.0;
		else
			pp->delay = pp->remdelay;
	}
	if (pp->lastpolltime) {
		pp->locdelay = iround(TDIFF(msclock(), pp->lastpolltime) / 200.0);
		if (pp->locdelay < 1)
			pp->locdelay = 1;
		pp->lastpolltime = 0;
		if (pp->delay)
			pp->delay = (7.0 * pp->delay + pp->locdelay) / 8.0;
		else
			pp->delay = pp->locdelay;
	}
	delay = iround(pp->delay);
	if (delay != olddelay) {
		for (pd = Dests; pd; pd = pd->next)
			for (pq = pd->qualities; pq; pq = pq->next)
				if (pq->peer == pp && pq->delay)
					pq->delay = pq->delay - olddelay + delay;
	}
	/* "in none": he stays a peer - we answer him and he may have our
	 * table - but he is not a destination of ours, so nothing routes to
	 * him and nobody hears of him.
	 */
	if (peer_in(pp) != RF_IN_NONE)
		update_delay(pp->call, pp, delay);
}

/*---------------------------------------------------------------------------*/

static void recv_poll(const struct peer *pp, struct mbuf **bpp)
{
	struct mbuf *bp;

	free_p(bpp);
	if ((bp = alloc_mbuf(20))) {
		sprintf((char *) bp->data, "%c%d\r", FLEX_RPRT, pp->locdelay ? pp->locdelay : DEFAULTDELAY);
		bp->cnt = strlen((char *) bp->data);
		send_ax25(pp->axp, &bp, PID_FLEXNET);
	}
}

/*---------------------------------------------------------------------------*/

static void recv_rout(struct peer *pp, struct mbuf **bpp)
{

	int chr;
	int delay;
	int learn;
	int ourdelay;
	struct mbuf *bp;
	uint8 call[FLEXLEN];
	uint8 *cp;

	/* Only "in all" lets a station teach us about others.  Under
	 * "only-him" and "none" his routing packet is read to the end and
	 * thrown away - read to the end because the token handling below is
	 * about the link, not about routes, and it still has to work.
	 */
	learn = peer_in(pp) == RF_IN_ALL;
	ourdelay = iround(pp->delay);

	for (;;) {
		chr = PULLCHAR(bpp);
		if ((chr >= '0' && chr <= '9') || (chr >= 'A' && chr <= 'Z')) {
			if (pullflexcall(bpp, chr, call))
				break;
			if (pullnumber(bpp, &delay) == -1)
				break;
			if (learn && !ismyax25addr(call)) {
				if (delay)
					update_delay(call, pp, iround(1.125 * delay) + ourdelay);
				else
					update_delay(call, pp, 0);
			}
		} else if (chr == '+') {
			if ((bp = alloc_mbuf(3))) {
				cp = bp->data;
				*cp++ = FLEX_ROUT;
				*cp++ = '-';
				*cp++ = '\r';
				bp->cnt = 3;
				send_ax25(pp->axp, &bp, PID_FLEXNET);
				pp->token = NOTOKEN;
			}
			break;
		} else if (chr == '-') {
			pp->token = HAVETOKEN;
			break;
		} else {
			break;
		}
	}
	free_p(bpp);
}

/*---------------------------------------------------------------------------*/

static void recv_qury(struct peer *pp, struct mbuf **bpp)
{

	char *cp;
	int n;
	struct ax_route *rp;
	struct ax_route *rp_stack[20];
	struct dest *pd;
	struct quality *pq = 0;
	struct querypkt querypkt;

	if (decode_query_packet(bpp, &querypkt))
		return;
	querypkt.hopcnt++;
	if (ismyax25addr(querypkt.destcall)) {
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
		return;
	}
	querypkt.numcalls--;
	for (pd = Dests; pd; pd = pd->next)
		if (addrmatch(querypkt.destcall, pd->call))
			break;
	if (pd &&
	    (pq = find_best_quality(pd)) &&
	    (rp = ax_routeptr(pq->peer->call, 0))) {
		for (n = 0; rp; rp = rp->digi)
			rp_stack[n++] = rp;
		while (--n >= 0) {
			if (querypkt.numcalls >= MAXQCALLS)
				return;
			cp = querypkt.bufs[querypkt.numcalls++];
			pax25(cp, rp_stack[n]->target);
			if (n > 0)
				for (; (*cp = Xtolower(*cp)); cp++) ;
		}
		if (addreq(pd->call, pq->peer->call)) {
			send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
		} else {
			if (querypkt.numcalls >= MAXQCALLS)
				return;
			pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
			send_query_packet(FLEX_QURY, &querypkt, pq->peer->call);
		}
	} else {
		if (querypkt.numcalls >= MAXQCALLS)
			return;
		strcpy(querypkt.bufs[querypkt.numcalls++], "???");
		if (querypkt.numcalls >= MAXQCALLS)
			return;
		pax25(querypkt.bufs[querypkt.numcalls++], querypkt.destcall);
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
	}
}

/*---------------------------------------------------------------------------*/

static void recv_rslt(struct peer *pp, struct mbuf **bpp)
{
	struct querypkt querypkt;

	if (decode_query_packet(bpp, &querypkt))
		return;
	querypkt.hopcnt++;
	if (ismyax25addr(querypkt.srccall))
		print_query_packet(&querypkt);
	else
		send_query_packet(FLEX_RSLT, &querypkt, querypkt.srccall);
}

/*---------------------------------------------------------------------------*/

void flexnet_input(struct iface *iface, struct ax25_cb *axp, uint8 *src, uint8 *destination, struct mbuf **bpp, int mcast)
{

	struct peer *pp;
	uint8 call[FLEXLEN];

	if (!axp || !bpp || !*bpp || mcast)
		goto discard;
	if (!(pp = find_peer(src))) {
		if (ismyax25addr(src))
			goto discard;
		addrcp(call, src);
		call[ALEN + 1] = call[ALEN];
		if (!(pp = create_peer(call, 0)))
			goto discard;
	} else if (pp->id != axp->id) {
		setaxp(pp);
	}
	/* Where he is, for a filter written per port.  Kept up to date rather
	 * than set once: a peer is a callsign, and the same callsign may well
	 * come back on a different port tomorrow.
	 */
	pp->iface = iface;
	switch (PULLCHAR(bpp)) {

	case FLEX_INIT:
		recv_init(pp, bpp);
		break;

	case FLEX_RPRT:
		recv_rprt(pp, bpp);
		break;

	case FLEX_POLL:
		recv_poll(pp, bpp);
		break;

	case FLEX_ROUT:
		recv_rout(pp, bpp);
		break;

	case FLEX_QURY:
		recv_qury(pp, bpp);
		break;

	case FLEX_RSLT:
		recv_rslt(pp, bpp);
		break;

	default:
		goto discard;

	}
	process_changes();
	return;

      discard:
	free_p(bpp);
}

/*---------------------------------------------------------------------------*/

void flexnet_dump(FILE *fp, struct mbuf **bpp)
{

	char buf[FLEXBUF];
	int chr;
	int func;
	int i;
	struct querypkt querypkt;
	uint8 call[FLEXLEN];

	if (bpp == NULL || *bpp == NULL)
		return;
	fprintf(fp, "FLEXNET: len %d", len_p(*bpp));
	switch (func = PULLCHAR(bpp)) {

	case -1:
		goto too_short;

	case FLEX_INIT:
		fprintf(fp, " Link setup");
		if ((i = PULLCHAR(bpp)) == -1)
			goto too_short;
		fprintf(fp, " - Max SSID: %d", i & 0xf);
		break;

	case FLEX_RPRT:
		fprintf(fp, " Poll response");
		if (pullnumber(bpp, &i) == -1)
			goto too_short;
		fprintf(fp, " - Delay: %d", i);
		break;

	case FLEX_POLL:
		fprintf(fp, " Poll");
		break;

	case FLEX_ROUT:
		fprintf(fp, " Routing:");
		for (;;) {
			chr = PULLCHAR(bpp);
			if (chr == -1) {
				goto too_short;
			} else if ((chr >= '0' && chr <= '9') || (chr >= 'A' && chr <= 'Z')) {
				if (pullflexcall(bpp, chr, call))
					goto too_short;
				fprintf(fp, "\n  %-12s", sprintflexcall(buf, call));
				if (pullnumber(bpp, &i) == -1)
					goto too_short;
				if (!i)
					fprintf(fp, " Link down");
				else
					fprintf(fp, " Delay %d", i);
			} else if (chr == '+') {
				fprintf(fp, "\n  Request token");
			} else if (chr == '-') {
				fprintf(fp, "\n  Release token");
			} else if (chr == '\r') {
				break;
			} else {
				fprintf(fp, "\n  Unknown character '%c'", chr);
				break;
			}
		}
		break;

	case FLEX_QURY:
	case FLEX_RSLT:
		if (func == FLEX_QURY)
			fprintf(fp, " Route query");
		else
			fprintf(fp, " Query response");
		if (decode_query_packet(bpp, &querypkt)) {
			*bpp = 0;
			fprintf(fp, " - bad packet\n");
			return;
		}
		*bpp = 0;
		fprintf(fp, " - hopcnt: %d qso: %d\n  ", querypkt.hopcnt, querypkt.qsonum);
		for (i = 0; i < querypkt.numcalls; i++)
			printf(" %s", querypkt.bufs[i]);
		break;

	default:
		fprintf(fp, " Unknown type: %d", func);
		break;

	}
	putc('\n', fp);
	return;

      too_short:
	fprintf(fp, " - packet too short\n");
}
