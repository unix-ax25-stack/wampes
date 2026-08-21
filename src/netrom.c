/* @(#) $Id: netrom.c,v 1.58 2000/03/04 18:31:14 deyke Exp $ */

#include <ctype.h>
#include <stdio.h>

#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "arp.h"
#include "ip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "routefilter.h"
#include "pidfilter.h"
#include "cmdparse.h"
#include "trace.h"

static int nr_maxdest     =   400;
static int nr_minqual     =     0;      /* not used */
static int nr_hfqual      =   192;
static int nr_rsqual      =   255;      /* not used */
static int nr_obsinit     =     3;
static int nr_minobs      =     0;      /* not used */
static int nr_bdcstint    =  1800;
static int nr_ttlinit     =    16;
static int nr_ttimeout    =    60;
static int nr_tretry      =     5;
static int nr_tackdelay   =     1;      /* not used */
static int nr_tbsydelay   =   180;
static int nr_twindow     =     8;
static int nr_tnoackbuf   =     8;
static int nr_timeout     =  1800;
static int nr_persistance =    64;      /* not used */
static int nr_slottime    =    10;      /* not used */
static int nr_callcheck   =     0;      /* not used */
static int nr_beacon      =     0;      /* not used */
static int nr_cq          =     0;      /* not used */
static int nr_maxcircuits =   100;
/* Which table decides where a datagram goes.  Not a preference but the whole
 * reason no conversion is needed in the middle: if INP3 wins as soon as it
 * has an entry, nothing has to be compared against a quality, and the graph
 * is left exactly as it was.
 *
 * Default on, and not out of enthusiasm - WE DO NOT MEASURE ANYTHING in the
 * graph.  nr_hfqual is a constant every direct neighbour is given, so its
 * figure is an assumption where INP3's is a measurement, and preferring the
 * measured one is the only choice with a reason behind it.
 *
 * Turned off, INP3 is asked only where the graph has nothing - and that buys
 * back a problem: comparing the two means converting between them, and
 * qual2rtt covers 0.1 s to 25.5 s where INP3 reaches 599.99 s.
 */
static int nr_inp3first   =     1;

static const struct parms {
  char *text;
  int *valptr;
  int minval;
  int maxval;
} parms[] = {
  { "",                                               0,               0,          0 },
  { " 1 Maximum destination list entries           ", &nr_maxdest,     1,        400 },
  { " 2 Worst quality for auto-updates             ", &nr_minqual,     0,        255 },
  { " 3 Channel 0 (HDLC) quality                   ", &nr_hfqual,      0,        255 },
  { " 4 Channel 1 (RS232) quality                  ", &nr_rsqual,      0,        255 },
  { " 5 Obsolescence count initializer (0=off)     ", &nr_obsinit,     0,        255 },
  { " 6 Obsolescence count min to be broadcast     ", &nr_minobs,      0,        255 },
  { " 7 Auto-update broadcast interval (sec, 0=off)", &nr_bdcstint,    0,      65535 },
  { " 8 Network 'time-to-live' initializer         ", &nr_ttlinit,     1,        255 },
  { " 9 Transport timeout (sec)                    ", &nr_ttimeout,    5,        600 },
  { "10 Transport maximum tries                    ", &nr_tretry,      1,        127 },
  { "11 Transport acknowledge delay (ms)           ", &nr_tackdelay,   1,      60000 },
  { "12 Transport busy delay (sec)                 ", &nr_tbsydelay,   1,       1000 },
  { "13 Transport requested window size (frames)   ", &nr_twindow,     1,        127 },
  { "14 Congestion control threshold (frames)      ", &nr_tnoackbuf,   1,        127 },
  { "15 No-activity timeout (sec, 0=off)           ", &nr_timeout,     0,      65535 },
  { "16 Persistance                                ", &nr_persistance, 0,        255 },
  { "17 Slot time (10msec increments)              ", &nr_slottime,    0,        127 },
  { "18 Link T1 timeout 'FRACK' (ms)               ", &T1init,         1, 0x7fffffff },
  { "19 Link TX window size 'MAXFRAME' (frames)    ", &Maxframe,       1,          7 },
  { "20 Link maximum tries (0=forever)             ", &N2,             0,        127 },
  { "21 Link T2 timeout (ms)                       ", &T2init,         1, 0x7fffffff },
  { "22 Link T3 timeout (ms)                       ", &T3init,         0, 0x7fffffff },
  { "23 AX.25 digipeating  (0=off 1=dumb 2=s&f)    ", &Digipeat,       0,          2 },
  { "24 Validate callsigns (0=off 1=on)            ", &nr_callcheck,   0,          1 },
  { "25 Station ID beacons (0=off 1=after 2=every) ", &nr_beacon,      0,          2 },
  { "26 CQ UI frames       (0=off 1=on)            ", &nr_cq,          0,          1 },
  { "27 Maximum transport circuits (0=unlimited)   ", &nr_maxcircuits, 0,      65535 },
  /* Past the end of the classic list, which stops at 27 - so a number that
   * cannot be confused with one another implementation also has.
   */
  { "28 INP3 routes before NET/ROM (0=off 1=on)    ", &nr_inp3first,   0,          1 }
};

#define NPARMS 28

static const uint8 L3RTT[] = {
  'L'<<1, '3'<<1, 'R'<<1, 'T'<<1, 'T'<<1, ' '<<1, 0<<1
};

struct link;

static struct node *nodeptr(const uint8 *call, int create);
static void send_packet_to_neighbor(struct mbuf **data, struct node *pn);
static void send_broadcast_packet(struct mbuf **bpp);
static void link_manager_initialize(void);
static struct linkinfo *linkinfoptr(struct node *node1, struct node *node2);
static int update_link(struct node *node1, struct node *node2, int source, int quality);
static int link_valid(struct node *pn, struct link *pl);
static void calculate_hopcnts(struct node *pn);
static void calculate_qualities(struct node *pn);
static void calculate_all(void);
static void broadcast_recv(struct mbuf **bpp, struct node *pn);
static struct mbuf *alloc_broadcast_packet(void);
static void send_broadcast(void *arg);
static void route_packet(struct mbuf **bpp, struct node *fromneighbor);
static void send_l3_packet(uint8 *source, uint8 *dest, int ttl, struct mbuf **data);
static void routing_manager_initialize(void);
static void reset_t1(struct circuit *pc);
static int nrbusy(struct circuit *pc);
static void send_l4_packet(struct circuit *pc, int opcode, struct mbuf **data);
static void try_send(struct circuit *pc, int fill_sndq);
static void set_circuit_state(struct circuit *pc, enum netrom_state newstate);
static void l4_t1_timeout(void *arg);
static void l4_t3_timeout(void *arg);
static void l4_t4_timeout(void *arg);
static struct ax25_cb *neighbour_link(uint8 *call);
static struct circuit *create_circuit(void);
static void circuit_manager(struct mbuf **bpp, const uint8 *answeras);
static int nr_proxy_l4(struct mbuf **bpp);
static void nr_proxy_open_peer(struct circuit *left);
static void proxy_recv_upcall(struct circuit *pc, int cnt);
static void proxy_send_upcall(struct circuit *pc, int cnt);
static void proxy_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate);
static void nrserv_recv_upcall(struct circuit *pc, int cnt);
static void nrserv_send_upcall(struct circuit *pc, int cnt);
static void nrserv_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate);
static void nrclient_parse(char *buf, int n);
static void nrclient_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate);
static int donconnect(int argc, char *argv[], void *p);
static int dobroadcast(int argc, char *argv[], void *p);
static int dofilter(int argc, char *argv[], void *p);
static int doident(int argc, char *argv[], void *p);
static int donkick(int argc, char *argv[], void *p);
static int dolinks(int argc, char *argv[], void *p);
static int donrpeer(int argc, char *argv[], void *p);
static int donodes(int argc, char *argv[], void *p);
static int doparms(int argc, char *argv[], void *p);
static int donreset(int argc, char *argv[], void *p);
static int donstatus(int argc, char *argv[], void *p);

/*---------------------------------------------------------------------------*/
/******************************** Link Manager *******************************/
/*---------------------------------------------------------------------------*/

#define IDENTLEN     6
#define INFINITY   999

struct broadcast {
  struct ax25 hdr;
  struct iface *iface;
  /* Switched off rather than deleted, for the reason the listen entries give:
   * an entry that vanishes is one the operator cannot see any more.  This is
   * what makes the node broadcast interval - which is one number for the
   * whole node - answerable per port: learn nodes over axudp without filling
   * an HF access with broadcasts of our own.
   */
  int disabled;
  struct broadcast *next;
};

/* One destination as it stands between us and ONE partner - BOTH directions.
 * This is the second routing table: the graph that calculate_all() works on
 * stays exactly as it was, and these hang beside it.
 *
 * Not a second NODE table.  DB0XYZ is the same node however we heard of him -
 * his alias, his IP, the options he carries belong to HIM - and only "how
 * fast, through whom" belongs to the way.  TNN keeps it the same way round:
 * one nodetab, and a routes array per peer indexed by the same entry, holding
 * quality AND reported_quality.  "reported" here is that second field, and it
 * is what makes the sending side possible at all: INP3 announces CHANGES, so
 * something has to remember what was last said, per partner and destination.
 */

struct nrinp3 {
  struct nrinp3 *next;
  struct nrpeer *peer;          /* the partner this cell is about */
  int time;                     /* HIS route time to it, 10 ms units.  0 means
                                 * he reports no way - the cell then lives on
                                 * only for the sake of "reported" below */
  int hops;
  long stamp;                   /* secclock() when last heard */
  int reported;                 /* what WE last told him about it, 10 ms
                                 * units, 0 = nothing outstanding.  A cell with
                                 * neither time nor reported is deleted */
};

struct node {
  uint8 *call;
  char ident[IDENTLEN];
  int hopcnt;
  struct link *links;
  struct nrinp3 *inp3;          /* the INP3 ways to him, one per partner */
  int32 inp3_ip;                /* INP3 option 0x01: he is also reachable at
                                 * this address.  0 = he named none */
  int inp3_ipbits;
  /* Options we do not know, kept exactly as they arrived - length byte, type
   * byte and payload - so that we can put them back on the wire unchanged.
   * THIS IS THE ROAD TO IPv6 and it costs nothing but the bytes: a field type
   * neither we nor the node in the middle understands still reaches the far
   * end, so a 16-byte address needs no flag and no agreement, only that
   * everybody in between passes on what it cannot read.  TNN does this and it
   * is the one extension mechanism the protocol has.
   */
  uint8 *inp3_opts;
  int inp3_optlen;
  struct node *neighbor, *old_neighbor;
  double quality, old_quality, tmp_quality;
  int force_broadcast;
  struct iface *iface;          /* Port he was last heard on, for a filter
				 * written per port.  Null for a node we only
				 * ever heard ABOUT - which is right: a filter
				 * is about the station we talk to. */
  struct node *prev, *next;
};

/* The stations we are configured to run an INP3 interlink with.
 *
 * NET/ROM has never had a list of neighbours and has not needed one:
 * nr3_input() makes a neighbour of whoever sends an L3 frame and
 * calculate_all() works out the rest.  That is right for a protocol that
 * broadcasts, and not enough for one that runs connected - INP3 has to be
 * told whom to call, which is the same thing "flexnet link add" says and for
 * the same reason.  It is deliberately NOT the route filter: that decides
 * what flows once somebody speaks to us, this decides whom we speak to.
 *
 * WRITTEN WITHOUT AN SSID THE ENTRY MATCHES ANY.  The callsign an interlink
 * runs under and the node's own ID need not carry the same one - TNN keeps
 * both views side by side, find_node_this_ssid() against
 * find_node_ssid_range() - and a sysop who writes the base callsign means the
 * station.  With an SSID it is exact.
 */

struct nrpeer {
  struct nrpeer *next;
  uint8 call[AXALEN];
  int anyssid;                  /* written without one: any SSID will do */
  int id;                       /* id of the link we last saw.  A different
                                 * one is a different connection, and nothing
                                 * agreed on the old one still holds. */
  /* The round trip measurement.  Everything is in 10 ms units, which is what
   * INP3 speaks - the granularity of the specification and of every value on
   * the wire.
   */
  int32 rttstart;               /* msclock() when the probe went out, 0 when
                                 * none is outstanding */
  int32 rttid;                  /* the number we wrote into it, echoed back
                                 * unchanged, so a late answer to an older
                                 * probe is not taken for this one */
  int srtt;                     /* smoothed ONE WAY time, 0 = never measured.
                                 * Half the round trip: that is the SNTT the
                                 * specification adds to a reported route
                                 * time. */
  int lastrtt;                  /* the last round trip, unsmoothed, for the
                                 * display - a smoothed value alone hides a
                                 * link that has just got worse */
  int hissrtt;                  /* HIS measurement of the link to us, out of
                                 * his own L3RTT frame.  0 = he has not got
                                 * one yet, and then there is no point telling
                                 * him anything: he adds this to every route
                                 * time we send, and without it every one of
                                 * them arrives short by the length of the
                                 * link.  TNN waits for both measurements
                                 * before it announces, and this is the half
                                 * that is not ours. */
  int inp3;                     /* he sent "$N": he speaks INP3 */
  int maxtime;                  /* his "$M": do not tell him about anything
                                 * slower than this.  0 = he named none */
  int rttcount;                 /* service ticks since the last probe */
  int sweepcount;               /* service ticks since the last full pass */
};

static struct nrpeer *nrpeers;
static struct timer nrpeer_timer;

/* Defined with the routing table further down, needed by the link service
 * above it: a partner that goes away takes his ways with him, and a partner
 * that is up gets told what has changed.
 */
static void inp3_drop_peer(struct nrpeer *pp);
static void inp3_inform_peer(struct nrpeer *pp, struct ax25_cb *axp);

/* Two rates out of one timer, the way TNN does it: the tick is short so that
 * a link coming up is NOTICED soon - the first measurement belongs to the
 * moment it stands, not to the next probe interval - and probing itself is
 * counted in ticks, at TNN's L3_RTT_TIME of 180 seconds.  There is no reason
 * to be noisier than the implementation we measure against.
 *
 * A tick costs nothing when everything is up: it is a find_ax25() per
 * partner.  It costs a connection attempt for a station that is not there,
 * and AX.25 retries do that waiting anyway.
 */

#define NRPEER_INTERVAL 10      /* seconds per tick */
#define NRRTT_EVERY     18      /* ticks between probes: 180 s */
/* Changes go out on EVERY tick, which is TNN's brosrv10() and is not an
 * accident of our timer: the specification allows positive news to be held
 * back but guarantees a maximum delay of 10 seconds for negative news, and
 * one tick is that guarantee.
 *
 * The full pass is the safety net under a protocol that only sends changes.
 * TNN runs brosrv360() hourly, which clears every reported value so that the
 * change mechanism has to say everything again.  Nothing should depend on it;
 * it is there for the message that went missing.
 */
#define NRSWEEP_EVERY  360      /* ticks between full passes: one hour */

static struct broadcast *broadcasts;
static struct node *nodes, *mynode;
static int nnodes;              /* entries on the nodes list, see nr_maxdest */

/*---------------------------------------------------------------------------*/

/* Returns 0 if the node is not known and may not be created: either the
 * destination list is full or there is no memory.  Every caller has to cope
 * with that - the table used to grow for as long as the entries kept coming,
 * and a nodes broadcast carries as many as the sender puts in it, which over
 * AXUDP is not limited by an AX.25 frame.  Each one costs a struct node and a
 * linkinfo, and calculate_all() then walks all of them.
 *
 * The limit is parameter 1, "Maximum destination list entries", which has
 * been in the parameter table and settable all along, marked "not used".
 */

static struct node *nodeptr(const uint8 *call, int create)
{
  struct node *pn;

  for (pn = nodes; pn && !addreq(call, pn->call); pn = pn->next) ;
  if (!pn && create) {
    if (nr_maxdest > 0 && nnodes >= nr_maxdest)
      return NULL;
    pn = (struct node *) calloc(1, sizeof(struct node));
    if (!pn)
      return NULL;
    pn->call = (uint8 *) malloc(AXALEN);
    if (!pn->call) {
      free(pn);
      return NULL;
    }
    addrcp(pn->call, call);
    memset(pn->ident, ' ', IDENTLEN);
    pn->hopcnt = INFINITY;
    if (nodes) {
      pn->next = nodes;
      nodes->prev = pn;
    }
    nodes = pn;
    nnodes++;
  }
  return pn;
}

/*---------------------------------------------------------------------------*/

static void send_packet_to_neighbor(struct mbuf **bpp, struct node *pn)
{

  struct ax25_cb *axp;

  if (!(axp = neighbour_link(pn->call))) {
    if (update_link(mynode, pn, 1, 0)) calculate_all();
    free_p(bpp);
    return;
  }
  pushdown(bpp, NULL, 1);
  (*bpp)->data[0] = PID_NETROM;
  send_ax25(axp, bpp, -1);
}

/*---------------------------------------------------------------------------*/

/* Does this port get our destinations, or only the fact that we exist?
 * "feed" is per port here and not per station, because that is how NET/ROM
 * announces - one UI frame for everybody on the port.
 */

static int broadcast_feeds(const struct broadcast *p)
{
  return rf_feed(RF_NETROM, NULL, p->iface);
}

/*---------------------------------------------------------------------------*/

static void broadcast_to(struct broadcast *p, struct mbuf **bpp)
{
  struct mbuf *bp;

  /* The one send in the node that does not go through ax_send_ui(): the UI
   * and the protocol id are written into the buffer by
   * alloc_broadcast_packet() and the frame goes straight to the driver.  So
   * the port's protocol gate is asked here by name, or "pid out block netrom"
   * would stop everything but the nodes broadcast.
   */
  if (pid_blocked(p->iface, PF_OUT, PID_NETROM)) return;
  addrcp(p->hdr.source, p->iface->hwaddr);
  dup_p(&bp, *bpp, 0, MAXINT16);
  htonax25(&p->hdr, &bp);
  if (p->iface->forw)
    (*p->iface->forw->raw)(p->iface->forw, &bp);
  else
    (*p->iface->raw)(p->iface, &bp);
}

/*---------------------------------------------------------------------------*/

