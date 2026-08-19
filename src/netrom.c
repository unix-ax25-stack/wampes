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
  { "27 Maximum transport circuits (0=unlimited)   ", &nr_maxcircuits, 0,      65535 }
};

#define NPARMS 27

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

struct node {
  uint8 *call;
  char ident[IDENTLEN];
  int hopcnt;
  struct link *links;
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
};

static struct nrpeer *nrpeers;
static struct timer nrpeer_timer;

/* How often we look whether the interlinks are still up.  A minute is short
 * enough that a link comes back soon after the far end does, and long enough
 * that a station which is simply not there costs one call a minute.  AX.25
 * retries do the rest of the waiting - open_ax25() is not a fast operation
 * that is being repeated here, it is a connection attempt that either stands
 * or is still running.
 */

#define NRPEER_INTERVAL 60

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

  struct ax25 hdr;
  struct ax25_cb *axp;

  if (!(axp = find_ax25(pn->call))) {
    memset(&hdr, 0, sizeof(struct ax25));
    addrcp(hdr.dest, pn->call);
    axp = open_ax25(&hdr, AX_ACTIVE, 0);
    if (!axp) {
      if (update_link(mynode, pn, 1, 0)) calculate_all();
      free_p(bpp);
      return;
    }
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

static struct ax25_cb *nrpeer_link(struct nrpeer *pp, int *isnew)
{
  uint8 *call;
  struct ax25 hdr;
  struct ax25_cb *axp;

  if (isnew) *isnew = 0;
  if (!(call = nrpeer_target(pp))) return NULL;
  if (!(axp = find_ax25(call))) {
    memset(&hdr, 0, sizeof(struct ax25));
    addrcp(hdr.dest, call);
    if (!(axp = open_ax25(&hdr, AX_ACTIVE, 0))) return NULL;
  }
  if (pp->id != axp->id) {
    pp->id = axp->id;
    if (isnew) *isnew = 1;
  }
  return axp;
}

/*---------------------------------------------------------------------------*/

static void nrpeer_service(void *arg)
{
  int isnew;
  struct nrpeer *pp;

  (void) arg;
  set_timer(&nrpeer_timer, NRPEER_INTERVAL * 1000L);
  start_timer(&nrpeer_timer);

  for (pp = nrpeers; pp; pp = pp->next)
    (void) nrpeer_link(pp, &isnew);
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
    if (pn != mynode && !pn->links && !pn->force_broadcast) {
      if (pn->prev)
	pn->prev->next = pn->next;
      else
	nodes = pn->next;
      if (pn->next) pn->next->prev = pn->prev;
      free(pn->call);
      free(pn);
      nnodes--;
    }
  }
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
      if (pn->hopcnt >= hopcnt && (((int) pn->quality) || pn->force_broadcast)) {
	if (pn->hopcnt == hopcnt) {
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
	   */
	  addrcp(p, pn->neighbor ? (node_advert(pn->neighbor) ?
				    pn->neighbor->call : mynode->call)
			         : pn->call);
	  p += AXALEN;
	  *p++ = (char) pn->quality;
	  if ((bp->cnt = p - bp->data) > 258 - NRRTDESTLEN) {
	    send_broadcast_packet(&bp);
	    routes_stat.sent++;
	    bp = NULL;
	  }
	} else if (pn->hopcnt < nexthopcnt) {
	  nexthopcnt = pn->hopcnt;
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
    if (pn == mynode) goto discard;  /* ROUTING ERROR */
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
    send_packet_to_neighbor(bpp, fromneighbor);
    return;
  }

  if (!(pn = nodeptr((*bpp)->data + AXALEN, 1))) goto discard;
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

void nr3_input(struct iface *iface, const uint8 *src, struct mbuf **bpp)
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
  if (bpp && *bpp && (*bpp)->cnt && *(*bpp)->data == 0xff)
    broadcast_recv(bpp, pn);
  else
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
    printf("Call       SSID   Interlink     Known as\n");
    for (pp = nrpeers; pp; pp = pp->next) {
      uint8 *target = nrpeer_target(pp);
      struct ax25_cb *axp = target ? find_ax25(target) : NULL;
      int seen = 0;

      printf("%-9s  %-5s  %-12s  ", pax25(buf, pp->call),
	     pp->anyssid ? "any" : "exact",
	     /* Not "down" when we do not even know whom to call: an SSID-less
	      * entry with nobody heard of yet is waiting, not failing.
	      */
	     axp    ? Ax25states[axp->state] :
	     target ? "down" : "no call yet");
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
    (void) nrpeer_link(pp, NULL);
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
  printf("Node       Ident   Neighbor   Level  Quality  In        Adv\n");
  for (pn = nodes; pn; pn = pn->next)
    if (argc < 2 || pn == pn1) {
      pax25(buf1, pn->call);
      if (pn->neighbor)
	pax25(buf2, pn->neighbor->call);
      else
	*buf2 = '\0';
      printf("%-9s  %-6.6s  %-9s  %5d  %7d  %-8s  %s\n", buf1, pn->ident, buf2,
	     pn->hopcnt, (int) pn->quality,
	     rf_in_name(node_in(pn)), node_advert(pn) ? "yes" : "no");
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