static void send_broadcast_packet(struct mbuf **bpp)
{

  struct broadcast *p;

  for (p = broadcasts; p; p = p->next)
    if (!p->disabled && broadcast_feeds(p))
      broadcast_to(p, bpp);
  free_p(bpp);
}

/*---------------------------------------------------------------------------*/

static void link_manager_initialize(void)
{

  if (!(mynode = nodeptr(Mycall, 1))) {
    printf("netrom: cannot create own node entry\n");
    return;
  }
  free(mynode->call);
  mynode->call = Mycall;
  calculate_all();
}

/*---------------------------------------------------------------------------*/
/****************************** Routing Manager ******************************/
/*---------------------------------------------------------------------------*/

#define PERMANENT       0x7fffffff      /* Max long integer */

struct link {
  struct node *node;
  struct linkinfo *info;
  struct link *prev, *next;
};

struct linkinfo {
  int source;
  int quality;
  long time;
};

struct routes_stat {
  int rcvd;
  int sent;
};

static struct iface *Nr_iface;
static struct routes_stat routes_stat;
static struct timer broadcast_timer;

/*---------------------------------------------------------------------------*/

static struct linkinfo *linkinfoptr(struct node *node1, struct node *node2)
{

  struct link *pl;
  struct linkinfo *pi;

  for (pl = node1->links; pl; pl = pl->next)
    if (pl->node == node2) return pl->info;
  pi = (struct linkinfo *) calloc(1, sizeof(struct linkinfo));
  pi->source = INFINITY;
  pl = (struct link *) calloc(1, sizeof(struct link));
  pl->node = node2;
  pl->info = pi;
  if (node1->links) {
    pl->next = node1->links;
    node1->links->prev = pl;
  }
  node1->links = pl;
  pl = (struct link *) calloc(1, sizeof(struct link));
  pl->node = node1;
  pl->info = pi;
  if (node2->links) {
    pl->next = node2->links;
    node2->links->prev = pl;
  }
  node2->links = pl;
  return pi;
}

/*---------------------------------------------------------------------------*/

static int update_link(struct node *node1, struct node *node2, int source, int quality)
{

  int ret;
  struct linkinfo *pi;

  if (node1 == node2) return 0;
  if (source > node1->hopcnt + 1 || source > node2->hopcnt + 1) return 0;
  pi = linkinfoptr(node1, node2);
  if (source > pi->source || pi->time == PERMANENT) return 0;
  ret = 0;
  if (pi->source != source) {
    pi->source = source;
    ret = 1;
  }
  if (quality > 255) quality = 255;
  if (pi->quality != quality) {
    pi->quality = quality;
    ret = 1;
  }
  pi->time = secclock();
  return ret;
}

/*---------------------------------------------------------------------------*/

static int link_valid(struct node *pn, struct link *pl)
{
  if (pl->info->time == PERMANENT) return 1;
  if (nr_obsinit && nr_bdcstint) {
    if (pl->info->time + nr_obsinit * nr_bdcstint < secclock()) return 0;
  }
  if (pl->info->source > pn->hopcnt + 1) return 0;
  if (pl->info->source > pl->node->hopcnt + 1) return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

/* Is this callsign a node we have a working link with?  Asked before a text
 * service is started on an incoming link: a neighbour called to speak L3, not
 * to be greeted.  See axserv_connected().
 */

int nr_is_neighbour(const uint8 *call)
{

  struct link *pl;
  struct node *pn;

  if (!mynode || !(pn = nodeptr(call, 0)) || pn == mynode) return 0;
  for (pl = mynode->links; pl; pl = pl->next)
    if (pl->node == pn) return link_valid(mynode, pl);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Is this callsign one of the configured interlink partners?  The SSID is
 * compared only when the entry carries one - see struct nrpeer.
 */

static int nrpeer_match(const struct nrpeer *pp, const uint8 *call)
{
  if (pp->anyssid) return !memcmp(pp->call, call, ALEN);
  return addreq(pp->call, call);
}

static struct nrpeer *nrpeer_find(const uint8 *call)
{
  struct nrpeer *pp;

  for (pp = nrpeers; pp; pp = pp->next)
    if (nrpeer_match(pp, call)) return pp;
  return 0;
}

int nr_is_peer(const uint8 *call)
{
  return nrpeer_find(call) != NULL;
}

/*---------------------------------------------------------------------------*/

/* Which callsign to CALL for this entry, or nothing if we cannot know.
 *
 * With an SSID written it is that one.  Without, the entry says "whichever he
 * uses" - which answers who may connect to us, and does not answer whom to
 * connect to.  Guessing SSID 0 would be a guess.  But if we have heard of him
 * we no longer need to guess: the node table has the callsign he really uses,
 * and that is what "netrom peer" shows under "Known as".  Until then we wait
 * for him, which is what an entry written that way is for.
 */

static uint8 *nrpeer_target(struct nrpeer *pp)
{
  struct node *pn;

  if (!pp->anyssid) return pp->call;
  for (pn = nodes; pn; pn = pn->next)
    if (pn != mynode && nrpeer_match(pp, pn->call)) return pn->call;
  return NULL;
}

/*---------------------------------------------------------------------------*/

/* The interlink itself: keep an AX.25 connection to every configured partner.
 *
 * L3 frames already travel connected - send_packet_to_neighbor() opens a link
 * when it has something to send - so this adds one thing only, and INP3 needs
 * exactly that one: the link stands BEFORE there is traffic.  A protocol that
 * announces changes has nothing to send at the moment it comes up, and the
 * round trip measurement it uses for its metric has to run on a link that is
 * already there.
 *
 * The connection is looked up rather than remembered, the way FlexNet's
 * setaxp() does it: an ax25_cb can go away underneath us and a stored pointer
 * would be the one thing that breaks.  What is remembered is its id, because
 * a new id means a new connection and nothing agreed on the old one holds.
 */

/* Is this a connection we have not seen before, and if so, forget the old one.
 *
 * Counted as ours only once it is UP.  Remembering the id while the
 * connection is still being set up would spend the "new" on a link nothing
 * can be agreed on yet - and the first measurement, which belongs exactly
 * there, would then wait for the next probe interval instead.
 *
 * IT HAS TO BE ASKED AT EVERY FRAME WE TAKE OFF THE LINK, not only on the
 * timer tick, and that was measured rather than reasoned: a partner announces
 * himself the moment the link stands, so his flags and our first measurement
 * arrive within milliseconds of it - and a reset arriving on the next tick
 * threw away precisely those.  The announcement was thirty seconds late on a
 * link that had been up for one.
 */

static int nrpeer_isnew(struct nrpeer *pp, struct ax25_cb *axp)
{
  if (!axp || axp->state != LAPB_CONNECTED || pp->id == axp->id) return 0;
  pp->id = axp->id;
  pp->srtt = pp->lastrtt = pp->rttstart = pp->hissrtt = 0;
  pp->inp3 = pp->maxtime = 0;
  pp->rttcount = pp->sweepcount = 0;
  /* A new connection: he announces again from scratch, and what the old one
   * taught us was true of a link that no longer exists.  What WE told him
   * goes with it - the cell holds both halves - so we begin by telling him
   * everything again.
   */
  inp3_drop_peer(pp);
  return 1;
}

/*---------------------------------------------------------------------------*/

/* The partner's link, opened if it is not there.  Looked up rather than
 * remembered, the way FlexNet's setaxp() does it.
 */

/* THE LINK TO A NEIGHBOUR, under the callsign of the port he is reached over.
 * Which port that is only the routing table knows, so the path is resolved
 * first and the pair looked up with what it says - the same order open_ax25()
 * follows, and for the same reason: our own callsign does not exist until
 * axroute() has stamped it.
 *
 * Asking for "any link to him" instead, as this did, hands NET/ROM whatever
 * is open to that station - a user session someone opened under a different
 * ssid included, and then his L3 frames ride in it.  Unlike FlexNet there is
 * no protocol reason to prefer some other callsign of ours: the neighbour
 * does not read our address as an anchor for anything, he answers to what he
 * hears.
 */

static struct ax25_cb *neighbour_link(uint8 *call)
{
  struct ax25 hdr;
  struct ax25_cb *axp;
  struct iface *ifp;

  memset(&hdr, 0, sizeof(hdr));
  addrcp(hdr.dest, call);
  ax25_resolve_path(&hdr, &ifp, 0);
  if (ifp != NULL && (axp = find_ax25(hdr.source, call)) != NULL)
    return axp;
  /* Not there, so open it - with a FRESH header.  axroute() has been over
   * that one already, and it is not idempotent: it inserts digipeaters and
   * stamps the source, so running it twice is not running it once.
   */
  memset(&hdr, 0, sizeof(hdr));
  addrcp(hdr.dest, call);
  return open_ax25(&hdr, AX_ACTIVE, 0);
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *nrpeer_link(struct nrpeer *pp, int *isnew)
{
  uint8 *call;
  struct ax25_cb *axp;

  if (isnew) *isnew = 0;
  if (!(call = nrpeer_target(pp))) return NULL;
  if (!(axp = neighbour_link(call))) return NULL;
  if (nrpeer_isnew(pp, axp) && isnew) *isnew = 1;
  return axp;
}

/*---------------------------------------------------------------------------*/

/* The same question asked from a frame rather than from the timer: which
 * connection did this arrive on, and is it the one we think we have?
 */

static void nrpeer_seen(struct nrpeer *pp)
{
  struct ax25_cb *axp;
  uint8 *call;

  if ((call = nrpeer_target(pp)) && (axp = find_ax25(NULL, call)))
    nrpeer_isnew(pp, axp);
}

/*---------------------------------------------------------------------------*/

/* Smoothing, with the constants TNN uses (l3tab.c smooth(), l3local.h): seven
 * eighths of the old value plus one eighth of the new, and a FIRST
 * measurement counted threefold.  The threefold is not caution about noise,
 * it is caution about being believed: a link announced as fast draws traffic,
 * and one sample is not evidence.  It settles within a few probes.
 *
 * THE FLOOR MATTERS MORE THAN THE FORMULA.  Zero means WITHDRAWN in INP3, so
 * a link fast enough to round to nothing would announce itself as dead - over
 * axudp that is every link.  TNN guards it in four places and this is one of
 * them; 10 ms is the smallest value the protocol can say at all.
 */

#define NRRTT_MIN       1       /* 10 ms, and never 0 - that means withdrawn */
#define NRRTT_MAX   59999       /* 599.99 s; above is the horizon,= dead */
#define NRRTT_BETA      3       /* first measurement counts threefold */

static void nrpeer_smooth(int *old, int val)
{
  if (val < NRRTT_MIN) val = NRRTT_MIN;
  if (val > NRRTT_MAX) {
    *old = 0;                   /* beyond the horizon: start again */
    return;
  }
  *old = *old ? ((*old + 1) * 7 - 1 + val) / 8 : val * NRRTT_BETA;
  if (*old < NRRTT_MIN) *old = NRRTT_MIN;
}

/*---------------------------------------------------------------------------*/

/* Send one L3RTT probe.  The far end reflects it unchanged - which WE have
 * always done, route_packet() has answered these since long before this file
 * had anything to measure - and the time it takes to come back is the round
 * trip.
 *
 * The frame is a NET/ROM L3 header addressed to the pseudo destination
 * "L3RTT", carrying what looks like an L4 info packet whose text begins
 * "L3RTT:".  Four decimal numbers follow, written the way TNN writes them
 * (" %10lu"), then our alias and version, then the flags.  A reader takes
 * them with sscanf and does not care about the widths; they are copied
 * because being byte for byte what the other implementations send costs
 * nothing and removes a question.
 *
 * IT IS PADDED TO THE INTERFACE MTU on purpose, as TNN pads it.  A short
 * frame measures latency; a full one measures what a full frame costs, and
 * that is what the metric is for.
 */

static void nrpeer_send_rtt(struct nrpeer *pp, struct ax25_cb *axp)
{
  char buf[256];
  int len;
  int mtu;
  struct mbuf *bp;
  uint8 *cp;

  if (!mynode || !axp || axp->state != LAPB_CONNECTED) return;

  /* An id that is ours and not a pointer.  TNN puts its peer pointer here
   * and reads it back with "%lu" into a pointer, which cannot be right on a
   * 64 bit machine; a counter says the same thing and always fits.
   */
  if (!++pp->rttid) pp->rttid = 1;

  len = sprintf(buf, "L3RTT: %10lu %10lu %10lu %10lu ",
		(unsigned long) (msclock() / 10),
		(unsigned long) pp->srtt,
		(unsigned long) pp->lastrtt,
		(unsigned long) pp->rttid);
  memcpy(buf + len, mynode->ident, IDENTLEN);
  len += IDENTLEN;
  len += sprintf(buf + len, " LEVEL3_V2.1 WAMPES");
  /* "$N" says we speak INP3.  TNN sends it only when it WANTS an INP link,
   * and switches the peer over when it sees ours - so this word is the whole
   * negotiation.  "$I" for the IP option does not exist in TNN; that field
   * travels unannounced.
   */
  len += sprintf(buf + len, " $N\r");

  mtu = axp->iface ? axp->iface->mtu : 256;
  if (mtu > (int) sizeof(buf)) mtu = sizeof(buf);
  while (len < mtu) buf[len++] = ' ';

  if (!(bp = alloc_mbuf(NR3HLEN + NR4MINHDR + len + 1))) return;
  cp = bp->data;
  *cp++ = PID_NETROM;
  addrcp(cp, mynode->call);
  cp += AXALEN;
  addrcp(cp, L3RTT);
  cp += AXALEN;
  *cp++ = 2;                            /* time to live, as TNN sends it */
  *cp++ = 0;                            /* circuit index */
  *cp++ = 0;                            /* circuit id */
  *cp++ = 0;                            /* tx sequence */
  *cp++ = 0;                            /* rx sequence */
  *cp++ = NR4OPINFO;
  memcpy(cp, buf, (size_t) len);
  cp += len;
  bp->cnt = cp - bp->data;

  pp->rttstart = msclock();
  send_ax25(axp, &bp, -1);
}

/*---------------------------------------------------------------------------*/

/* The text of an L3RTT frame, as a string we can read.  It starts at the
 * fixed offset the caller has already checked "L3RTT:" at, and it is not
 * terminated on the wire - TNN pads it with spaces to the MTU.
 */

static int nrpeer_rtt_text(struct mbuf *bp, char *buf, int size)
{
  int n = bp->cnt - (2 * AXALEN + 6);

  if (n <= 0) return 0;
  if (n > size - 1) n = size - 1;
  memcpy(buf, bp->data + 2 * AXALEN + 6, (size_t) n);
  buf[n] = '\0';
  return n;
}

/*---------------------------------------------------------------------------*/

/* What a neighbour says about himself, in the flags at the end of his own
 * L3RTT frame.  They are words beginning with "$", and TNN knows exactly two.
 */

static void nrpeer_read_flags(struct mbuf *bp, struct node *fromneighbor)
{
  char buf[200];
  char *cp;
  struct nrpeer *pp;
  unsigned long tic, hissrtt, hislast, id;

  if (!(pp = nrpeer_find(fromneighbor->call))) return;
  if (!nrpeer_rtt_text(bp, buf, sizeof(buf))) return;
  /* FIRST, and before anything is taken from the frame: it may be the first
   * thing on a connection we have not counted yet, and the reset would
   * otherwise undo exactly what we are about to read.  Measured: with these
   * two the wrong way round the first announcement was twenty seconds late,
   * because the value below was taken and then cleared again.
   */
  nrpeer_seen(pp);
  /* The second number in his frame is HIS smoothed time to US - the same
   * field we write our own srtt into.  It is not decoration: he adds it to
   * every route time we send him, so until he has one there is nothing worth
   * telling him.
   */
  if (sscanf(buf + 6, "%lu %lu %lu %lu", &tic, &hissrtt, &hislast, &id) == 4 &&
      hissrtt <= NRRTT_MAX)
    pp->hissrtt = (int) hissrtt;
  /* "$N" is the whole negotiation: he speaks INP3 and wants to.  A neighbour
   * that never sends it gets the classic broadcast from us and nothing else.
   */
  if (strstr(buf, "$N")) {
    /* THE MOMENT IT TURNS ON, MEASURE - and this is TNN's own answer to a
     * gap we walked into while testing.  Each side learns the other speaks
     * INP3 only from the other's probe, and probes are 180 s apart, so after
     * a link comes up the announcement could wait three minutes for a frame
     * that had nothing else to carry.  TNN answers a "$N" by announcing
     * itself at once; a probe of ours does both, because ours carries our
     * own "$N" and starts the measurement the metric needs.
     *
     * Only on the change from off to on, so that two nodes reading each
     * other's flags do not keep answering one another.
     */
    if (!pp->inp3) {
      struct ax25_cb *axp;
      uint8 *call = nrpeer_target(pp);

      pp->inp3 = 1;
      if (!pp->rttstart && call && (axp = find_ax25(NULL, call)))
	nrpeer_send_rtt(pp, axp);
    }
  }
  /* "$M<n>" is a ceiling HE sets on what we report to him.  Absent means he
   * named none, and TNN clears it in that case rather than keeping the last.
   */
  pp->maxtime = (cp = strstr(buf, "$M")) ? atoi(cp + 2) : 0;
}

/*---------------------------------------------------------------------------*/

/* Our own probe, reflected by the far end.  The time it took is the round
 * trip; half of it is the one way time the specification calls SNTT and adds
 * to every route time a neighbour reports.
 */

static void nrpeer_recv_rtt(struct mbuf *bp, struct node *fromneighbor)
{
  char buf[200];
  int32 rtt;
  struct nrpeer *pp;
  unsigned long sent, hissrtt, hislast, id;

  if (!(pp = nrpeer_find(fromneighbor->call))) return;
  if (!nrpeer_rtt_text(bp, buf, sizeof(buf))) return;
  if (sscanf(buf + 6, "%lu %lu %lu %lu", &sent, &hissrtt, &hislast, &id) != 4)
    return;
  /* If this arrived on a connection we had not counted, the probe it answers
   * belonged to the previous one - nrpeer_seen() clears it, and the test
   * below then rejects the reflection rather than timing it against a clock
   * that has been restarted.
   */
  nrpeer_seen(pp);
  /* Only the probe that is outstanding, and only if the number came back
   * unchanged: a reflection of an older one would otherwise be timed against
   * the newer clock and read as absurdly fast.
   */
  if (!pp->rttstart || (int32) id != pp->rttid) return;

  /* The +2 is TNN's, and it is the reason a fast link does not measure zero:
   * over axudp the round trip is below the 10 ms the protocol can express,
   * and zero on the wire means WITHDRAWN.
   */
  rtt = (msclock() - pp->rttstart) / 10 + 2;
  pp->rttstart = 0;
  if (rtt > NRRTT_MAX) {
    pp->srtt = 0;               /* beyond the horizon: as good as unmeasured */
    return;
  }
  pp->lastrtt = (int) rtt;
  nrpeer_smooth(&pp->srtt, (int) (rtt / 2));
}

/*---------------------------------------------------------------------------*/

/* One partner, one tick: hold the link and decide whether to probe.  Pulled
 * out so that "netrom peer add" runs exactly what the timer runs - a second
 * copy of this is how the two would drift apart.
 */

static void nrpeer_poll(struct nrpeer *pp)
{
  int isnew;
  struct ax25_cb *axp;

  {
    if (!(axp = nrpeer_link(pp, &isnew)) ||
	axp->state != LAPB_CONNECTED) {
      pp->sweepcount = 0;
      /* No link, no ways through him.  Held any longer they would be routes
       * to a partner we cannot reach, and nothing would ever withdraw them -
       * the withdrawal would have had to come over the link that is gone.
       */
      if (pp->srtt || pp->inp3) {
	pp->srtt = pp->lastrtt = pp->rttstart = 0;
	pp->inp3 = pp->maxtime = 0;
	inp3_drop_peer(pp);
      }
      return;
    }
    /* A new connection has no measurement behind it, whatever we knew about
     * the old one.  Probe at once rather than at the next interval: until
     * there is a time, there is nothing to announce him with.
     */
    /* Everything the new connection invalidates was dropped by
     * nrpeer_isnew() already, wherever it was first noticed.  What is left
     * here is the one thing the timer is for: measure at once rather than at
     * the end of the first interval, because until there is a time there is
     * nothing to announce with.
     */
    if (isnew) {
      nrpeer_send_rtt(pp, axp);
      return;
    }
    /* One outstanding probe at a time.  Beyond the horizon the link counts as
     * unmeasured again - TNN disconnects after 180 s, we let the next probe
     * decide, since our timer comes round anyway.
     */
    if (pp->rttstart) {
      if (msclock() - pp->rttstart > NRRTT_MAX * 10L) {
	pp->rttstart = 0;
	pp->srtt = 0;
      }
    } else if (++pp->rttcount >= NRRTT_EVERY) {
      pp->rttcount = 0;
      nrpeer_send_rtt(pp, axp);
    }
    /* And then what has changed, which is NOT tied to the probe: measuring is
     * every 180 s and announcing is every tick, because ten seconds is the
     * maximum delay the specification allows itself for bad news.
     */
    inp3_inform_peer(pp, axp);
  }
}

/*---------------------------------------------------------------------------*/

static void nrpeer_service(void *arg)
{
  struct nrpeer *pp;

  (void) arg;
  set_timer(&nrpeer_timer, NRPEER_INTERVAL * 1000L);
  start_timer(&nrpeer_timer);

  for (pp = nrpeers; pp; pp = pp->next)
    nrpeer_poll(pp);
}

/*---------------------------------------------------------------------------*/

/* What the filter says about this node.  The port is the one he was last
 * heard on; a node we only ever heard ABOUT has none, and then only a rule
 * naming his callsign or the default applies - which is right, because we do
 * not talk to him and a rule written per port is about who we talk to.
 */

static enum rf_in node_in(const struct node *pn)
{
  return rf_in(RF_NETROM, pn->call, pn->iface);
}

static int node_advert(const struct node *pn)
{
  return rf_advert(RF_NETROM, pn->call, pn->iface);
}

/*---------------------------------------------------------------------------*/

static void calculate_hopcnts(struct node *pn)
{

  int hopcnt;
  struct link *pl;

  hopcnt = pn->hopcnt + 1;
  for (pl = pn->links; pl; pl = pl->next)
    if (link_valid(pn, pl) && pl->node->hopcnt > hopcnt) {
      pl->node->hopcnt = hopcnt;
      calculate_hopcnts(pl->node);
    }
}

/*---------------------------------------------------------------------------*/

static void calculate_qualities(struct node *pn)
{

  double quality;
  struct link *pl;

  for (pl = pn->links; pl; pl = pl->next) {
    quality = pn->tmp_quality * pl->info->quality / 256.0;
    if (pl->node->tmp_quality < quality) {
      pl->node->tmp_quality = quality;
      calculate_qualities(pl->node);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void calculate_all(void)
{

  int start_broadcast_timer;
  struct link *pl1, *plnext;
  struct link *pl;
  struct node *neighbor;
  struct node *pn1, *pnnext;
  struct node *pn;

  /*** preset hopcnt, neighbor, and quality ***/

  for (pn = nodes; pn; pn = pn->next) {
    pn->hopcnt = INFINITY;
    pn->old_neighbor = pn->neighbor;
    pn->neighbor = 0;
    pn->old_quality = pn->quality;
    pn->quality = 0.0;
  }
  mynode->hopcnt = 0;

  /*** calculate new hopcnts ***/

  calculate_hopcnts(mynode);

  /*** remove invalid links ***/

  for (pn = nodes; pn; pn = pn->next)
    for (pl = pn->links; pl; pl = plnext) {
      plnext = pl->next;
      if (!link_valid(pn, pl)) {
	if (pl->prev)
	  pl->prev->next = pl->next;
	else
	  pn->links = pl->next;
	if (pl->next) pl->next->prev = pl->prev;
	pn1 = pl->node;
	/* This assumed that every link has its counterpart.  If that ever
	 * stops being true it should not be a null dereference.
	 */
	for (pl1 = pn1->links; pl1 && pl1->node != pn; pl1 = pl1->next) ;
	if (pl1) {
	  if (pl1->prev)
	    pl1->prev->next = pl1->next;
	  else
	    pn1->links = pl1->next;
	  if (pl1->next) pl1->next->prev = pl1->prev;
	}
	free(pl->info);
	free(pl1);
	free(pl);
      }
    }

  /*** calculate new neighbor and quality values ***/

  for (pl = mynode->links; pl; pl = pl->next) {
    for (pn = nodes; pn; pn = pn->next) pn->tmp_quality = 0.0;
    mynode->tmp_quality = 256.0;
    neighbor = pl->node;
    neighbor->tmp_quality = pl->info->quality;
    calculate_qualities(neighbor);
    for (pn = nodes; pn; pn = pn->next)
      if (pn->quality < pn->tmp_quality ||
	  (pn->quality == pn->tmp_quality && neighbor == pn->old_neighbor)) {
	pn->quality = pn->tmp_quality;
	pn->neighbor = neighbor;
      }
  }
  mynode->neighbor = 0;
  mynode->quality = 256.0;

  /*** check changes ***/

  start_broadcast_timer = 0;
  for (pn = nodes; pn; pn = pn->next) {
    if (pn != mynode &&
	(pn->neighbor != pn->old_neighbor ||
	((int) pn->quality) != ((int) pn->old_quality)))
      pn->force_broadcast = 1;
    if (pn->force_broadcast) start_broadcast_timer = 1;
  }
  if (start_broadcast_timer) {
    set_timer(&broadcast_timer, 10 * 1000L);
#ifdef FORCE_BC
    start_timer(&broadcast_timer);
#endif
  }

  /*** remove obsolete nodes ***/

  for (pn = nodes; pn; pn = pnnext) {
    pnnext = pn->next;
    /* An INP3 way is a reason to keep him as good as a link is.  Without
     * this, a destination we know ONLY through INP3 - which has no edge in
     * the graph, because a RIP names no intermediate node to build one from -
     * is swept away here on the next pass, and its ways with it.
     */
    if (pn != mynode && !pn->links && !pn->inp3 && !pn->force_broadcast) {
      if (pn->prev)
	pn->prev->next = pn->next;
      else
	nodes = pn->next;
      if (pn->next) pn->next->prev = pn->prev;
      free(pn->call);
      free(pn->inp3_opts);
      free(pn);
      nnodes--;
    }
  }
}

/*---------------------------------------------------------------------------*/

/* The INP3 way to a destination through one partner, created on demand. */

static struct nrinp3 *inp3_route(struct node *pd, struct nrpeer *pp, int create)
{
  struct nrinp3 *rp;

  for (rp = pd->inp3; rp; rp = rp->next)
    if (rp->peer == pp) return rp;
  if (!create) return NULL;
  if (!(rp = (struct nrinp3 *) calloc(1, sizeof(struct nrinp3)))) return NULL;
  rp->peer = pp;
  rp->next = pd->inp3;
  pd->inp3 = rp;
  return rp;
}

/*---------------------------------------------------------------------------*/

/* The best INP3 way to a destination, and the comparison IS the
 * specification: target time = route time + our measured time to the
 * neighbour who reports it (TT = RT + SNTT).  Smallest wins.
 *
 * A partner we have not measured is skipped rather than counted as instant.
 * Without his SNTT the sum would be a route time alone, which is a different
 * quantity - and it would beat every measured way.
 */

static struct nrinp3 *inp3_best(const struct node *pd)
{
  struct nrinp3 *best = NULL;
  struct nrinp3 *rp;

  for (rp = pd->inp3; rp; rp = rp->next) {
    if (!rp->time) continue;    /* a cell kept only for what we reported */
    if (!rp->peer->srtt) continue;
    if (!best || rp->time + rp->peer->srtt < best->time + best->peer->srtt)
      best = rp;
  }
  return best;
}

/*---------------------------------------------------------------------------*/

/* WHICH WAY WOULD WE TAKE to this destination - and it is asked in three
 * places: route_packet() wants the next hop, an INP3 announcement wants the
 * time of it, our own nodes broadcast wants its quality.  Worked out
 * separately those three would drift apart, and a node that advertises one
 * way while forwarding down another is worse than one that does neither.
 *
 * Null means "the graph decides".  This is the whole of parameter 28: on, an
 * INP3 entry wins as soon as there is one; off, it is asked only where the
 * graph has nothing at all.
 */

static struct nrinp3 *inp3_preferred(const struct node *pd)
{
  if (!nr_inp3first && pd->neighbor && (int) pd->quality) return NULL;
  return inp3_best(pd);
}

/*---------------------------------------------------------------------------*/

/* WHOM TO SEND TO, out of the INP3 table - the one question route_packet()
 * needs answered, and the only place the two routing worlds have to meet.
 * Everything else about them can stay apart.
 *
 * Null when there is nothing here, and the caller then goes on to the graph.
 * A partner whose link has gone has no ways left at all - inp3_drop_peer()
 * takes them when it goes - so there is no need to test the link again.
 */

static struct node *inp3_nexthop(const struct node *pd, const struct node *from)
{
  struct nrinp3 *rp;
  struct node *via;
  uint8 *call;

  if (!(rp = inp3_preferred(pd))) return NULL;
  if (!(call = nrpeer_target(rp->peer))) return NULL;
  if (!(via = nodeptr(call, 0))) return NULL;
  /* NOT BACK THE WAY IT CAME.  Poison reverse should mean a partner never
   * offers us a way that runs through us, so this ought not to happen - but
   * if it does it is a loop between two nodes, and bouncing the datagram back
   * is the one answer that is certainly wrong.  Falling through to the graph
   * may still deliver it.
   */
  if (via == from) return NULL;
  return via;
}

/*---------------------------------------------------------------------------*/

static void inp3_route_drop(struct node *pd, struct nrpeer *pp)
{
  struct nrinp3 *rp, **rpp;

  for (rpp = &pd->inp3; (rp = *rpp); rpp = &rp->next)
    if (rp->peer == pp) {
      *rpp = rp->next;
      free(rp);
      return;
    }
}

/*---------------------------------------------------------------------------*/

/* He has no way to this destination any more.  That is not the same as having
 * nothing to do with it: if we have told him about it, the cell has to stay
 * until we have taken that back, because what is remembered there is OUR
 * announcement and not his.  With nothing outstanding it goes, and then the
 * node itself can be collected - calculate_all() reaps a node with no links
 * and no INP3 cells, which is how a destination we only ever heard of through
 * INP3 disappears again.
 */

static void inp3_route_clear(struct node *pd, struct nrpeer *pp)
{
  struct nrinp3 *rp;

  if (!(rp = inp3_route(pd, pp, 0))) return;
  rp->time = 0;
  rp->hops = 0;
  if (!rp->reported) inp3_route_drop(pd, pp);
}

/*---------------------------------------------------------------------------*/

/* Everything this partner told us, forgotten at once.
 *
 * THE LINK IS THE LIFETIME, and it has to be: INP3 sends CHANGES, so silence
 * from a partner means "nothing has changed" and ageing an entry out would
 * delete a way that is perfectly good.  What says a way is gone is either his
 * withdrawal or the link going away - and the second is this.
 */

static void inp3_drop_peer(struct nrpeer *pp)
{
  struct node *pn;

  for (pn = nodes; pn; pn = pn->next)
    inp3_route_drop(pn, pp);
}

/*---------------------------------------------------------------------------*/

/* A routing information frame, numbered on the interlink, signature 0xff.
 * After it come routing information packets, each
 *
 *      7  callsign, AX.25 shifted
 *      1  hop count
 *      2  route time, MSB first, 10 ms units
 *      n  options, each  1 length  1 type  data
 *      1  0x00, end of packet
 *
 * AND THE LENGTH BYTE COUNTS ITSELF AND THE TYPE.  The specification says the
 * IP option is "5 byte", meaning its payload; TNN writes 7 and reads 7, and
 * what TNN does is what is on the air.  Getting this wrong makes an
 * implementation that nobody understands, so it is worth the sentence.
 */

#define INP3_RIF        0xff
#define INP3_EOP        0x00
#define INP3_ALIAS      0x00
#define INP3_IPA        0x01
#define INP3_HORIZON   60000    /* 600 s: unreachable, never sent */
#define INP3_DEFAULT_LT   10    /* what TNN puts in place of a zero hop count */

static void inp3_recv(struct mbuf **bpp, struct node *pn)
{
  struct nrpeer *pp;

  if (!(pp = nrpeer_find(pn->call))) {
    /* Not a partner of ours.  He should not be sending these - an interlink
     * is agreed, not assumed - and taking routes from him would be exactly
     * the "everyone is a neighbour" the route filter was built against.
     */
    goto discard;
  }
  if (PULLCHAR(bpp) != INP3_RIF) goto discard;

  /* Each pass is one RIP.  The shortest possible is 11 bytes - callsign, hop
   * count, time, EOP - so anything shorter is the end of the frame.
   */
  while (*bpp && len_p(*bpp) >= 11) {
    char alias[IDENTLEN];
    int hops;
    int haveip = 0;
    int ipbits = 0;
    int optlen = 0;
    int time;
    int32 ip = 0;
    int valid = 1;
    struct node *pd;
    struct nrinp3 *rp;
    uint8 call[AXALEN];
    uint8 hdr[AXALEN + 3];
    uint8 opts[64];             /* options we do not know, kept verbatim.  The
                                 * bound is ours: what we pass on we also have
                                 * to fit into a frame, and an entry cannot be
                                 * allowed to grow without limit because
                                 * somebody upstream is generous */

    if (pullup(bpp, hdr, AXALEN + 3) != AXALEN + 3) break;
    addrcp(call, hdr);
    hops = hdr[AXALEN];
    time = (hdr[AXALEN+1] << 8) | hdr[AXALEN+2];
    memset(alias, 0, sizeof(alias));

    /* A hop count of nought must never be sent; TNN substitutes its default
     * rather than dropping the entry, and counts up on RECEIPT - so the
     * number we store already includes the step through us.
     */
    if (!hops) hops = INP3_DEFAULT_LT;
    else hops++;

    /* Options until the end of packet marker. */
    for (;;) {
      int len, type, i;
      uint8 data[256];

      if ((len = PULLCHAR(bpp)) < 0) { valid = 0; break; }
      if (len == INP3_EOP) break;
      if (len < 2) { valid = 0; break; }        /* cannot even hold its type */
      len -= 2;                                 /* length and type byte */
      if ((type = PULLCHAR(bpp)) < 0) { valid = 0; break; }
      if (len > (int) len_p(*bpp)) { valid = 0; break; }
      for (i = 0; i < len; i++) data[i] = (uint8) PULLCHAR(bpp);

      switch (type) {
      case INP3_ALIAS:
	for (i = 0; i < len && i < IDENTLEN; i++)
	  alias[i] = (data[i] >= ' ' && data[i] < 127) ? data[i] : ' ';
	for (; i < IDENTLEN; i++) alias[i] = ' ';
	break;
      case INP3_IPA:
	/* Four bytes of address and one of prefix length.  Bits outside
	 * 1..32 drop the ADDRESS and not the whole entry, which is what TNN
	 * does: the route is still good, only that field is not.
	 */
	if (len != 5) { valid = 0; break; }
	ip = ((int32) data[0] << 24) | ((int32) data[1] << 16) |
	     ((int32) data[2] << 8) | data[3];
	ipbits = data[4];
	if (ipbits < 1 || ipbits > 32) { ip = 0; ipbits = 0; }
	haveip = 1;
	break;
      default:
	/* Unknown, and it is kept EXACTLY as it came in - its own length byte
	 * back in front - so that it can go out again unchanged.  A field
	 * nobody in the middle understands still reaches the far end, which is
	 * the whole extension mechanism the protocol has and the way IPv6 will
	 * travel.  Beyond what we can hold, the rest of them are dropped and
	 * the route itself is still good: an option is an addition to an
	 * entry, never the entry.
	 */
	if (optlen + len + 2 <= (int) sizeof(opts)) {
	  opts[optlen++] = (uint8) (len + 2);
	  opts[optlen++] = (uint8) type;
	  memcpy(opts + optlen, data, (size_t) len);
	  optlen += len;
	}
	break;
      }
      if (!valid) break;
    }
    if (!valid) break;                  /* the frame is not to be trusted */

    if (addreq(call, mynode->call)) continue;   /* he is telling us about us */

    /* Zero and the horizon are both "withdrawn".  A way that is gone is
     * removed rather than stored as nought - an entry with no time is not a
     * way, and keeping one would only make every reader test for it.  A
     * withdrawal for a node we never had creates NOTHING: the table is
     * bounded by nr_maxdest, and "he is not reachable" is no reason to spend
     * an entry on him.
     */
    if (!time || time >= INP3_HORIZON) {
      if ((pd = nodeptr(call, 0))) inp3_route_clear(pd, pp);
      continue;
    }

    /* What he says about HIMSELF is not a route time, it is a formality: the
     * way to him is the link, and we have measured that ourselves.  TNN
     * substitutes 1 and 1 here rather than believing him, and it is right to
     * - a partner who names a large time for himself would otherwise be
     * counted twice, once as his figure and once as our SNTT.
     */
    if (addreq(call, pn->call)) {
      time = 1;
      hops = 1;
    }

    if (!(pd = nodeptr(call, 1))) continue;     /* destination list full */
    if (!(rp = inp3_route(pd, pp, 1))) continue;
    rp->time = time;
    rp->hops = hops;
    rp->stamp = secclock();

    if (*alias && *alias != ' ') memcpy(pd->ident, alias, IDENTLEN);
    /* SET OR CLEARED, never merely set.  There is no separate withdrawal for
     * an address: a node takes one back by announcing itself without the
     * field, and TNN reads it that way - it drops the old entry first and
     * only puts one back if the new packet named one.  Taking it only when
     * present would leave an address standing for as long as the node exists,
     * and we would go on passing it to others after its owner had stopped.
     *
     * The address belongs to the NODE, so the partner who spoke last decides.
     * That is TNN's arrangement too, and it holds because every node re-emits
     * the field as it passes a route on: partners who report the same
     * destination report the same address with it.
     */
    pd->inp3_ip = haveip ? ip : 0;
    pd->inp3_ipbits = haveip ? ipbits : 0;
    /* Replaced rather than merged, and for the same reason: these belong to
     * the entry he just sent, and an option he has stopped sending is one
     * that no longer applies.
     */
    free(pd->inp3_opts);
    pd->inp3_opts = NULL;
    pd->inp3_optlen = 0;
    if (optlen && (pd->inp3_opts = (uint8 *) malloc((size_t) optlen))) {
      memcpy(pd->inp3_opts, opts, (size_t) optlen);
      pd->inp3_optlen = optlen;
    }
  }

discard:
  free_p(bpp);
}

/*---------------------------------------------------------------------------*/

/* WHAT WE WOULD TELL THIS PARTNER about this destination: our route time to
 * it in 10 ms units, and 0 for "nothing to say".  He adds his own measured
 * time to us on top - that is the specification's TT = RT + SNTT, seen from
 * the other end - so what belongs here is our side of the sum alone.
 */

static int inp3_report_time(const struct node *pd, const struct nrpeer *pp,
			    const uint8 *tocall, int *hops)
{
  int ceiling = pp->maxtime ? pp->maxtime : NRRTT_MAX;
  int t;
  struct nrinp3 *rp;

  /* Ourselves, and the smallest value the protocol can say.  Anything larger
   * would be added to his measurement of the link, which is the honest half
   * of the figure and already covers the whole distance between us.
   */
  if (pd == mynode) {
    *hops = 1;
    return 1;
  }

  /* "advert no" leaves a node out here exactly as it leaves him out of the
   * broadcasts.  One filter, one meaning: INP3 must not become the back door
   * through which a node we were told to hide is announced after all.
   */
  if (!node_advert(pd)) return 0;
  /* AND THAT IS THE ONLY RULE.  An alias beginning with "#" is NOT a second
   * one: it means "do not show this node to users", not "do not pass it on",
   * and WAMPES uses it that way itself - net.rc.dl1sbl-1 gives its own node
   * the identifier "#BOEB1".  Suppressing such a node here would make it
   * unreachable through us with nothing written down anywhere to explain it,
   * which is the worse of the two mistakes: an announcement that should not
   * have gone out is visible and one filter line fixes it.
   */
  /* Never his own entry back to him.  He knows where he is, and this is the
   * first and simplest case of the poison reverse below.
   */
  if (tocall && addreq(pd->call, tocall)) return 0;

  /* The way we would TAKE, parameter 28 and all - not merely the best INP3
   * entry.  Announcing one way and forwarding down another is exactly what
   * inp3_preferred() exists to prevent.
   */
  if ((rp = inp3_preferred(pd))) {
    /* POISON REVERSE.  The way we would use runs through him, so seen from
     * where he stands we are not a way at all - and saying otherwise is
     * precisely how two nodes end up pointing at each other.
     */
    if (rp->peer == pp) return 0;
    t = rp->time + rp->peer->srtt;
    *hops = rp->hops;
  } else if (pd->neighbor && (int) pd->quality > 0) {
    /* Known only from the graph, so it has to be converted: he speaks time
     * and the graph holds quality.  This is TNN's qual2rtt, and it sits HERE,
     * at the edge, for the reason TNN puts every one of its conversions at
     * the edge - in the middle it would cost the measured value we are
     * supposed to be passing on.
     *
     * THE FIGURE IS COARSE AND IT IS WORTH KNOWING WHY: nr_hfqual is a
     * constant that every direct neighbour is given, so the graph's quality
     * is an assumption and not a measurement, and converting it does not make
     * it one.  It also fits in a narrow band - quality 3 is 25.3 s and there
     * is nothing slower to be had.  The alternative was to announce nothing
     * we ever learned by broadcast, which would make us a node with exactly
     * one reachable destination, and that is worse than a coarse number.
     */
    if (tocall && addreq(pd->neighbor->call, tocall)) return 0;
    t = (256 - (int) pd->quality) * 10;
    *hops = pd->hopcnt;
  } else
    return 0;

  /* The penalty of the specification: one granularity step for the hop
   * through us, so that a chain of fast links still grows monotonically
   * instead of sticking at the floor.  The hop COUNT is raised by the
   * receiver, not here - raising it at both ends would count us twice.
   */
  t++;
  if (*hops < 1) *hops = 1;
  if (*hops > 254) *hops = 254;
  if (t > ceiling) return 0;    /* past his "$M", or past what we can say */
  return t;
}

/*---------------------------------------------------------------------------*/

/* WHAT TO PUT ON THE WIRE for this destination, or -1 for "say nothing".
 *
 * INP3 announces CHANGES, and the whole art of it is in not announcing them
 * too eagerly: a route that flaps costs every node behind us a recalculation,
 * and the ones behind them another.  The thresholds are TNN's, read off
 * inform_peer() (l3netrom.c:706-866).
 *
 * NOTE THAT THIS IS NOT ALWAYS THE VALUE WE HOLD, and that is the part I had
 * built backwards before the sources were to hand again.  A worsening goes
 * out AT ONCE - negative information has priority and the specification
 * gives it a guaranteed maximum delay - but it goes out PESSIMISTICALLY,
 * inflated by an eighth of itself plus half the link time.  The damping then
 * costs nothing further: because the inflated figure is what we remember
 * having said, every further small worsening up to it is no longer a
 * worsening at all and needs no frame.
 *
 * What I had instead was a band the worsening had to clear before being sent
 * truthfully - which damps just as well and withholds bad news to do it.
 * That is the one thing this protocol is emphatic about not doing.
 */

static int inp3_to_say(const struct nrpeer *pp, int now, int old,
		       int sweep, int congested)
{
  int ceiling = pp->maxtime ? pp->maxtime : NRRTT_MAX;
  int diff;

  if (!now) return old ? 0 : -1;        /* withdrawal, and only if he has it */
  if (sweep) return now;
  /* The first word about it, and only well inside his ceiling: mentioned at
   * the very edge, the next slight worsening would have to withdraw it again.
   */
  if (!old) return now * 2 <= ceiling ? now : -1;
  if (now == old) return -1;

  if (now < old) {
    /* Good news, and it has to be substantial by four separate measures at
     * once.  The last is the plainest: a link with a queue on it has other
     * things to do, and this can wait for a tick when it is cheap.
     */
    diff = old - now;
    if (diff < old / 2) return -1;              /* at least half again */
    if (diff < 10) return -1;                   /* at least 100 ms */
    if (diff < pp->srtt / 2) return -1;         /* worth more than the link */
    if (congested) return -1;
    return now;
  }

  now += now / 8 + pp->srtt / 2;
  /* Inflated past what he asked for, so it is not a route as far as he is
   * concerned.  Withdrawn rather than sent as something he would only throw
   * away.
   */
  if (now > ceiling) return 0;
  return now;
}

/*---------------------------------------------------------------------------*/

/* One routing information packet.  Returns the bytes it took, or 0 if it did
 * not fit - the caller then sends what it has and asks again.
 *
 * A WITHDRAWAL GOES OUT AS THE HORIZON AND NEVER AS A LITERAL ZERO.  Both
 * mean "gone" on receipt, but TNN turns a zero into 60000 as it writes, and
 * being identical to it costs nothing.  It carries no options either: they
 * describe an entry, and there is no longer an entry to describe.
 */

static int inp3_put_rip(uint8 *p, int room, const struct node *pd,
			int time, int hops)
{
  int gone = time >= INP3_HORIZON;
  int havealias = !gone && pd->ident[0] && pd->ident[0] != ' ';
  int need = AXALEN + 3 + 1;            /* call, hops, time, end of packet */
  uint8 *start = p;

  if (havealias) need += 2 + IDENTLEN;
  if (!gone && pd->inp3_ip) need += 7;
  if (!gone) need += pd->inp3_optlen;
  if (need > room) return 0;

  addrcp(p, pd->call);
  p += AXALEN;
  *p++ = (uint8) hops;
  *p++ = (uint8) (time >> 8);
  *p++ = (uint8) time;
  if (havealias) {
    *p++ = 2 + IDENTLEN;
    *p++ = INP3_ALIAS;
    memcpy(p, pd->ident, IDENTLEN);
    p += IDENTLEN;
  }
  if (!gone && pd->inp3_ip) {
    /* AND THE SEVEN IS THE WHOLE POINT: the length byte counts itself and the
     * type byte.  The specification calls this field five bytes, meaning its
     * payload, and five on the wire is what nobody can read.
     */
    *p++ = 7;
    *p++ = INP3_IPA;
    *p++ = (uint8) (pd->inp3_ip >> 24);
    *p++ = (uint8) (pd->inp3_ip >> 16);
    *p++ = (uint8) (pd->inp3_ip >> 8);
    *p++ = (uint8) pd->inp3_ip;
    *p++ = (uint8) pd->inp3_ipbits;
  }
  /* Verbatim, including their own length bytes - see struct node.  We are the
   * node in the middle here, and passing on what we cannot read is the only
   * thing that makes a new field type possible without asking anybody.
   */
  if (!gone && pd->inp3_optlen) {
    memcpy(p, pd->inp3_opts, (size_t) pd->inp3_optlen);
    p += pd->inp3_optlen;
  }
  *p++ = INP3_EOP;
  return (int) (p - start);
}

/*---------------------------------------------------------------------------*/

/* Everything that has changed for this partner since the last tick, in as few
 * frames as it takes.  This is the sending side, and it is the counterpart of
 * send_rout() in flexnet.c down to the shape of the loop.
 */

static void inp3_inform_peer(struct nrpeer *pp, struct ax25_cb *axp)
{
  int congested;
  int mtu;
  int sweep = 0;
  struct mbuf *bp = NULL;
  struct node *pd;
  uint8 *tocall;

  if (!mynode || !axp || axp->state != LAPB_CONNECTED) return;
  /* Nothing agreed, or the measurement is not yet complete at BOTH ends -
   * which is TNN's condition and not an obvious one.  Ours is needed because
   * every route time we send is built on it.  HIS is needed because he adds
   * his own to each of them, and until he has one every route we name arrives
   * at him short by the length of the link - which makes us look better than
   * we are, exactly at the moment we know least.
   */
  if (!pp->inp3 || !pp->srtt || !pp->hissrtt) return;

  mtu = axp->iface ? axp->iface->mtu : 256;
  if (mtu > 256) mtu = 256;
  if (mtu < 64) return;
  congested = len_q(axp->txq) > 7;
  tocall = nrpeer_target(pp);

  if (++pp->sweepcount >= NRSWEEP_EVERY) {
    pp->sweepcount = 0;
    sweep = 1;
  }

  for (pd = nodes; pd; pd = pd->next) {
    int hops = 1;
    int n = 0;
    int now = inp3_report_time(pd, pp, tocall, &hops);
    struct nrinp3 *rp = inp3_route(pd, pp, 0);
    int say = inp3_to_say(pp, now, rp ? rp->reported : 0, sweep, congested);

    if (say < 0) continue;

    for (;;) {
      if (!bp) {
	if (!(bp = alloc_mbuf(mtu + 1))) return;
	bp->data[0] = PID_NETROM;
	bp->data[1] = INP3_RIF;
	bp->cnt = 2;
      }
      if ((n = inp3_put_rip(bp->data + bp->cnt, mtu + 1 - (int) bp->cnt,
			    pd, say ? say : INP3_HORIZON, hops)) > 0) break;
      if (bp->cnt <= 2) break;          /* longer than a whole frame: leave it */
      send_ax25(axp, &bp, -1);
      bp = NULL;
    }
    if (n <= 0) continue;
    bp->cnt += n;

    /* Remembered only once it is in a frame, and it is what we SAID that is
     * remembered, not what we hold - a worsening was inflated on the way out,
     * and remembering the true figure would make us say it again next tick.
     * A cell may have to be made for it: a destination he never mentioned has
     * none, and from now on that is exactly what the cell is for.
     */
    if (!rp && say) rp = inp3_route(pd, pp, 1);
    if (rp) {
      rp->reported = say;
      if (!rp->time && !rp->reported) inp3_route_drop(pd, pp);
    }
  }
  if (bp) send_ax25(axp, &bp, -1);
}

/*---------------------------------------------------------------------------*/

static void broadcast_recv(struct mbuf **bpp, struct node *pn)
{

  char ident[IDENTLEN];
  enum rf_in in;
  int quality;
  struct linkinfo *pi;
  struct node *pb, *pd;
  uint8 buf[NRRTDESTLEN];

  routes_stat.rcvd++;
  if (pn == mynode) goto discard;
  if (PULLCHAR(bpp) != 0xff) goto discard;
  if (pullup(bpp, ident, IDENTLEN) != IDENTLEN) goto discard;
  in = node_in(pn);
  /* "in none" is the one place where NET/ROM cannot do what FlexNet does.
   * This link is not only how we learn about him, it is how we REACH him -
   * calculate_all() routes by it - so refusing it means refusing to speak
   * NET/ROM with him at all.  That is a legitimate thing to ask for, but it
   * is not the same as the FlexNet "in none", and doc/ROUTE-FILTER.md says
   * so.
   */
  if (in == RF_IN_NONE) goto discard;
  if (*ident > ' ') memcpy(pn->ident, ident, IDENTLEN);
  update_link(mynode, pn, 1, nr_hfqual);
  /* Under "only-him" he is a neighbour and nothing more: we reach him, and
   * the rest of his broadcast is read to the end and dropped.
   */
  if (in != RF_IN_ALL) {
    calculate_all();
    goto discard;
  }
  while (pullup(bpp, buf, NRRTDESTLEN) == NRRTDESTLEN) {
    if (!*buf) break;
    if (addreq(buf, mynode->call)) continue;
    if (!(pd = nodeptr(buf, 1))) continue;      /* destination list full */
    if (buf[AXALEN] > ' ') memcpy(pd->ident, buf + AXALEN, IDENTLEN);
    if (!(pb = nodeptr(buf + AXALEN + IDENTLEN, 1))) continue;
    quality = buf[AXALEN+IDENTLEN+AXALEN];
    if (pb == mynode) {
      if (quality >= pd->quality) pd->force_broadcast = 1;
      continue;
    }
    if (pn == pb || pb == pd)
      update_link(pn, pd, 2, quality);
    else {
      pi = linkinfoptr(pn, pb);
      if (pi->time != PERMANENT) pi->time = secclock();
      if (pi->quality) {
	int q = quality * 256 / pi->quality;
	while (q * pi->quality / 256 < quality) q++;
	quality = q;
      }
      update_link(pb, pd, 3, quality);
    }
  }
  calculate_all();

discard:
  free_p(bpp);
}

/*---------------------------------------------------------------------------*/

/* Is there a port left that wants the whole table?  Asked before the packet is
 * built, so that a node with every entry switched off - or every entry down to
 * "feed no" - does not build one, and does not count sends it never made in
 * "netrom status".
 */

static int broadcasts_active(void)
{
  struct broadcast *p;

  for (p = broadcasts; p; p = p->next)
    if (!p->disabled && broadcast_feeds(p)) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static struct mbuf *alloc_broadcast_packet(void)
{
  struct mbuf *bp;

  if ((bp = alloc_mbuf(258))) {
    bp->data[0] = UI;
    bp->data[1] = PID_NETROM;
    bp->data[2] = 0xff;
    memcpy(bp->data + 3, mynode->ident, IDENTLEN);
    bp->cnt = 3 + IDENTLEN;
  }
  return bp;
}

/*---------------------------------------------------------------------------*/

/* THE OTHER OUTPUT EDGE, and the exact counterpart of the conversion in
 * inp3_report_time(): a way we hold over INP3 is a measured TIME, and a
 * nodes broadcast speaks QUALITY.
 *
 * TNN never has this question, because it normalises everything to time as
 * it comes IN and keeps one table.  We keep both metrics, so we convert as a
 * frame is WRITTEN - which means there are two output edges, and this is the
 * second of them.
 *
 * Without it a destination known only through INP3 keeps hopcnt INFINITY and
 * quality 0 - the values calculate_hopcnts() and calculate_qualities() leave
 * behind for a node with no edges, and a RIP names no intermediate node to
 * build an edge from.  It would never appear in a broadcast at all, and a
 * neighbour who speaks only NET/ROM could not reach it through us.  That is
 * an INP3 island, and it was never a decision - it is what falls out of the
 * data structure if nobody looks.
 *
 * Clamped 3..254 as TNN clamps it: 0 is "unreachable" and would withdraw the
 * entry, and the top of the range belongs to a node's view of itself.
 */

static int inp3_quality(const struct nrinp3 *rp)
{
  int q = 255 - (rp->time + rp->peer->srtt) / 10;

  if (q < 3) q = 3;
  if (q > 254) q = 254;
  return q;
}

/*---------------------------------------------------------------------------*/

static void send_broadcast(void *arg)
{

  uint8 *p;
  int hopcnt, nexthopcnt;
  struct mbuf *bp;
  struct node *pn;

  set_timer(&broadcast_timer, nr_bdcstint * 1000L);
  start_timer(&broadcast_timer);
  calculate_all();

  /* Ports that announce nothing but our own existence get one bare packet -
   * the identifier and no entries.  The neighbour still learns that we are
   * here, because broadcast_recv() makes a neighbour of whoever sent the
   * frame, and that is the whole difference from switching the entry off:
   * a neighbour who never hears from us cannot route to us either.
   *
   * It is sent here rather than inside send_broadcast_packet(), which is
   * called once per full packet - a port that wants nothing would otherwise
   * get one empty frame per packetful of entries it is not being sent.
   */
  {
    struct broadcast *p;
    struct mbuf *bare = 0;

    for (p = broadcasts; p; p = p->next) {
      if (p->disabled || broadcast_feeds(p)) continue;
      if (!bare && !(bare = alloc_broadcast_packet())) break;
      broadcast_to(p, &bare);
      routes_stat.sent++;
    }
    free_p(&bare);
  }

  if (!broadcasts_active()) return;
  bp = alloc_broadcast_packet();
  for (hopcnt = 1; hopcnt <= INFINITY; hopcnt = nexthopcnt) {
    nexthopcnt = INFINITY + 1;
    for (pn = nodes; pn; pn = pn->next) {
      /* The way we would TAKE, which for an INP3 destination is not in the
       * graph at all - see inp3_quality() above.
       */
      struct nrinp3 *rp = inp3_preferred(pn);
      int nodehops = rp ? (rp->hops < INFINITY ? rp->hops : INFINITY)
			: pn->hopcnt;
      int nodequal = rp ? inp3_quality(rp) : (int) pn->quality;

      if (nodehops >= hopcnt && (nodequal || pn->force_broadcast)) {
	if (nodehops == hopcnt) {
	  pn->force_broadcast = 0;
	  /* "advert no": he is left out.  Only he - what lies behind him
	   * came in through "in", and once accepted it is our route like any
	   * other.
	   */
	  if (pn != mynode && !node_advert(pn)) continue;
	  if (!bp) bp = alloc_broadcast_packet();
	  p = bp->data + bp->cnt;
	  addrcp(p, pn->call);
	  p += AXALEN;
	  memcpy(p, pn->ident, IDENTLEN);
	  p += IDENTLEN;
	  /* The best neighbour, and it is a back door: broadcast_recv() at
	   * the far end creates a node for whatever stands in this field
	   * (nodeptr(buf + AXALEN + IDENTLEN, 1)), so a hidden neighbour
	   * would become known through the entries that reach past him.  We
	   * put ourselves there instead - to the outside we are then the last
	   * hop, which is exactly "appear as one system".
	   *
	   * An INP3 way names US for the same reason and one more: the
	   * partner it runs through speaks a protocol this neighbour does
	   * not, so his callsign would be an offer nobody here can take up.
	   * As far as this broadcast goes we ARE the last hop, and that is
	   * not a polite fiction but the truth.
	   */
	  if (rp)
	    addrcp(p, mynode->call);
	  else
	    addrcp(p, pn->neighbor ? (node_advert(pn->neighbor) ?
				      pn->neighbor->call : mynode->call)
				   : pn->call);
	  p += AXALEN;
	  *p++ = (char) nodequal;
	  if ((bp->cnt = p - bp->data) > 258 - NRRTDESTLEN) {
	    send_broadcast_packet(&bp);
	    routes_stat.sent++;
	    bp = NULL;
	  }
	} else if (nodehops < nexthopcnt) {
	  nexthopcnt = nodehops;
	}
      }
    }
  }
  if (bp) {
    send_broadcast_packet(&bp);
    routes_stat.sent++;
  }
}

/*---------------------------------------------------------------------------*/

/* Is this an IP datagram carried over NET/ROM, and if so, take it.
 *
 * Pulled out of route_packet() because there are now two places that accept
 * one: the ordinary "addressed to us", and a datagram merely passing through
 * from a node we hide, which we terminate here rather than forward - see
 * nr_proxy_ip() below.  The two must accept identically, and the surest way
 * to keep them identical is for there to be one of them.
 *
 * The protocol id sits where an L4 header would: opcode 0 is NR4OPPID,
 * "protocol id extension to the network layer", and the two bytes in front of
 * it name the protocol.  So this is not a misuse of the transport header, it
 * is the provision made for exactly this.
 */

static int nr_ip_deliver(struct mbuf **bpp)
{
  int32 ipaddr;
  struct arp_tab *ap;
  uint8 hwaddr[AXALEN];

  if ((*bpp)->cnt < 40                  ||
      (*bpp)->data[19] != 0             ||
      (*bpp)->data[15] != NRPROTO_IP    ||
      (*bpp)->data[16] != NRPROTO_IP    ||
      !(ipaddr = get32((*bpp)->data + 32)) ||
      !Nr_iface)
    return 0;

  Nr_iface->rawrecvcnt++;
  Nr_iface->lastrecv = secclock();
  /* Which node this address lives behind, so that the answer finds its way.
   * In the passing-through case this is the only chance to learn it: until
   * now the pairing was only noted for datagrams addressed to us.
   */
  if ((ap = arp_lookup(ARP_NETROM, ipaddr)) == NULL ||
      ap->state != ARP_VALID ||
      run_timer(&ap->timer)) {
    addrcp(hwaddr, (*bpp)->data);
    arp_add(ipaddr, ARP_NETROM, hwaddr, 0);
  }
  pullup(bpp, NULL, 20);
  dump(Nr_iface, IF_TRACE_IN, *bpp);
  ip_route(Nr_iface, bpp, 0);
  return 1;
}

/*---------------------------------------------------------------------------*/

static void route_packet(struct mbuf **bpp, struct node *fromneighbor)
{

  int ttl;
  struct node *pn;
  struct node *source = NULL;   /* the node the datagram started at */

  if (!bpp || !*bpp || (*bpp)->cnt < 15) goto discard;

  if (fromneighbor != mynode) {
    enum rf_in in = node_in(fromneighbor);

    if (in == RF_IN_NONE) goto discard;
    if (update_link(mynode, fromneighbor, 1, nr_hfqual)) calculate_all();
    if (!(pn = nodeptr((*bpp)->data, 1))) goto discard;
    if (pn == mynode) {
      /* Normally a routing error: a datagram cannot arrive at the node it
       * started from.  EXACTLY ONE does, legitimately - our own L3RTT probe,
       * reflected unchanged by the neighbour, which is the whole point of
       * the frame.  It has to be caught here, ahead of the discard, because
       * the L3RTT branch further down is never reached for it.
       */
      if (addreq((*bpp)->data + AXALEN, L3RTT) &&
	  (*bpp)->cnt >= AXALEN * 2 + 12 &&
	  ((*bpp)->data[AXALEN*2+5] & NR4OPCODE) == NR4OPINFO &&
	  !memcmp("L3RTT:", (*bpp)->data + AXALEN * 2 + 6, 6))
	nrpeer_recv_rtt(*bpp, fromneighbor);
      goto discard;
    }
    source = pn;                     /* pn is reused below for the target */
    /* A frame passing through builds the way back, and that is the second
     * way a station teaches us about others - so "in" governs it too.  Note
     * what this costs under "only-him": a connection routed through him has
     * no return path and dies.  On a user port that is the point; on a link
     * that carries transit it is not, and doc/ROUTE-FILTER.md warns about it.
     */
    if (in == RF_IN_ALL && !pn->neighbor) {
      struct linkinfo *pi = linkinfoptr(mynode, fromneighbor);
      if (pi->quality) {
	int q = 1;
	while (q * pi->quality / 256 < 1) q++;
	if (update_link(fromneighbor, pn, 2, q)) calculate_all();
      }
    }
  }

  if (addreq((*bpp)->data + AXALEN, mynode->call)) {
    if (nr_ip_deliver(bpp)) return;
    pullup(bpp, NULL, 15);
    if (!*bpp) return;
    if (fromneighbor == mynode) {
      struct mbuf *hbp = copy_p(*bpp, len_p(*bpp));
      free_p(bpp);
      *bpp = hbp;
    }
    circuit_manager(bpp, NULL);
    return;
  }

  /* Passing through from a node we hide: terminate it here instead.
   *
   * A datagram forwarded at L3 keeps its source callsign all the way - only
   * the TTL below is touched - so every node it crosses learns the sender.
   * For a node we have been told not to announce, that is the leak the whole
   * filter was for: hidden in our broadcasts, and known to everybody the
   * moment traffic flows.
   *
   * Taking the datagram here ends that.  ip_route() sends it onward as ours,
   * and nr_send() puts mynode->call in the source, so what leaves us carries
   * our callsign and nothing of his.  There is nothing to rewrite and so
   * nothing to forget - the frame that goes out is one we built.
   *
   * Only the sender is hidden this way, never what lies behind him: a
   * datagram from a node further out has its own source and is forwarded as
   * before.  That is the same rule "advert no" follows in the broadcasts.
   */
  if (source != NULL && !node_advert(source)) {
    if (nr_ip_deliver(bpp)) return;
    /* An L4 session is terminated here instead - see nr_proxy_l4().  Note
     * what this means for a hidden node with more than one uplink: once we
     * proxy, his session exists only as our pair of circuits, so the same
     * session sent through a different neighbour arrives at the far end with
     * numbers it has no circuit for and stops.  "advert no" on a node with
     * several interlinks is a configuration error, and this is why.
     */
    if (nr_proxy_l4(bpp)) return;
  }

  ttl = (*bpp)->data[2*AXALEN];
  if (--ttl <= 0) goto discard;
  (*bpp)->data[2*AXALEN] = ttl;

  if (addreq((*bpp)->data + AXALEN, L3RTT)) {
    /* The opcode is at 19 and "L3RTT:" runs to 25, but the only length this
     * function has established is 15 - a short frame addressed to L3RTT read
     * up to eleven bytes past the end of the mbuf.
     */
    if ((*bpp)->cnt < AXALEN * 2 + 12) goto discard;
    if (((*bpp)->data[AXALEN*2+5] & NR4OPCODE) != NR4OPINFO) goto discard;
    if (memcmp("L3RTT:", (*bpp)->data + AXALEN * 2 + 6, 6)) goto discard;
    /* Only a neighbour's frame reaches this: one of ours coming back has our
     * own callsign as its L3 source and was taken further up, where the
     * routing error check would otherwise have thrown it away.
     *
     * Read what he says about himself on the way past, then reflect it
     * UNCHANGED, which is what the far end times.  We have always reflected;
     * the reading is the new part, and it is where "$N" arrives.
     */
    nrpeer_read_flags(*bpp, fromneighbor);
    send_packet_to_neighbor(bpp, fromneighbor);
    return;
  }

  if (!(pn = nodeptr((*bpp)->data + AXALEN, 1))) goto discard;

  /* THE INP3 TABLE IS ASKED FIRST, and that is the whole of the change: one
   * question ahead of the old one, and the graph untouched behind it.  It is
   * also what makes the two metrics able to stand side by side without being
   * converted into one another - whoever is asked first and answers, decides.
   *
   * Turned round by parameter 28, INP3 answers only where the graph has
   * nothing.  There is no third case and in particular NO FALL BACK ON OUR
   * AX.25 ROUTES: a destination with no way is discarded, as it always was.
   * Not to be confused with send_packet_to_neighbor(), which certainly does
   * use them - to reach the NEIGHBOUR.  That is delivery at layer 2 and not
   * routing.
   */
  {
    struct node *via = inp3_nexthop(pn, fromneighbor);

    if (via) {
      send_packet_to_neighbor(bpp, via);
      return;
    }
  }

  if (!pn->neighbor) {
    if (fromneighbor != mynode) {
      pn->force_broadcast = 1;
#ifdef FORCE_BC
      send_broadcast(NULL);
#endif
    }
    goto discard;
  }

#ifdef FORCE_BC
  if (pn->neighbor == fromneighbor ||
      addreq(pn->neighbor->call, (*bpp)->data)) send_broadcast(NULL);
#endif

  send_packet_to_neighbor(bpp, pn->neighbor);
  return;

discard:
  free_p(bpp);
}

/*---------------------------------------------------------------------------*/

static void send_l3_packet(uint8 *source, uint8 *dest, int ttl, struct mbuf **bpp)
{
  pushdown(bpp, NULL, 2 * AXALEN + 1);
  addrcp((*bpp)->data, source);
  addrcp((*bpp)->data + AXALEN, dest);
  if (++ttl > 255) ttl = 255;
  (*bpp)->data[2*AXALEN] = ttl;
  route_packet(bpp, mynode);
}

/*---------------------------------------------------------------------------*/

int nr_send(struct mbuf **bpp, struct iface *iface, int32 gateway, uint8 tos)
{
  struct arp_tab *arp;

  dump(iface, IF_TRACE_OUT, *bpp);
  iface->rawsndcnt++;
  iface->lastsent = secclock();
  if (!(arp = arp_lookup(ARP_NETROM, gateway))) {
    free_p(bpp);
    return -1;
  }
  pushdown(bpp, NULL, 5);
  (*bpp)->data[0] = NRPROTO_IP;
  (*bpp)->data[1] = NRPROTO_IP;
  (*bpp)->data[2] = 0;
  (*bpp)->data[3] = 0;
  (*bpp)->data[4] = 0;
  if (iface->trace & IF_TRACE_RAW)
    raw_dump(iface, -1, *bpp);
  send_l3_packet(mynode->call, arp->hw_addr, nr_ttlinit, bpp);
  return 0;
}

/*---------------------------------------------------------------------------*/

void nr3_input(struct iface *iface, struct ax25_cb *axp, const uint8 *src, struct mbuf **bpp)
{
  struct node *pn;

  if (!(pn = nodeptr(src, 1))) {
    free_p(bpp);                /* destination list full */
    return;
  }
  /* Where he is, for a filter written per port.  A node is a callsign and
   * carries no port of its own; this is the only place that knows.
   */
  pn->iface = iface;
  /* 0xff is BOTH signatures, and the transport is what tells them apart: the
   * classic nodes broadcast is a UI frame, an INP3 routing information frame
   * is numbered on the interlink.  Nothing in the bytes distinguishes them,
   * which is why axp had to be carried down here.
   */
  if (bpp && *bpp && (*bpp)->cnt && *(*bpp)->data == 0xff) {
    struct nrpeer *pp;

    if (axp)
      inp3_recv(bpp, pn);
    else if ((pp = nrpeer_find(pn->call)) != NULL && pp->inp3)
      /* HIS BROADCAST IS DROPPED WHILE HIS INTERLINK IS UP, and that needs
       * no configuring: he tells us the same destinations twice then, once
       * as a measured time over the interlink and once as a guessed quality
       * in the broadcast, and the worse source would overwrite the better
       * one at whatever interval it happens to arrive.
       *
       * TNN does not do this - rx_ui_broadcast() takes a broadcast without
       * looking at the sender's type - and it is not a problem there because
       * an INP link is usually a port of its own.  With several partners on
       * one axudp line it is one, and here TNN is the thing to do better
       * rather than the thing to copy.
       */
      free_p(bpp);
    else
      broadcast_recv(bpp, pn);
  } else
    route_packet(bpp, pn);
}

/*---------------------------------------------------------------------------*/

static void routing_manager_initialize(void)
{
  broadcast_timer.func = send_broadcast;
  set_timer(&broadcast_timer, 10 * 1000L);
  start_timer(&broadcast_timer);
  /* Not started here: with no partners configured there is nothing to do, and
   * net.rc is read after this.  donrpeer() starts it with the first entry.
   */
  nrpeer_timer.func = nrpeer_service;
}

/*---------------------------------------------------------------------------*/
/****************************** Circuit Manager ******************************/
/*---------------------------------------------------------------------------*/

#include "netrom.h"

char *Nr4states[] = {
	"Disconnected",
	"Conn Pending",
	"Connected",
	"Disc Pending",
	"Listening"
} ;

char *Nr4reasons[] = {
	"Normal",
	"By Peer",
	"Timeout",
	"Reset",
	"Refused"
} ;

static int server_enabled;
static struct circuit *circuits;
static int ncircuits;           /* open circuits, see nr_maxcircuits */

/*---------------------------------------------------------------------------*/

char *nr_addr2str(struct circuit *pc)
{

  char *p;
  static char buf[128];

  pax25(p = buf, pc->cuser);
  while (*p) p++;
  *p++ = ' ';
  if (pc->outbound) {
    *p++ = '-';
    *p++ = '>';
  } else
    *p++ = '@';
  *p++ = ' ';
  pax25(p, pc->node);
  return buf;
}

/*---------------------------------------------------------------------------*/

static void reset_t1(struct circuit *pc)
{
  int32 tmp;

  tmp = 4 * pc->mdev + pc->srtt;
  set_timer(&pc->timer_t1, max(tmp, 500));
}

/*---------------------------------------------------------------------------*/

static int nrbusy(struct circuit *pc)
{
  return pc->rcvcnt >= nr_tnoackbuf * NR4MAXINFO;
}

/*---------------------------------------------------------------------------*/

static void send_l4_packet(struct circuit *pc, int opcode, struct mbuf **bpp)
{

  int start_t1_timer = 0;
  struct mbuf *bp;

  if (bpp == NULL) {
    bp = NULL;
    bpp = &bp;
  }

  switch (opcode & NR4OPCODE) {
  case NR4OPCONRQ:
    pushdown(bpp, NULL, 20);
    (*bpp)->data[0] = pc->localindex;
    (*bpp)->data[1] = pc->localid;
    (*bpp)->data[2] = 0;
    (*bpp)->data[3] = 0;
    (*bpp)->data[4] = opcode;
    (*bpp)->data[5] = pc->window;
    addrcp((*bpp)->data + 6, pc->cuser);
    addrcp((*bpp)->data + 13, mynode->call);
    start_t1_timer = 1;
    break;
  case NR4OPCONAK:
    pushdown(bpp, NULL, 6);
    (*bpp)->data[0] = pc->remoteindex;
    (*bpp)->data[1] = pc->remoteid;
    (*bpp)->data[2] = pc->localindex;
    (*bpp)->data[3] = pc->localid;
    (*bpp)->data[4] = opcode;
    (*bpp)->data[5] = pc->window;
    break;
  case NR4OPDISRQ:
    start_t1_timer = 1;
  case NR4OPDISAK:
    pushdown(bpp, NULL, 5);
    (*bpp)->data[0] = pc->remoteindex;
    (*bpp)->data[1] = pc->remoteid;
    (*bpp)->data[2] = 0;
    (*bpp)->data[3] = 0;
    (*bpp)->data[4] = opcode;
    break;
  case NR4OPINFO:
    start_t1_timer = 1;
  case NR4OPACK:
    pushdown(bpp, NULL, 5);
    if (pc->reseq && !pc->naksent) {
      opcode |= NR4NAK;
      pc->naksent = 1;
    }
    if ((pc->chokesent = nrbusy(pc))) opcode |= NR4CHOKE;
    pc->response = 0;
    (*bpp)->data[0] = pc->remoteindex;
    (*bpp)->data[1] = pc->remoteid;
    (*bpp)->data[2] = pc->send_state;
    (*bpp)->data[3] = pc->recv_state;
    (*bpp)->data[4] = opcode;
    if ((opcode & NR4OPCODE) == NR4OPINFO)
      pc->send_state = (pc->send_state + 1) & 0xff;
    break;
  default:
    free_p(bpp);
    return;
  }
  if (start_t1_timer) start_timer(&pc->timer_t1);
  /* Normally we sign with our own call.  On the near half of a proxied
   * session we sign as the node the caller asked for - he believes he is
   * talking to it, and an answer from anyone else is one he has no circuit
   * for.  See nr_proxy_l4().
   */
  send_l3_packet(pc->proxyas[0] ? pc->proxyas : mynode->call,
		 pc->node, nr_ttlinit, bpp);
}

/*---------------------------------------------------------------------------*/

static void try_send(struct circuit *pc, int fill_sndq)
{

  int cnt;
  struct mbuf *bp;
  struct mbuf *bp1;

  while (pc->unack < pc->cwind) {
    if (pc->state != NR4STCON || pc->remote_busy) return;
    if (fill_sndq && pc->t_upcall) {
      cnt = space_nr(pc);
      if (cnt > 0) {
	(*pc->t_upcall)(pc, cnt);
	if (pc->unack >= pc->cwind) return;
      }
    }
    if (!pc->sndq) return;
    cnt = len_p(pc->sndq);
    if (cnt < NR4MAXINFO && pc->unack) return;
    if (cnt > NR4MAXINFO) cnt = NR4MAXINFO;
    if (!(bp = alloc_mbuf(cnt))) return;
    pullup(&pc->sndq, bp->data, bp->cnt = cnt);
    dup_p(&bp1, bp, 0, cnt);
    enqueue(&pc->resndq, &bp);
    pc->unack++;
    pc->sndtime[pc->send_state] = msclock();
    send_l4_packet(pc, NR4OPINFO, &bp1);
  }
}

/*---------------------------------------------------------------------------*/

static void set_circuit_state(struct circuit *pc, enum netrom_state newstate)
{
  enum netrom_state oldstate;

  oldstate = pc->state;
  pc->state = newstate;
  pc->retry = 0;
  stop_timer(&pc->timer_t1);
  stop_timer(&pc->timer_t4);
  reset_t1(pc);
  switch (newstate) {
  case NR4STDISC:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    break;
  case NR4STCPEND:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    send_l4_packet(pc, NR4OPCONRQ, NULL);
    break;
  case NR4STCON:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    try_send(pc, 1);
    break;
  case NR4STDPEND:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    send_l4_packet(pc, NR4OPDISRQ, NULL);
    break;
  case NR4STLISTEN:
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t1_timeout(void *arg)
{
  struct circuit *pc = (struct circuit *) arg;
  struct mbuf *bp, *qp;

  set_timer(&pc->timer_t1, (dur_timer(&pc->timer_t1) * 5 + 2) / 4);
  if (pc->cwind > 1) pc->cwind--;
  if (++pc->retry > nr_tretry) pc->reason = NR4RTIMEOUT;
  switch (pc->state) {
  case NR4STDISC:
    break;
  case NR4STCPEND:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, NR4STDISC);
    else
      send_l4_packet(pc, NR4OPCONRQ, NULL);
    break;
  case NR4STCON:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, NR4STDPEND);
    else if (pc->unack) {
      pc->send_state = (pc->send_state - pc->unack) & 0xff;
      for (qp = pc->resndq; qp; qp = qp->anext) {
	pc->sndtime[pc->send_state] = 0;
	dup_p(&bp, qp, 0, NR4MAXINFO);
	send_l4_packet(pc, NR4OPINFO, &bp);
      }
    }
    break;
  case NR4STDPEND:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, NR4STDISC);
    else
      send_l4_packet(pc, NR4OPDISRQ, NULL);
    break;
  case NR4STLISTEN:
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t3_timeout(void *arg)
{
  struct circuit *pc = (struct circuit *) arg;

  if (!run_timer(&pc->timer_t1)) close_nr(pc);
}

/*---------------------------------------------------------------------------*/

static void l4_t4_timeout(void *arg)
{
  struct circuit *pc = (struct circuit *) arg;

  pc->remote_busy = 0;
  if (pc->unack) start_timer(&pc->timer_t1);
  try_send(pc, 1);
}

/*---------------------------------------------------------------------------*/

static struct circuit *create_circuit(void)
{

  static int nextid;
  struct circuit *pc;

  pc = (struct circuit *) calloc(1, sizeof(struct circuit));
  nextid++;
  pc->localindex = (nextid >> 8) & 0xff;
  pc->localid = nextid & 0xff;
  pc->remoteindex = -1;
  pc->remoteid = -1;
  pc->state = NR4STDISC;
  pc->cwind = 1;
  pc->mdev = (1000L * nr_ttimeout + 2) / 4;
  reset_t1(pc);
  pc->timer_t1.func = l4_t1_timeout;
  pc->timer_t1.arg = pc;
  pc->timer_t3.func = l4_t3_timeout;
  pc->timer_t3.arg = pc;
  pc->timer_t4.func = l4_t4_timeout;
  pc->timer_t4.arg = pc;
  pc->next = circuits;
  ncircuits++;
  return circuits = pc;
}

/*---------------------------------------------------------------------------*/

/* answeras is NULL for a session addressed to us, and the far node's call for
 * one we proxy - see nr_proxy_l4().
 */

static void circuit_manager(struct mbuf **bpp, const uint8 *answeras)
{

  int nakrcvd;
  struct circuit *pc;
  struct mbuf *p;

  if (!bpp || !*bpp || (*bpp)->cnt < 5) goto discard;

  if (((*bpp)->data[4] & NR4OPCODE) == NR4OPCONRQ) {
    if ((*bpp)->cnt < 20) goto discard;
    for (pc = circuits; pc; pc = pc->next)
      if (pc->remoteindex == (*bpp)->data[0] &&
	  pc->remoteid == (*bpp)->data[1] &&
	  addreq(pc->cuser, (*bpp)->data + 6) &&
	  addreq(pc->node, (*bpp)->data + 13)) break;
    if (!pc) {
      pc = create_circuit();
      pc->remoteindex = (*bpp)->data[0];
      pc->remoteid = (*bpp)->data[1];
      addrcp(pc->cuser, (*bpp)->data + 6);
      addrcp(pc->node, (*bpp)->data + 13);
      if (answeras) {
	addrcp(pc->proxyas, answeras);
	pc->r_upcall = proxy_recv_upcall;
	pc->t_upcall = proxy_send_upcall;
	pc->s_upcall = proxy_state_upcall;
      } else {
	pc->r_upcall = nrserv_recv_upcall;
	pc->t_upcall = nrserv_send_upcall;
	pc->s_upcall = nrserv_state_upcall;
      }
    }
  } else
    for (pc = circuits; ; pc = pc->next) {
      if (!pc) goto discard;
      if (pc->localindex == (*bpp)->data[0] &&
	  pc->localid == (*bpp)->data[1]) break;
    }

  set_timer(&pc->timer_t3, nr_timeout * 1000L);
  start_timer(&pc->timer_t3);

  switch ((*bpp)->data[4] & NR4OPCODE) {

  case NR4OPCONRQ:
    switch (pc->state) {
    case NR4STDISC:
      pc->window = (*bpp)->data[5];
      if (pc->window > nr_twindow) pc->window = nr_twindow;
      if (pc->window < 1) pc->window = 1;
      /* Every connect request with a combination of index, id, user and node
       * not seen before used to open a circuit, and nothing ever said no.
       * Refusing is what this branch already does when there is no server,
       * and it is a legitimate answer: the peer is told to go away instead of
       * being left waiting.
       */
      if (pc->proxyas[0]) {
	/* Proxied: do not answer yet.  We build the far half first and accept
	 * only when it stands - which changes nothing for the caller, because
	 * L4 runs end to end and he waits for the far node's answer anyway.
	 * Until then this circuit stays in NR4STDISC: a retransmitted connect
	 * request finds it again and is simply let go, and NR4STCPEND is not
	 * usable here because entering it would send a connect request of our
	 * own back at him.
	 */
	if (!pc->proxypeer) nr_proxy_open_peer(pc);
      } else if (server_enabled &&
	  (nr_maxcircuits <= 0 || ncircuits <= nr_maxcircuits)) {
	send_l4_packet(pc, NR4OPCONAK, NULL);
	set_circuit_state(pc, NR4STCON);
      } else {
	send_l4_packet(pc, NR4OPCONAK | NR4CHOKE, NULL);
	del_nr(pc);
      }
      break;
    case NR4STCON:
      send_l4_packet(pc, NR4OPCONAK, NULL);
      break;
    default:
      goto discard;
    }
    break;

  case NR4OPCONAK:
    if (pc->state != NR4STCPEND) goto discard;
    pc->remoteindex = (*bpp)->data[2];
    pc->remoteid = (*bpp)->data[3];
    if (pc->window > (*bpp)->data[5]) pc->window = (*bpp)->data[5];
    if (pc->window < 1) pc->window = 1;
    if ((*bpp)->data[4] & NR4CHOKE) {
      pc->reason = NR4RREFUSED;
      set_circuit_state(pc, NR4STDISC);
    } else
      set_circuit_state(pc, NR4STCON);
    break;

  case NR4OPDISRQ:
    send_l4_packet(pc, NR4OPDISAK, NULL);
    pc->reason = NR4RREMOTE;
    set_circuit_state(pc, NR4STDISC);
    break;

  case NR4OPDISAK:
    if (pc->state != NR4STDPEND) goto discard;
    set_circuit_state(pc, NR4STDISC);
    break;

  case NR4OPINFO:
  case NR4OPACK:
    if (pc->state != NR4STCON) goto discard;
    stop_timer(&pc->timer_t1);
    if ((*bpp)->data[4] & NR4CHOKE) {
      if (!pc->remote_busy) pc->remote_busy = msclock();
      set_timer(&pc->timer_t4, nr_tbsydelay * 1000L);
      start_timer(&pc->timer_t4);
    } else {
      pc->remote_busy = 0;
      stop_timer(&pc->timer_t4);
    }
    if (((pc->send_state - (*bpp)->data[3]) & 0xff) < pc->unack) {
      pc->retry = 0;
      if (pc->sndtime[((*bpp)->data[3]-1) & 0xff]) {
	int32 rtt = TDIFF(msclock(), pc->sndtime[((*bpp)->data[3]-1) & 0xff]);
	int32 abserr = (rtt > pc->srtt) ? rtt - pc->srtt : pc->srtt - rtt;
	pc->srtt = ((NRAGAIN - 1) * pc->srtt + rtt + (NRAGAIN / 2)) / NRAGAIN;
	pc->mdev = ((NRDGAIN - 1) * pc->mdev + abserr + (NRDGAIN / 2)) / NRDGAIN;
	reset_t1(pc);
	if (pc->cwind < pc->window) pc->cwind++;
      }
      while (((pc->send_state - (*bpp)->data[3]) & 0xff) < pc->unack) {
	pc->resndq = free_p(&pc->resndq);
	pc->unack--;
      }
    }
    nakrcvd = (*bpp)->data[4] & NR4NAK;
    if (((*bpp)->data[4] & NR4OPCODE) == NR4OPINFO) {
      pc->response = 1;
      if ((((*bpp)->data[2] - pc->recv_state) & 0xff) < pc->window) {
	if (!pc->reseq || ((*bpp)->data[2] - pc->reseq->data[2]) & 0x80) {
	  (*bpp)->anext = pc->reseq;
	  pc->reseq = (*bpp);
	  (*bpp) = NULL;
	} else {
	  for (p = pc->reseq;
	       p->next && (p->data[2] - (*bpp)->data[2] - 1) & 0x80;
	       p = p->anext) ;
	  if (p->data[2] != (*bpp)->data[2]) {
	    (*bpp)->anext = p->anext;
	    p->anext = (*bpp);
	    (*bpp) = NULL;
	  }
	}
	while (pc->reseq && !((pc->reseq->data[2] - pc->recv_state) & 0xff)) {
	  p = pc->reseq;
	  pc->reseq = p->anext;
	  p->anext = NULL;
	  pc->recv_state = (pc->recv_state + 1) & 0xff;
	  pullup(&p, NULL, 5);
	  if (p) {
	    pc->rcvcnt += len_p(p);
	    append(&pc->rcvq, &p);
	  }
	  pc->naksent = 0;
	}
	if (pc->r_upcall && pc->rcvcnt) (*pc->r_upcall)(pc, pc->rcvcnt);
      }
    }
    if (nakrcvd && pc->unack) {
      int old_send_state;
      struct mbuf *bp1;
      old_send_state = pc->send_state;
      pc->send_state = (pc->send_state - pc->unack) & 0xff;
      pc->sndtime[pc->send_state] = 0;
      dup_p(&bp1, pc->resndq, 0, NR4MAXINFO);
      send_l4_packet(pc, NR4OPINFO, &bp1);
      pc->send_state = old_send_state;
    }
    try_send(pc, 1);
    if (pc->response) send_l4_packet(pc, NR4OPACK, NULL);
    if (pc->unack && !pc->remote_busy) start_timer(&pc->timer_t1);
    if (pc->closed && !pc->sndq && !pc->unack)
      set_circuit_state(pc, NR4STDPEND);
    break;
  }

discard:
  free_p(bpp);
}

/*---------------------------------------------------------------------------*/
/********************************* User Calls ********************************/
/*---------------------------------------------------------------------------*/

struct circuit *open_nr(uint8 *node, uint8 *cuser, int window, void (*r_upcall)(struct circuit *p, int cnt), void (*t_upcall)(struct circuit *p, int cnt), void (*s_upcall)(struct circuit *p, enum netrom_state oldstate, enum netrom_state newstate), char *user)
{
  struct circuit *pc;

  if (!nodeptr(node, 0)) {
    Net_error = INVALID;
    return 0;
  }
  if (!cuser || !*cuser) {
    cuser = mynode->call;
  }
  if (!window) {
    window = nr_twindow;
  }
  if (!(pc = create_circuit())) {
    Net_error = NO_MEM;
    return 0;
  }
  pc->outbound = 1;
  addrcp(pc->node, node);
  addrcp(pc->cuser, cuser);
  pc->window = window;
  pc->r_upcall = r_upcall;
  pc->t_upcall = t_upcall;
  pc->s_upcall = s_upcall;
  pc->user = user;
  set_circuit_state(pc, NR4STCPEND);
  return pc;
}

/*---------------------------------------------------------------------------*/

int send_nr(struct circuit *pc, struct mbuf **bpp)
{
  int cnt;

  if (!(pc && bpp && *bpp)) {
    free_p(bpp);
    Net_error = INVALID;
    return -1;
  }
  switch (pc->state) {
  case NR4STDISC:
    free_p(bpp);
    Net_error = NO_CONN;
    return -1;
  case NR4STCPEND:
  case NR4STCON:
    if (!pc->closed) {
      if ((cnt = len_p(*bpp))) {
	append(&pc->sndq, bpp);
	try_send(pc, 0);
      }
      return cnt;
    }
  case NR4STDPEND:
    free_p(bpp);
    Net_error = CON_CLOS;
    return -1;
  case NR4STLISTEN:
    break;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/

int space_nr(struct circuit *pc)
{
  int cnt;

  if (!pc) {
    Net_error = INVALID;
    return -1;
  }
  switch (pc->state) {
  case NR4STDISC:
    Net_error = NO_CONN;
    return -1;
  case NR4STCPEND:
  case NR4STCON:
    if (!pc->closed) {
      cnt = (pc->cwind - pc->unack) * NR4MAXINFO - len_p(pc->sndq);
      return (cnt > 0) ? cnt : 0;
    }
  case NR4STDPEND:
    Net_error = CON_CLOS;
    return -1;
  case NR4STLISTEN:
    break;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/

int recv_nr(struct circuit *pc, struct mbuf **bpp, int cnt)
{
  if (!(pc && bpp)) {
    Net_error = INVALID;
    return -1;
  }
  if (pc->rcvcnt) {
    if (!cnt || pc->rcvcnt <= cnt) {
      *bpp = dequeue(&pc->rcvq);
      cnt = len_p(*bpp);
    } else {
      if (!(*bpp = alloc_mbuf(cnt))) {
	Net_error = NO_MEM;
	return -1;
      }
      pullup(&pc->rcvq, (*bpp)->data, cnt);
      (*bpp)->cnt = cnt;
    }
    pc->rcvcnt -= cnt;
    if (pc->chokesent && !nrbusy(pc)) send_l4_packet(pc, NR4OPACK, NULL);
    return cnt;
  }
  switch (pc->state) {
  case NR4STCPEND:
  case NR4STCON:
    *bpp = NULL;
    Net_error = WOULDBLK;
    return -1;
  case NR4STDISC:
  case NR4STDPEND:
    *bpp = NULL;
    return 0;
  case NR4STLISTEN:
    break;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/

int close_nr(struct circuit *pc)
{
  if (!pc) {
    Net_error = INVALID;
    return -1;
  }
  if (pc->closed) {
    Net_error = CON_CLOS;
    return -1;
  }
  pc->closed = 1;
  switch (pc->state) {
  case NR4STDISC:
    Net_error = NO_CONN;
    return -1;
  case NR4STCPEND:
    set_circuit_state(pc, NR4STDISC);
    return 0;
  case NR4STCON:
    if (!pc->sndq && !pc->unack) set_circuit_state(pc, NR4STDPEND);
    return 0;
  case NR4STDPEND:
    Net_error = CON_CLOS;
    return -1;
  case NR4STLISTEN:
    break;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/

int reset_nr(struct circuit *pc)
{
  if (!pc) {
    Net_error = INVALID;
    return -1;
  }
  pc->reason = NR4RRESET;
  set_circuit_state(pc, NR4STDISC);
  return 0;
}

/*---------------------------------------------------------------------------*/

int del_nr(struct circuit *pc)
{
  struct circuit *p, *q;

  for (q = 0, p = circuits; p != pc; q = p, p = p->next)
    if (!p) {
      Net_error = INVALID;
      return -1;
    }
  if (q)
    q->next = p->next;
  else
    circuits = p->next;
  stop_timer(&pc->timer_t1);
  stop_timer(&pc->timer_t3);
  stop_timer(&pc->timer_t4);
  free_q(&pc->reseq);
  free_q(&pc->rcvq);
  free_q(&pc->sndq);
  free_q(&pc->resndq);
  free(pc);
  ncircuits--;
  return 0;
}

/*---------------------------------------------------------------------------*/

int valid_nr(struct circuit *pc)
{
  struct circuit *p;

  if (!pc) return 0;
  for (p = circuits; p; p = p->next)
    if (p == pc) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Force a retransmission */

int kick_nr(struct circuit *pc)
{
  if (!valid_nr(pc)) return -1;
  l4_t1_timeout(pc);
  return 0;
}

/*---------------------------------------------------------------------------*/
/******************************* Node Proxying *******************************/
/*---------------------------------------------------------------------------*/

/* Terminate an L4 session from a node we hide, and open a second one to where
 * it was going.
 *
 * The problem this solves is the one described in doc/ROUTE-FILTER.md: an L3
 * frame keeps its source callsign the whole way, so every node it crosses
 * learns the sender and announces him in its own broadcast.  "advert no" then
 * hides him from our node list and from nobody else's.
 *
 * The alternative was to rewrite his callsign as the frame passes.  It would
 * mean touching two places - the L3 source, which is what the intermediate
 * nodes learn from, and the node field at offset 13 of the connect request,
 * which is where the far end sends its answers - and keeping a table of
 * (our index, our id) against (his node, his index, his id), because a
 * circuit is named per node: two hidden neighbours would both arrive with
 * index 0.  Terminating needs neither.  Our own connect request fills both
 * fields with mynode->call because WE are its origin, so there is nothing to
 * rewrite and nothing to forget; and the pairing is the two circuits, held by
 * a pointer each, so there is no table and no second expiry to get wrong.
 *
 * What it costs is a second window and a second set of buffers, and having to
 * push back: proxy_pump() moves data only as far as the other side has room,
 * and the rest waits in the receive queue until its send upcall says there is
 * space.  The end-to-end argument does not apply here - WAMPES acknowledges
 * hop by hop anyway - and two shorter windows recover a loss locally instead
 * of across the whole path, which on radio is usually the faster way.
 */

/* Move what the one side has received to the other, as far as it will take
 * it.  What does not fit stays in the receive queue; the other side's send
 * upcall comes back for it.  That is the whole of the back pressure.
 */

static void proxy_pump(struct circuit *from, struct circuit *to)
{
  int room;
  struct mbuf *bp;

  while (from->rcvcnt && (room = space_nr(to)) > 0) {
    bp = NULL;
    if (recv_nr(from, &bp, (room < from->rcvcnt) ? room : from->rcvcnt) <= 0) {
      free_p(&bp);
      return;
    }
    if (send_nr(to, &bp) < 0) return;
  }
}

/*---------------------------------------------------------------------------*/

static void proxy_recv_upcall(struct circuit *pc, int cnt)
{
  if (pc->proxypeer) proxy_pump(pc, pc->proxypeer);
}

/*---------------------------------------------------------------------------*/

static void proxy_send_upcall(struct circuit *pc, int cnt)
{
  if (pc->proxypeer) proxy_pump(pc->proxypeer, pc);
}

/*---------------------------------------------------------------------------*/

static void proxy_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate)
{
  struct circuit *other = pc->proxypeer;

  switch (newstate) {

  case NR4STCON:
    /* The far half stands, so the caller gets his answer now - signed as the
     * node he asked for, which send_l4_packet() does from proxyas.
     */
    if (other && other->proxyas[0] && other->state == NR4STDISC) {
      send_l4_packet(other, NR4OPCONAK, NULL);
      set_circuit_state(other, NR4STCON);
    }
    break;

  case NR4STDISC:
    /* Unlink BOTH before doing anything else.  Whatever we do next may end
     * with the other half calling back in here, and by then this circuit is
     * gone.
     */
    pc->proxypeer = NULL;
    if (other) {
      other->proxypeer = NULL;
      proxy_pump(pc, other);            /* whatever is still in hand */
      if (other->proxyas[0] && other->state == NR4STDISC) {
	/* Never accepted - so the refusal IS the answer he is waiting for,
	 * rather than a silence he has to time out.
	 */
	send_l4_packet(other, NR4OPCONAK | NR4CHOKE, NULL);
	del_nr(other);
      } else {
	close_nr(other);
      }
    }
    del_nr(pc);
    break;

  default:
    break;
  }
}

/*---------------------------------------------------------------------------*/

/* Build the far half.  The user callsign is carried across unchanged, so what
 * the far end sees is "user @ our node" - indistinguishable from an ordinary
 * user connect through us, which is exactly what it should look like.
 */

static void nr_proxy_open_peer(struct circuit *left)
{
  struct circuit *right;

  right = open_nr(left->proxyas, left->cuser, left->window,
		  proxy_recv_upcall, proxy_send_upcall, proxy_state_upcall,
		  NULL);
  if (!right) {
    /* No route to the target.  Say so instead of leaving him waiting - and
     * say it as the target, since that is who he asked.
     */
    send_l4_packet(left, NR4OPCONAK | NR4CHOKE, NULL);
    del_nr(left);
    return;
  }
  left->proxypeer = right;
  right->proxypeer = left;
}

/*---------------------------------------------------------------------------*/

/* An L4 packet passing through from a node we hide: take it here.
 *
 * The place is the transit path, not the accept path.  A user connect through
 * us is addressed at L3 to the TARGET node, so circuit_manager() never sees
 * it and route_packet() would hand it on.  The L4 header sits at the same
 * offsets the IP branch already uses: data[15..19], opcode in data[19].
 *
 * Opcode 0 is NR4OPPID and not a session at all - that is the protocol id
 * extension nr_ip_deliver() handles - so it is left alone here.
 */

static int nr_proxy_l4(struct mbuf **bpp)
{
  uint8 target[AXALEN];

  if ((*bpp)->cnt < 20) return 0;
  if (((*bpp)->data[19] & NR4OPCODE) == NR4OPPID) return 0;

  addrcp(target, (*bpp)->data + AXALEN);
  pullup(bpp, NULL, 15);
  if (!*bpp) return 1;
  circuit_manager(bpp, target);
  return 1;
}

/*---------------------------------------------------------------------------*/
/******************************** Login Server *******************************/
/*---------------------------------------------------------------------------*/

#include "login.h"

/*---------------------------------------------------------------------------*/

static void nrserv_recv_upcall(struct circuit *pc, int cnt)
{
  struct mbuf *bp;

  bp = 0;
  recv_nr(pc, &bp, 0);
  login_write((struct login_cb *) pc->user, &bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_send_upcall(struct circuit *pc, int cnt)
{
  struct mbuf *bp;

  if ((bp = login_read((struct login_cb *) pc->user, space_nr(pc))))
    send_nr(pc, &bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_send_login_upcall(void *arg)
{
  nrserv_send_upcall((struct circuit *) arg, 0);
}

static void nrserv_close_upcall(void *arg)
{
  close_nr((struct circuit *) arg);
}

static void nrserv_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate)
{  switch (newstate) {
  case NR4STCON:
    /* A configured target first - "listen netrom add ..." - and only then the
     * node's own login, which is what an incoming L4 session always got.
     */
    if (nrserv_listen_start(pc)) break;
    pc->user = (char *) login_open(nr_addr2str(pc), "NETROM", nrserv_send_login_upcall, nrserv_close_upcall, pc);
    if (!pc->user) close_nr(pc);
    break;
  case NR4STDISC:
    /* Which of the two is behind this circuit?  Its own receive upcall says
     * so, so there is nothing to remember.
     */
    if (pc->r_upcall == nrserv_recv_upcall)
      login_close((struct login_cb *) pc->user);
    else
      nrserv_listen_close(pc);
    del_nr(pc);
    break;
  default:
    break;
  }
}

/*---------------------------------------------------------------------------*/
/*********************************** Client **********************************/
/*---------------------------------------------------------------------------*/

#include "session.h"

/*---------------------------------------------------------------------------*/

static void nrclient_parse(char *buf, int n)
{
  struct mbuf *bp;

  if (!(Current && Current->type == NRSESSION && Current->cb.netrom)) return;
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return;
  bp = qdata(buf, n);
  send_nr(Current->cb.netrom, &bp);
  if (Current->record) {
    if (buf[n-1] == '\r') buf[n-1] = '\n';
    fwrite(buf, 1, n, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

void nrclient_send_upcall(struct circuit *pc, int cnt)
{

  uint8 *p;
  int chr;
  struct mbuf *bp;
  struct session *s;

  if (!(s = (struct session *) pc->user) || !s->upload || cnt <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (cnt) {
    if ((chr = getc(s->upload)) == EOF) break;
    if (chr == '\n') chr = '\r';
    *p++ = chr;
    cnt--;
  }
  if ((bp->cnt = p - bp->data))
    send_nr(pc, &bp);
  else
    free_p(&bp);
  if (cnt) {
    fclose(s->upload);
    s->upload = 0;
    free(s->ufile);
    s->ufile = 0;
  }
}

/*---------------------------------------------------------------------------*/

void nrclient_recv_upcall(struct circuit *pc, int cnt)
{

  int c;
  struct mbuf *bp;

  if (!(Mode == CONV_MODE && Current && Current->type == NRSESSION && Current->cb.netrom == pc)) return;
  recv_nr(pc, &bp, 0);
  while ((c = PULLCHAR(&bp)) != -1) {
    if (c == '\r') c = '\n';
    putchar(c);
    if (Current->record) putc(c, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

static void nrclient_state_upcall(struct circuit *pc, enum netrom_state oldstate, enum netrom_state newstate)
{
  int notify;

  notify = (Current && Current->type == NRSESSION && Current == (struct session *) pc->user);
  if (newstate != NR4STDISC) {
    if (notify) printf("%s\n", Nr4states[newstate]);
  } else {
    if (notify) printf("%s (%s)\n", Nr4states[newstate], Nr4reasons[pc->reason]);
    if (pc->user) freesession((struct session *) pc->user);
    del_nr(pc);
    if (notify) cmdmode();
  }
}

/*---------------------------------------------------------------------------*/

static int donconnect(int argc, char *argv[], void *p)
{

  struct session *s;
  uint8 cuser[AXALEN];
  uint8 node[AXALEN];

  if (setcall(node, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (!nodeptr(node, 0)) {
    printf("Unknown node \"%s\"\n", argv[1]);
    return 1;
  }
  if (argc < 3) {
    cuser[0] = 0;
  } else if (setcall(cuser, argv[2])) {
    printf("Invalid call \"%s\"\n", argv[2]);
    return 1;
  }
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  Current = s;
  s->type = NRSESSION;
  s->name = NULL;
  s->cb.netrom = 0;
  s->parse = nrclient_parse;
  if (!(s->cb.netrom = open_nr(node, cuser, 0, nrclient_recv_upcall, nrclient_send_upcall, nrclient_state_upcall, (char *) s))) {
    freesession(s);
    switch (Net_error) {
    case NONE:
      printf("No error\n");
      break;
    case CON_EXISTS:
      printf("Connection already exists\n");
      break;
    case NO_CONN:
      printf("Connection does not exist\n");
      break;
    case CON_CLOS:
      printf("Connection closing\n");
      break;
    case NO_MEM:
      printf("No memory\n");
      break;
    case WOULDBLK:
      printf("Would block\n");
      break;
    case NOPROTO:
      printf("Protocol or mode not supported\n");
      break;
    case INVALID:
      printf("Invalid arguments\n");
      break;
    }
    return 1;
  }
  go(argc, argv, p);
  return 0;
}

/*---------------------------------------------------------------------------*/
/****************************** NETROM Commands ******************************/
/*---------------------------------------------------------------------------*/

int nr_attach(int argc, char *argv[], void *p)
{
  char *ifname = "netrom";

  if (Nr_iface || if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return -1;
  }
  Nr_iface = (struct iface *) callocw(1, sizeof(struct iface));
  Nr_iface->addr = Ip_addr;
  Nr_iface->broadcast = 0xffffffffUL;
  Nr_iface->netmask = 0xffffffffUL;
  Nr_iface->name = strdup(ifname);
  Nr_iface->hwaddr = (uint8 *) mallocw(AXALEN);
  memcpy(Nr_iface->hwaddr, Mycall, AXALEN);
  Nr_iface->mtu = NR4MAXINFO;
  /* And it is not merely the default - a NET/ROM information field IS
   * NR4MAXINFO, so "ifconfig netrom mtu 1500" is not a setting but a lie.
   * Declaring it lets ifmtu() say so.
   */
  Nr_iface->framemax = NR4MAXINFO;
  setencap(Nr_iface, "NETROM");
  Nr_iface->next = Ifaces;
  Ifaces = Nr_iface;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* The entry the operator means by its number, counting from 1 - the same
 * shape as "listen ax25 enable <n>", and the number is the position in the
 * whole list so it means the same thing wherever it was read.
 */

static struct broadcast *broadcast_number(const char *s)
{
  struct broadcast *bp;
  int n = atoi(s);

  if (n < 1) return 0;
  for (bp = broadcasts; bp; bp = bp->next)
    if (!--n) return bp;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dobroadcast(int argc, char *argv[], void *p)
{
  struct broadcast *bp;
  int n;

  if (argc >= 2 && (!strcmp(argv[1], "enable") || !strcmp(argv[1], "disable"))) {
    if (argc < 3) {
      printf("Usage: netrom broadcast %s <n>\n", argv[1]);
      return 1;
    }
    if (!(bp = broadcast_number(argv[2]))) {
      printf("No broadcast entry %s\n", argv[2]);
      return 1;
    }
    bp->disabled = !strcmp(argv[1], "disable");
    return 0;
  }

  if (argc < 3) {
    /* Contents as well as state, because they are two different switches and
     * only one of them lived here.  Whether we broadcast at all is this
     * command; WHAT the broadcast carries is "netrom filter port=<iface> feed
     * yes|no", and an operator looking for the shortened broadcast looks
     * here first and used to find no sign that it existed.
     */
    puts(" #  Interface  State  Contents  Path");
    for (n = 1, bp = broadcasts; bp; n++, bp = bp->next)
      printf("%2d  %-9s  %-5s  %-8s  %s\n", n, bp->iface->name,
	     bp->disabled ? "off" : "on",
	     bp->disabled ? "-" : (broadcast_feeds(bp) ? "nodes" : "us only"),
	     ax25hdr_to_string(&bp->hdr));
    if (broadcasts)
      puts("\"us only\" is \"netrom filter port=<iface> feed no\": the identifier"
	   " and no entries");
    return 0;
  }

  bp = (struct broadcast *) calloc(1, sizeof(struct broadcast));
  if (!(bp->iface = if_lookup(argv[1]))) {
    printf("Interface \"%s\" unknown\n", argv[1]);
    free(bp);
    return 1;
  }
  if (bp->iface->output != ax_output) {
    printf("Interface \"%s\" not kiss\n", argv[1]);
    free(bp);
    return 1;
  }
  addrcp(bp->hdr.source, bp->iface->hwaddr);
  if (ax25args_to_hdr(argc - 2, argv + 2, &bp->hdr)) {
    free(bp);
    return 1;
  }
  bp->hdr.cmdrsp = LAPB_COMMAND;
  /* Appended, not prepended, and that is the whole reason for the loop: the
   * entries are addressed by their position, so entry 1 has to stay the first
   * line of net.rc.  Prepending would renumber everything each time a line
   * was added, and "netrom broadcast disable 1" in net.rc would then turn off
   * whichever entry happened to be written last.
   */
  {
    struct broadcast **bpp;

    for (bpp = &broadcasts; *bpp; bpp = &(*bpp)->next) ;
    *bpp = bp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dofilter(int argc, char *argv[], void *p)
{
  return rf_cmd(RF_NETROM, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int doident(int argc, char *argv[], void *p)
{

  char *cp;
  int i;

  if (argc < 2)
    printf("Ident %-6.6s\n", mynode->ident);
  else
    for (cp = argv[1], i = 0; i < IDENTLEN; i++)
      mynode->ident[i] = *cp ? *cp++ : ' ';
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Force a retransmission */

static int donkick(int argc, char *argv[], void *p)
{
  struct circuit *pc;

  pc = (struct circuit *) ltop(htol(argv[1]));
  if (!valid_nr(pc)) {
    printf("%s", Notval);
    return 1;
  }
  kick_nr(pc);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* netrom peer [add|del <call>]
 *
 * The list itself does nothing yet - an interlink is not built here.  It is
 * the thing everything else needs first: who is a partner is a decision, not
 * something to be read off the air, and INP3 cannot start a connection to
 * somebody nobody named.
 *
 * "add" and "del" rather than the "--delete" that the filter uses, because
 * this is a list of stations and not a set of rules: flexnet link add/del is
 * the same list and the operator should not have to remember two spellings.
 */

static int donrpeer(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  int anyssid;
  struct nrpeer *pp, **ppp;
  uint8 call[AXALEN];

  (void) p;

  if (argc < 2) {
    struct node *pn;

    if (!nrpeers) {
      printf("No interlink partners - \"netrom peer add <call>\"\n");
      return 0;
    }
    printf("Call       SSID   Interlink   INP3   SNTT     His      Last     "
	   "Known as\n");
    for (pp = nrpeers; pp; pp = pp->next) {
      char inp3[16], sntt[16], last[16], his[16];
      uint8 *target = nrpeer_target(pp);
      struct ax25_cb *axp = target ? find_ax25(NULL, target) : NULL;
      int seen = 0;

      /* Seconds for reading, 10 ms units on the wire.  A dash rather than
       * "0.00" while nothing has been measured: zero is a value with a
       * meaning of its own here, and it is not "unknown".
       */
      if (pp->srtt)
	sprintf(sntt, "%d.%02ds", pp->srtt / 100, pp->srtt % 100);
      else
	strcpy(sntt, "-");
      if (pp->lastrtt)
	sprintf(last, "%d.%02ds", pp->lastrtt / 100, pp->lastrtt % 100);
      else
	strcpy(last, "-");
      /* HIS measurement of us, out of his own frame.  Shown because nothing
       * is announced until it is there, and a dash in this column is then the
       * whole explanation for a partner that is up, agreed and silent.
       */
      if (pp->hissrtt)
	sprintf(his, "%d.%02ds", pp->hissrtt / 100, pp->hissrtt % 100);
      else
	strcpy(his, "-");
      /* His "$M" belongs beside his "$N", because it only means anything
       * once he speaks INP3: it is the ceiling HE puts on what we may report
       * to him, and it is shown so that a value we have taken can be seen.
       */
      if (!pp->inp3)
	strcpy(inp3, "-");
      else if (pp->maxtime)
	sprintf(inp3, "<%ds", pp->maxtime / 100);
      else
	strcpy(inp3, "yes");

      printf("%-9s  %-5s  %-10s  %-5s  %-7s  %-7s  %-7s  ", pax25(buf, pp->call),
	     pp->anyssid ? "any" : "exact",
	     /* Not "down" when we do not even know whom to call: an SSID-less
	      * entry with nobody heard of yet is waiting, not failing.
	      */
	     axp    ? Ax25states[axp->state] :
	     target ? "down" : "no call yet",
	     inp3, sntt, his, last);
      /* What the entry actually catches today.  With "any" it may be more
       * than one, and that is the whole point of writing it that way - so
       * show them rather than leaving the operator to guess.
       */
      for (pn = nodes; pn; pn = pn->next) {
	if (pn == mynode || !nrpeer_match(pp, pn->call)) continue;
	printf("%s%s%s", seen++ ? ", " : "", pax25(buf, pn->call),
	       nr_is_neighbour(pn->call) ? " (neighbour)" : "");
      }
      printf("%s\n", seen ? "" : "-");
    }
    return 0;
  }

  if (!strcmp(argv[1], "add") || !strcmp(argv[1], "del")) {
    if (argc < 3) {
      printf("Which station?\n");
      return 1;
    }
    if (setcall(call, argv[2])) {
      printf("Invalid call \"%s\"\n", argv[2]);
      return 1;
    }
    /* An SSID was MEANT only if one was written.  setcall() cannot say so -
     * it fills in zero - so the text is what decides.
     */
    anyssid = (strchr(argv[2], '-') == NULL);

    if (*argv[1] == 'd') {
      for (ppp = &nrpeers; (pp = *ppp); ppp = &pp->next)
	if (pp->anyssid == anyssid && addreq(pp->call, call)) {
	  *ppp = pp->next;
	  /* His ways go with him: struct nrinp3 points at this entry, and
	   * they would be reading freed memory a moment later.
	   */
	  inp3_drop_peer(pp);
	  free(pp);
	  return 0;
	}
      printf("No such partner \"%s\"\n", argv[2]);
      return 1;
    }

    if (addreq(call, Mycall)) {
      printf("That is us\n");
      return 1;
    }
    for (pp = nrpeers; pp; pp = pp->next)
      if (pp->anyssid == anyssid && addreq(pp->call, call)) return 0;
    if (!(pp = (struct nrpeer *) calloc(1, sizeof(struct nrpeer)))) {
      printf("%s", Nospace);
      return 1;
    }
    addrcp(pp->call, call);
    pp->anyssid = anyssid;
    pp->next = nrpeers;
    nrpeers = pp;
    /* Call him now rather than at the end of the first interval: a line in
     * net.rc should not mean a minute of nothing.  The timer runs from here
     * on and is not started before there is a partner to look after.
     */
    nrpeer_poll(pp);
    if (!run_timer(&nrpeer_timer)) {
      set_timer(&nrpeer_timer, NRPEER_INTERVAL * 1000L);
      start_timer(&nrpeer_timer);
    }
    return 0;
  }

  printf("There is no \"%s\" here - the words are \"add\" and \"del\"\n",
	 argv[1]);
  return 1;
}

/*---------------------------------------------------------------------------*/

static int dolinks(int argc, char *argv[], void *p)
{

  char buf1[20], buf2[20];
  uint8 call[AXALEN];
  int quality;
  long timestamp;
  struct link *pl;
  struct linkinfo *pi;
  struct node *pn1 = 0;
  struct node *pn2 = 0;

  if (argc >= 2) {
    if (setcall(call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    if (argc > 2) {
      if (!(pn1 = nodeptr(call, 1))) {
	printf("Destination list full (parameter 1)\n");
	return 1;
      }
    } else if (!(pn1 = nodeptr(call, 0))) {
      printf("Unknown node \"%s\"\n", argv[1]);
      return 1;
    }
  }

  if (argc <= 2) {
    printf("From       To         Level  Quality   Age\n");
    for (pn2 = nodes; pn2; pn2 = pn2->next)
      if (argc < 2 || pn1 == pn2) {
	pax25(buf1, pn2->call);
	for (pl = pn2->links; pl; pl = pl->next) {
	  pax25(buf2, pl->node->call);
	  if (pl->info->time != PERMANENT)
	    printf("%-9s  %-9s  %5d  %7d  %4ld\n", buf1, buf2, pl->info->source, pl->info->quality, secclock() - pl->info->time);
	  else
	    printf("%-9s  %-9s  %5d  %7d\n", buf1, buf2, pl->info->source, pl->info->quality);
	}
      }
    return 0;
  }

  if (argc < 4 || argc > 5) {
    printf("Usage: netrom links [<node> [<node2> <quality> [permanent]]]\n");
    return 1;
  }

  if (setcall(call, argv[2])) {
    printf("Invalid call \"%s\"\n", argv[2]);
    return 1;
  }
  if (!(pn2 = nodeptr(call, 1))) {
    printf("Destination list full (parameter 1)\n");
    return 1;
  }
  if (pn1 == pn2) {
    printf("Both calls are identical\n");
    return 1;
  }

  quality = atoi(argv[3]);
  if (quality < 0 || quality > 255) {
    printf("Quality must be 0..255\n");
    return 1;
  }

  if (argc < 5)
    timestamp = secclock();
  else {
    if (strncmp(argv[4], "permanent", strlen(argv[4]))) {
      printf("Usage: netrom links [<node> [<node2> <quality> [permanent]]]\n");
      return 1;
    }
    timestamp = PERMANENT;
  }

  pi = linkinfoptr(pn1, pn2);
  pi->quality = quality;
  pi->source = 1;
  pi->time = timestamp;
  calculate_all();
  return 0;
}

/*---------------------------------------------------------------------------*/

static int donodes(int argc, char *argv[], void *p)
{

  char buf1[20], buf2[20];
  uint8 call[AXALEN];
  struct node *pn = 0;
  struct node *pn1 = 0;

  if (argc >= 2) {
    if (setcall(call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    if (!(pn1 = nodeptr(call, 0))) {
      printf("Unknown node \"%s\"\n", argv[1]);
      return 1;
    }
  }
  /* In and Adv are what is IN FORCE for this node, not what was written
   * about him: the rule may be his port's or the default, and working that
   * out by hand across three levels is what the operator should not have to
   * do.  A node we only ever heard about has no port and so has no port rule.
   */
  printf("Node       Ident   Neighbor   Level  Quality  In        Adv  INP3\n");
  for (pn = nodes; pn; pn = pn->next)
    if (argc < 2 || pn == pn1) {
      char tt[16];
      struct nrinp3 *best = inp3_best(pn);

      pax25(buf1, pn->call);
      if (pn->neighbor)
	pax25(buf2, pn->neighbor->call);
      else
	*buf2 = '\0';
      /* The target time, so the column is comparable across destinations:
       * what he reports plus what the way to him costs.
       */
      if (best) {
	int t = best->time + best->peer->srtt;

	sprintf(tt, "%d.%02ds", t / 100, t % 100);
      } else
	strcpy(tt, "-");
      printf("%-9s  %-6.6s  %-9s  %5d  %7d  %-8s  %-3s  %s\n", buf1, pn->ident,
	     buf2, pn->hopcnt, (int) pn->quality,
	     rf_in_name(node_in(pn)), node_advert(pn) ? "yes" : "no", tt);
      /* Named on its own, the node is worth spelling out: every INP3 way
       * rather than the best, since which partner reports what is the
       * question one asks a single node about.
       */
      if (pn1) {
	struct nrinp3 *rp;

	for (rp = pn->inp3; rp; rp = rp->next) {
	  char told[24];

	  /* Both halves of the cell, because they answer different questions
	   * and only one of them is his: what he tells us, and what we last
	   * told him.  A cell may hold either alone - a way he reports and we
	   * do not pass on, or one we announce out of the graph that he never
	   * mentioned - so neither is printed when it is not there.
	   */
	  if (rp->reported)
	    sprintf(told, "told %d.%02ds", rp->reported / 100, rp->reported % 100);
	  else
	    strcpy(told, "told nothing");
	  if (rp->time)
	    printf("  INP3 via %-9s  route %d.%02ds  +link %d.%02ds  %d hops  "
		   "age %lds  %s\n",
		   pax25(buf2, rp->peer->call),
		   rp->time / 100, rp->time % 100,
		   rp->peer->srtt / 100, rp->peer->srtt % 100,
		   rp->hops, secclock() - rp->stamp, told);
	  else
	    printf("  INP3 to  %-9s  no way of his own, %s\n",
		   pax25(buf2, rp->peer->call), told);
	}
	if (pn->inp3_ip)
	  printf("  INP3 ip  %s/%d\n", inet_ntoa(pn->inp3_ip), pn->inp3_ipbits);
	/* Named but not shown: we cannot read them, and printing bytes we do
	 * not understand would only invite somebody to interpret them.  That
	 * they are CARRIED is the point, and that is what the count says.
	 */
	if (pn->inp3_optlen)
	  printf("  INP3 opt %d byte carried through\n", pn->inp3_optlen);
      }
    }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doparms(int argc, char *argv[], void *p)
{
  int i, j;

  switch (argc) {
  case 0:
  case 1:
    for (i = 1; i <= NPARMS; i++)
      printf("%s %10d\n", parms[i].text, *parms[i].valptr);
    return 0;
  case 2:
  case 3:
    i = atoi(argv[1]);
    if (i < 1 || i > NPARMS) {
      printf("parameter # must be 1..%d\n", NPARMS);
      return 1;
    }
    if (argc == 2) {
      printf("%s %10d\n", parms[i].text, *parms[i].valptr);
      return 0;
    }
    j = atoi(argv[2]);
    if (j < parms[i].minval || j > parms[i].maxval) {
      printf("parameter %d must be %d..%d\n", i, parms[i].minval, parms[i].maxval);
      return 1;
    }
    *parms[i].valptr = j;
    if (dur_timer(&broadcast_timer) != nr_bdcstint * 1000L) {
      set_timer(&broadcast_timer, nr_bdcstint * 1000L);
      start_timer(&broadcast_timer);
    }
    return 0;
  default:
    printf("Usage: netrom parms [<parm#> [<parm value>]]\n");
    return 1;
  }
}

/*---------------------------------------------------------------------------*/

static int donreset(int argc, char *argv[], void *p)
{
  struct circuit *pc;

  pc = (struct circuit *) htol(argv[1]);
  if (!valid_nr(pc)) {
    printf("%s", Notval);
    return 1;
  }
  reset_nr(pc);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int donstatus(int argc, char *argv[], void *p)
{

  int i;
  struct circuit *pc;
  struct mbuf *bp;

  if (argc < 2) {
    if (!Shortstatus)
      printf("bdcsts rcvd %d bdcsts sent %d\n", routes_stat.rcvd, routes_stat.sent);
    printf("   &NRCB Rcv-Q Unack  Rt  Srtt  State          Remote socket\n");
    for (pc = circuits; pc; pc = pc->next)
      printf("%8lx %5u%c%3u/%u%c %2d %5.1f  %-13s  %s\n",
	     (long) pc,
	     pc->rcvcnt,
	     pc->chokesent ? '*' : ' ',
	     pc->unack,
	     pc->cwind,
	     pc->remote_busy ? '*' : ' ',
	     pc->retry,
	     pc->srtt / 1000.0,
	     Nr4states[pc->state],
	     nr_addr2str(pc));
    if (server_enabled)
      printf("                                Listening      *\n");
  } else {
    pc = (struct circuit *) htol(argv[1]);
    if (!valid_nr(pc)) {
      printf("Not a valid control block address\n");
      return 1;
    }
    printf("Address:      %s\n", nr_addr2str(pc));
    printf("Remote id:    %d/%d\n", pc->remoteindex, pc->remoteid);
    printf("Local id:     %d/%d\n", pc->localindex, pc->localid);
    printf("State:        %s\n", Nr4states[pc->state]);
    if (pc->reason)
      printf("Reason:       %s\n", Nr4reasons[pc->reason]);
    printf("Window:       %d\n", pc->window);
    printf("NAKsent:      %s\n", pc->naksent ? "Yes" : "No");
    printf("CHOKEsent:    %s\n", pc->chokesent ? "Yes" : "No");
    printf("Closed:       %s\n", pc->closed ? "Yes" : "No");
    if (pc->remote_busy)
      printf("Remote_busy:  %lu ms\n",(unsigned long)TDIFF(msclock(), pc->remote_busy));
    else
      printf("Remote_busy:  No\n");
    printf("CWind:        %d\n", pc->cwind);
    printf("Retry:        %d\n", pc->retry);
    printf("Srtt:         %ld ms\n",(long)pc->srtt);
    printf("Mean dev:     %ld ms\n",(long)pc->mdev);
    printf("Timer T1:     ");
    if (run_timer(&pc->timer_t1))
      printf("%lu",(unsigned long)read_timer(&pc->timer_t1));
    else
      printf("stop");
    printf("/%lu ms\n",(unsigned long)dur_timer(&pc->timer_t1));
    printf("Timer T3:     ");
    if (run_timer(&pc->timer_t3))
      printf("%lu",(unsigned long)read_timer(&pc->timer_t3));
    else
      printf("stop");
    printf("/%lu ms\n",(unsigned long)dur_timer(&pc->timer_t3));
    printf("Timer T4:     ");
    if (run_timer(&pc->timer_t4))
      printf("%lu",(unsigned long)read_timer(&pc->timer_t4));
    else
      printf("stop");
    printf("/%lu ms\n",(unsigned long)dur_timer(&pc->timer_t4));
    printf("Rcv queue:    %d\n", pc->rcvcnt);
    if (pc->reseq) {
      printf("Reassembly queue:\n");
      for (bp = pc->reseq; bp; bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n", bp->data[2], len_p(bp));
    }
    printf("Snd queue:    %d\n", len_p(pc->sndq));
    if (pc->resndq) {
      printf("Resend queue:\n");
      for (i = 0, bp = pc->resndq; bp; i++, bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n",
	       (pc->send_state - pc->unack + i) & 0xff, len_p(bp));
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int donetrom(int argc, char *argv[], void *p)
{

  static struct cmds netromcmds[] = {
    { "broadcast",dobroadcast,0, 0,
      "netrom broadcast                       list the entries, numbered from 1\n"
      "       netrom broadcast <iface> <dest> [via <digi>...]\n"
      "       netrom broadcast enable|disable <n>\n"
      "  <dest> is what the nodes broadcast is addressed to, usually NODES.\n"
      "  This says WHETHER we broadcast on a port.  WHAT it carries is\n"
      "  \"netrom filter port=<iface> feed yes|no\" - \"no\" sends the identifier\n"
      "  and no entries, so the neighbour learns that we are here and nothing\n"
      "  about our nodes." },
    { "connect",  donconnect, 0, 2, "netrom connect <node> [<user>]" },
    { "filter",   dofilter,   0, 0, Rf_usage_netrom },
    { "ident",    doident,    0, 0, "netrom ident [<alias>]" },
    { "kick",     donkick,    0, 2, "netrom kick <nrcb>" },
    { "links",    dolinks,    0, 0, "netrom links                           our neighbours" },
    { "nodes",    donodes,    0, 0, "netrom nodes [<node>]                  the routing table" },
    { "parms",    doparms,    0, 0, "netrom parms [<n> <value>]...          list or set the parameters" },
    { "peer",     donrpeer,   0, 0,
      "netrom peer                            the interlink partners\n"
      "       netrom peer add|del <call>\n"
      "  Whom we run an INP3 interlink with.  NET/ROM needs no such list -\n"
      "  it broadcasts, and a neighbour is whoever answers - but INP3 runs\n"
      "  connected and has to be told whom to call.  The same statement as\n"
      "  \"flexnet link add\", and not the same question as \"netrom filter\",\n"
      "  which says what flows once somebody speaks to us.\n"
      "  Written WITHOUT an SSID the entry matches any: the callsign a link\n"
      "  runs under and the node's own ID need not carry the same one.\n"
      "  \"netrom links\" is a different thing - the edges we have learned." },
    { "reset",    donreset,   0, 2, "netrom reset <nrcb>" },
    { "status",   donstatus,  0, 0, "netrom status [<nrcb>]                 the transport circuits" },
    { NULL,       NULL,       0, 0, NULL }
  };

  return subcmd(netromcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

int nr4start(int argc, char *argv[], void *p)
{
  server_enabled = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

int nr40(int argc, char *argv[], void *p)
{
  server_enabled = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

void netrom_initialize(void)
{
  link_manager_initialize();
  routing_manager_initialize();
}
