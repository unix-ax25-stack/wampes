/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/netrom.c,v 1.42 1994/09/05 12:47:17 deyke Exp $ */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "config.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "arp.h"
#include "ip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "cmdparse.h"
#include "trace.h"

static int nr_maxdest     =   400;      /* not used */
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
static int nr_t2init      =     1;      /* not used */
static int nr_callcheck   =     0;      /* not used */
static int nr_beacon      =     0;      /* not used */
static int nr_cq          =     0;      /* not used */

static struct parms {
  char *text;
  int *valptr;
  int minval;
  int maxval;
} parms[] = {
  "",                                               (int *) 0,       0,          0,
  " 1 Maximum destination list entries           ", &nr_maxdest,     1,        400,
  " 2 Worst quality for auto-updates             ", &nr_minqual,     0,        255,
  " 3 Channel 0 (HDLC) quality                   ", &nr_hfqual,      0,        255,
  " 4 Channel 1 (RS232) quality                  ", &nr_rsqual,      0,        255,
  " 5 Obsolescence count initializer (0=off)     ", &nr_obsinit,     0,        255,
  " 6 Obsolescence count min to be broadcast     ", &nr_minobs,      0,        255,
  " 7 Auto-update broadcast interval (sec, 0=off)", &nr_bdcstint,    0,      65535,
  " 8 Network 'time-to-live' initializer         ", &nr_ttlinit,     1,        255,
  " 9 Transport timeout (sec)                    ", &nr_ttimeout,    5,        600,
  "10 Transport maximum tries                    ", &nr_tretry,      1,        127,
  "11 Transport acknowledge delay (ms)           ", &nr_tackdelay,   1,      60000,
  "12 Transport busy delay (sec)                 ", &nr_tbsydelay,   1,       1000,
  "13 Transport requested window size (frames)   ", &nr_twindow,     1,        127,
  "14 Congestion control threshold (frames)      ", &nr_tnoackbuf,   1,        127,
  "15 No-activity timeout (sec, 0=off)           ", &nr_timeout,     0,      65535,
  "16 Persistance                                ", &nr_persistance, 0,        255,
  "17 Slot time (10msec increments)              ", &nr_slottime,    0,        127,
  "18 Link T1 timeout 'FRACK' (ms)               ", &T1init,         1, 0x7fffffff,
  "19 Link TX window size 'MAXFRAME' (frames)    ", &Maxframe,       1,          7,
  "20 Link maximum tries (0=forever)             ", &N2,             0,        127,
  "21 Link T2 timeout (ms)                       ", &nr_t2init,      1, 0x7fffffff,
  "22 Link T3 timeout (ms)                       ", &T3init,         0, 0x7fffffff,
  "23 AX.25 digipeating  (0=off 1=dumb 2=s&f)    ", &Digipeat,       0,          2,
  "24 Validate callsigns (0=off 1=on)            ", &nr_callcheck,   0,          1,
  "25 Station ID beacons (0=off 1=after 2=every) ", &nr_beacon,      0,          2,
  "26 CQ UI frames       (0=off 1=on)            ", &nr_cq,          0,          1,
};

#define NPARMS 26

struct link;

static struct node *nodeptr(const char *call, int create);
static void send_packet_to_neighbor(struct mbuf *data, struct node *pn);
static void send_broadcast_packet(struct mbuf *data);
static void link_manager_initialize(void);
static struct linkinfo *linkinfoptr(struct node *node1, struct node *node2);
static int update_link(struct node *node1, struct node *node2, int source, int quality);
static int link_valid(struct node *pn, struct link *pl);
static void calculate_hopcnts(struct node *pn);
static void calculate_qualities(struct node *pn);
static void calculate_all(void);
static void broadcast_recv(struct mbuf *bp, struct node *pn);
static struct mbuf *alloc_broadcast_packet(void);
static void send_broadcast(void);
static void route_packet(struct mbuf *bp, struct node *fromneighbor);
static void send_l3_packet(char *source, char *dest, int ttl, struct mbuf *data);
static void routing_manager_initialize(void);
static void reset_t1(struct circuit *pc);
static int nrbusy(struct circuit *pc);
static void send_l4_packet(struct circuit *pc, int opcode, struct mbuf *data);
static void try_send(struct circuit *pc, int fill_sndq);
static void set_circuit_state(struct circuit *pc, int newstate);
static void l4_t1_timeout(struct circuit *pc);
static void l4_t3_timeout(struct circuit *pc);
static void l4_t4_timeout(struct circuit *pc);
static struct circuit *create_circuit(void);
static void circuit_manager(struct mbuf *bp);
static void nrserv_recv_upcall(struct circuit *pc, int cnt);
static void nrserv_send_upcall(struct circuit *pc, int cnt);
static void nrserv_state_upcall(struct circuit *pc, int oldstate, int newstate);
static void nrclient_parse(char *buf, int n);
static void nrclient_state_upcall(struct circuit *pc, int oldstate, int newstate);
static int donconnect(int argc, char *argv[], void *p);
static int dobroadcast(int argc, char *argv[], void *p);
static int doident(int argc, char *argv[], void *p);
static int donkick(int argc, char *argv[], void *p);
static int dolinks(int argc, char *argv[], void *p);
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
  struct broadcast *next;
};

struct node {
  char *call;
  char ident[IDENTLEN];
  int hopcnt;
  struct link *links;
  struct node *neighbor, *old_neighbor;
  double quality, old_quality, tmp_quality;
  int force_broadcast;
  struct node *prev, *next;
};

static struct broadcast *broadcasts;
static struct node *nodes, *mynode;

/*---------------------------------------------------------------------------*/

static struct node *nodeptr(const char *call, int create)
{
  struct node *pn;

  for (pn = nodes; pn && !addreq(call, pn->call); pn = pn->next) ;
  if (!pn && create) {
    pn = (struct node *) calloc(1, sizeof(*pn));
    pn->call = malloc(AXALEN);
    addrcp(pn->call, call);
    memset(pn->ident, ' ', IDENTLEN);
    pn->hopcnt = INFINITY;
    if (nodes) {
      pn->next = nodes;
      nodes->prev = pn;
    }
    nodes = pn;
  }
  return pn;
}

/*---------------------------------------------------------------------------*/

static void send_packet_to_neighbor(struct mbuf *data, struct node *pn)
{

  struct ax25 hdr;
  struct ax25_cb *axp;
  struct mbuf *bp;

  if (!(axp = find_ax25(pn->call))) {
    hdr.ndigis = hdr.nextdigi = 0;
    addrcp(hdr.dest, pn->call);
    axp = open_ax25(&hdr, AX_ACTIVE, NULLVFP, NULLVFP, NULLVFP, (char *) 0);
    if (!axp) {
      if (update_link(mynode, pn, 1, 0)) calculate_all();
      free_p(data);
      return;
    }
  }
  if (!(bp = pushdown(data, 1))) {
    free_p(data);
    return;
  }
  bp->data[0] = PID_NETROM;
  send_ax25(axp, bp, -1);
}

/*---------------------------------------------------------------------------*/

static void send_broadcast_packet(struct mbuf *data)
{

  struct broadcast *p;
  struct mbuf *bp, *bp1;

  for (p = broadcasts; p; p = p->next) {
    addrcp(p->hdr.source, p->iface->hwaddr);
    dup_p(&bp, data, 0, MAXINT16);
    if (!(bp1 = htonax25(&p->hdr, bp)))
      free_p(bp);
    else if (p->iface->forw)
      (*p->iface->forw->raw)(p->iface->forw, bp1);
    else
      (*p->iface->raw)(p->iface, bp1);
  }
  free_p(data);
}

/*---------------------------------------------------------------------------*/

static void link_manager_initialize(void)
{

  mynode = nodeptr(Mycall, 1);
  free(mynode->call);
  mynode->call = Mycall;
  calculate_all();
}

/*---------------------------------------------------------------------------*/
/****************************** Routing Manager ******************************/
/*---------------------------------------------------------------------------*/

#define NRRTDESTLEN     21              /* length of destination entry in */
					/* nodes broadcast */
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
  pi = (struct linkinfo *) calloc(1, sizeof(*pi));
  pi->source = INFINITY;
  pl = (struct link *) calloc(1, sizeof(*pl));
  pl->node = node2;
  pl->info = pi;
  if (node1->links) {
    pl->next = node1->links;
    node1->links->prev = pl;
  }
  node1->links = pl;
  pl = (struct link *) calloc(1, sizeof(*pl));
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
	for (pl1 = pn1->links; pl1->node != pn; pl1 = pl1->next) ;
	if (pl1->prev)
	  pl1->prev->next = pl1->next;
	else
	  pn1->links = pl1->next;
	if (pl1->next) pl1->next->prev = pl1->prev;
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
#ifdef FORCE_BC
  if (start_broadcast_timer) {
    set_timer(&broadcast_timer, 10 * 1000L);
    start_timer(&broadcast_timer);
  }
#endif

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
    }
  }
}

/*---------------------------------------------------------------------------*/

static void broadcast_recv(struct mbuf *bp, struct node *pn)
{

  char buf[NRRTDESTLEN];
  char ident[IDENTLEN];
  int quality;
  struct linkinfo *pi;
  struct node *pb, *pd;

  routes_stat.rcvd++;
  if (pn == mynode) goto discard;
  if (PULLCHAR(&bp) != 0xff) goto discard;
  if (pullup(&bp, ident, IDENTLEN) != IDENTLEN) goto discard;
  if (len_p(bp) % NRRTDESTLEN) goto discard;
  if (*ident > ' ') memcpy(pn->ident, ident, IDENTLEN);
  update_link(mynode, pn, 1, nr_hfqual);
  while (pullup(&bp, buf, NRRTDESTLEN) == NRRTDESTLEN) {
    if (addreq(buf, Mycall)) continue;
    pd = nodeptr(buf, 1);
    if (buf[AXALEN] > ' ') memcpy(pd->ident, buf + AXALEN, IDENTLEN);
    pb = nodeptr(buf + AXALEN + IDENTLEN, 1);
    quality = uchar(buf[AXALEN+IDENTLEN+AXALEN]);
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
  free_p(bp);
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

static void send_broadcast(void)
{

  char *p;
  int hopcnt, nexthopcnt;
  struct mbuf *bp;
  struct node *pn;

  set_timer(&broadcast_timer, nr_bdcstint * 1000L);
  start_timer(&broadcast_timer);
  calculate_all();
  if (!broadcasts) return;
  bp = alloc_broadcast_packet();
  for (hopcnt = 1; hopcnt <= INFINITY; hopcnt = nexthopcnt) {
    nexthopcnt = INFINITY + 1;
    for (pn = nodes; pn; pn = pn->next)
      if (pn->hopcnt >= hopcnt && (((int) pn->quality) || pn->force_broadcast))
	if (pn->hopcnt == hopcnt) {
	  pn->force_broadcast = 0;
	  if (!bp) bp = alloc_broadcast_packet();
	  p = bp->data + bp->cnt;
	  addrcp(p, pn->call);
	  p += AXALEN;
	  memcpy(p, pn->ident, IDENTLEN);
	  p += IDENTLEN;
	  addrcp(p, pn->neighbor ? pn->neighbor->call : pn->call);
	  p += AXALEN;
	  *p++ = pn->quality;
	  if ((bp->cnt = p - bp->data) > 258 - NRRTDESTLEN) {
	    send_broadcast_packet(bp);
	    routes_stat.sent++;
	    bp = NULLBUF;
	  }
	} else if (pn->hopcnt < nexthopcnt)
	  nexthopcnt = pn->hopcnt;
  }
  if (bp) {
    send_broadcast_packet(bp);
    routes_stat.sent++;
  }
}

/*---------------------------------------------------------------------------*/

static void route_packet(struct mbuf *bp, struct node *fromneighbor)
{

  int ttl;
  int32 ipaddr;
  struct arp_tab *ap;
  struct node *pn;

  if (!bp || bp->cnt < 15) goto discard;

  if (fromneighbor != mynode) {
    if (update_link(mynode, fromneighbor, 1, nr_hfqual)) calculate_all();
    pn = nodeptr(bp->data, 1);
    if (pn == mynode) goto discard;  /* ROUTING ERROR */
    if (!pn->neighbor) {
      struct linkinfo *pi = linkinfoptr(mynode, fromneighbor);
      if (pi->quality) {
	int q = 1;
	while (q * pi->quality / 256 < 1) q++;
	if (update_link(fromneighbor, pn, 2, q)) calculate_all();
      }
    }
  }

  if (addreq(bp->data + AXALEN, Mycall)) {
    char hwaddr[AXALEN];
    if (bp->cnt >= 40                     &&
	uchar(bp->data[19]) == 0          &&
	uchar(bp->data[15]) == NRPROTO_IP &&
	uchar(bp->data[16]) == NRPROTO_IP &&
	(ipaddr = get32(bp->data + 32))   &&
	Nr_iface) {
      Nr_iface->rawrecvcnt++;
      Nr_iface->lastrecv = secclock();
      if ((ap = arp_lookup(ARP_NETROM, ipaddr)) == NULLARP ||
	  ap->state != ARP_VALID ||
	  run_timer(&ap->timer)) {
	addrcp(hwaddr, bp->data);
	arp_add(ipaddr, ARP_NETROM, hwaddr, 0);
      }
      pullup(&bp, NULLCHAR, 20);
      dump(Nr_iface, IF_TRACE_IN, bp);
      ip_route(Nr_iface, bp, 0);
      return;
    }
    pullup(&bp, NULLCHAR, 15);
    if (!bp) return;
    if (fromneighbor == mynode) {
      struct mbuf *hbp = copy_p(bp, len_p(bp));
      free_p(bp);
      bp = hbp;
    }
    circuit_manager(bp);
    return;
  }

  ttl = uchar(bp->data[2*AXALEN]);
  if (--ttl <= 0) goto discard;
  bp->data[2*AXALEN] = ttl;

  pn = nodeptr(bp->data + AXALEN, 1);
  if (!pn->neighbor) {
    if (fromneighbor != mynode) {
      pn->force_broadcast = 1;
#ifdef FORCE_BC
      send_broadcast();
#endif
    }
    goto discard;
  }

#ifdef FORCE_BC
  if (pn->neighbor == fromneighbor ||
      addreq(pn->neighbor->call, bp->data)) send_broadcast();
#endif

  send_packet_to_neighbor(bp, pn->neighbor);
  return;

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/

static void send_l3_packet(char *source, char *dest, int ttl, struct mbuf *data)
{
  struct mbuf *bp;

  if (!(bp = pushdown(data, 2 * AXALEN + 1))) {
    free_p(data);
    return;
  }
  addrcp(bp->data, source);
  addrcp(bp->data + AXALEN, dest);
  if (++ttl > 255) ttl = 255;
  bp->data[2*AXALEN] = ttl;
  route_packet(bp, mynode);
}

/*---------------------------------------------------------------------------*/

int nr_send(struct mbuf *bp, struct iface *iface, int32 gateway, int tos)
{

  struct arp_tab *arp;
  struct mbuf *nbp;

  dump(iface, IF_TRACE_OUT, bp);
  iface->rawsndcnt++;
  iface->lastsent = secclock();
  if (!(arp = arp_lookup(ARP_NETROM, gateway))) {
    free_p(bp);
    return (-1);
  }
  if (!(nbp = pushdown(bp, 5))) {
    free_p(bp);
    return (-1);
  }
  bp = nbp;
  bp->data[0] = NRPROTO_IP;
  bp->data[1] = NRPROTO_IP;
  bp->data[2] = 0;
  bp->data[3] = 0;
  bp->data[4] = 0;
  if (iface->trace & IF_TRACE_RAW)
    raw_dump(iface, -1, bp);
  send_l3_packet(Mycall, arp->hw_addr, nr_ttlinit, bp);
  return 0;
}

/*---------------------------------------------------------------------------*/

void nr3_input(const char *src, struct mbuf *bp)
{
  if (bp && bp->cnt && uchar(*bp->data) == 0xff)
    broadcast_recv(bp, nodeptr(src, 1));
  else
    route_packet(bp, nodeptr(src, 1));
}

/*---------------------------------------------------------------------------*/

static void routing_manager_initialize(void)
{
  broadcast_timer.func = (void (*)()) send_broadcast;
  set_timer(&broadcast_timer, 10 * 1000L);
  start_timer(&broadcast_timer);
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

static void send_l4_packet(struct circuit *pc, int opcode, struct mbuf *data)
{

  int start_t1_timer = 0;
  struct mbuf *bp;

  switch (opcode & NR4OPCODE) {
  case NR4OPCONRQ:
    if (!(bp = pushdown(data, 20))) {
      free_p(data);
      return;
    }
    bp->data[0] = pc->localindex;
    bp->data[1] = pc->localid;
    bp->data[2] = 0;
    bp->data[3] = 0;
    bp->data[4] = opcode;
    bp->data[5] = pc->window;
    addrcp(bp->data + 6, pc->cuser);
    addrcp(bp->data + 13, Mycall);
    start_t1_timer = 1;
    break;
  case NR4OPCONAK:
    if (!(bp = pushdown(data, 6))) {
      free_p(data);
      return;
    }
    bp->data[0] = pc->remoteindex;
    bp->data[1] = pc->remoteid;
    bp->data[2] = pc->localindex;
    bp->data[3] = pc->localid;
    bp->data[4] = opcode;
    bp->data[5] = pc->window;
    break;
  case NR4OPDISRQ:
    start_t1_timer = 1;
  case NR4OPDISAK:
    if (!(bp = pushdown(data, 5))) {
      free_p(data);
      return;
    }
    bp->data[0] = pc->remoteindex;
    bp->data[1] = pc->remoteid;
    bp->data[2] = 0;
    bp->data[3] = 0;
    bp->data[4] = opcode;
    break;
  case NR4OPINFO:
    start_t1_timer = 1;
  case NR4OPACK:
    if (!(bp = pushdown(data, 5))) {
      free_p(data);
      return;
    }
    if (pc->reseq && !pc->naksent) {
      opcode |= NR4NAK;
      pc->naksent = 1;
    }
    if ((pc->chokesent = nrbusy(pc))) opcode |= NR4CHOKE;
    pc->response = 0;
    bp->data[0] = pc->remoteindex;
    bp->data[1] = pc->remoteid;
    bp->data[2] = pc->send_state;
    bp->data[3] = pc->recv_state;
    bp->data[4] = opcode;
    if ((opcode & NR4OPCODE) == NR4OPINFO)
      pc->send_state = uchar(pc->send_state + 1);
    break;
  default:
    free_p(data);
    return;
  }
  if (start_t1_timer) start_timer(&pc->timer_t1);
  send_l3_packet(Mycall, pc->node, nr_ttlinit, bp);
}

/*---------------------------------------------------------------------------*/

static void try_send(struct circuit *pc, int fill_sndq)
{

  int cnt;
  struct mbuf *bp;

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
    enqueue(&pc->resndq, bp);
    pc->unack++;
    pc->sndtime[pc->send_state] = msclock();
    dup_p(&bp, bp, 0, cnt);
    send_l4_packet(pc, NR4OPINFO, bp);
  }
}

/*---------------------------------------------------------------------------*/

static void set_circuit_state(struct circuit *pc, int newstate)
{
  int oldstate;

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
    send_l4_packet(pc, NR4OPCONRQ, NULLBUF);
    break;
  case NR4STCON:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    try_send(pc, 1);
    break;
  case NR4STDPEND:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    send_l4_packet(pc, NR4OPDISRQ, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t1_timeout(struct circuit *pc)
{
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
      send_l4_packet(pc, NR4OPCONRQ, NULLBUF);
    break;
  case NR4STCON:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, NR4STDPEND);
    else if (pc->unack) {
      pc->send_state = uchar(pc->send_state - pc->unack);
      for (qp = pc->resndq; qp; qp = qp->anext) {
	pc->sndtime[pc->send_state] = 0;
	dup_p(&bp, qp, 0, NR4MAXINFO);
	send_l4_packet(pc, NR4OPINFO, bp);
      }
    }
    break;
  case NR4STDPEND:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, NR4STDISC);
    else
      send_l4_packet(pc, NR4OPDISRQ, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t3_timeout(struct circuit *pc)
{
  if (!run_timer(&pc->timer_t1)) close_nr(pc);
}

/*---------------------------------------------------------------------------*/

static void l4_t4_timeout(struct circuit *pc)
{
  pc->remote_busy = 0;
  if (pc->unack) start_timer(&pc->timer_t1);
  try_send(pc, 1);
}

/*---------------------------------------------------------------------------*/

static struct circuit *create_circuit(void)
{

  static int nextid;
  struct circuit *pc;

  pc = (struct circuit *) calloc(1, sizeof(*pc));
  nextid++;
  pc->localindex = uchar(nextid >> 8);
  pc->localid = uchar(nextid);
  pc->remoteindex = -1;
  pc->remoteid = -1;
  pc->state = NR4STDISC;
  pc->cwind = 1;
  pc->mdev = (1000L * nr_ttimeout + 2) / 4;
  reset_t1(pc);
  pc->timer_t1.func = (void (*)()) l4_t1_timeout;
  pc->timer_t1.arg = pc;
  pc->timer_t3.func = (void (*)()) l4_t3_timeout;
  pc->timer_t3.arg = pc;
  pc->timer_t4.func = (void (*)()) l4_t4_timeout;
  pc->timer_t4.arg = pc;
  pc->next = circuits;
  return circuits = pc;
}

/*---------------------------------------------------------------------------*/

static void circuit_manager(struct mbuf *bp)
{

  int nakrcvd;
  struct circuit *pc;
  struct mbuf *p;

  if (!bp || bp->cnt < 5) goto discard;

  if ((bp->data[4] & NR4OPCODE) == NR4OPCONRQ) {
    if (bp->cnt < 20) goto discard;
    for (pc = circuits; pc; pc = pc->next)
      if (pc->remoteindex == uchar(bp->data[0]) &&
	  pc->remoteid == uchar(bp->data[1]) &&
	  addreq(pc->cuser, bp->data + 6) &&
	  addreq(pc->node, bp->data + 13)) break;
    if (!pc) {
      pc = create_circuit();
      pc->remoteindex = uchar(bp->data[0]);
      pc->remoteid = uchar(bp->data[1]);
      addrcp(pc->cuser, bp->data + 6);
      addrcp(pc->node, bp->data + 13);
      pc->r_upcall = nrserv_recv_upcall;
      pc->t_upcall = nrserv_send_upcall;
      pc->s_upcall = nrserv_state_upcall;
    }
  } else
    for (pc = circuits; ; pc = pc->next) {
      if (!pc) goto discard;
      if (pc->localindex == uchar(bp->data[0]) &&
	  pc->localid == uchar(bp->data[1])) break;
    }

  set_timer(&pc->timer_t3, nr_timeout * 1000L);
  start_timer(&pc->timer_t3);

  switch (bp->data[4] & NR4OPCODE) {

  case NR4OPCONRQ:
    switch (pc->state) {
    case NR4STDISC:
      pc->window = uchar(bp->data[5]);
      if (pc->window > nr_twindow) pc->window = nr_twindow;
      if (pc->window < 1) pc->window = 1;
      if (server_enabled) {
	send_l4_packet(pc, NR4OPCONAK, NULLBUF);
	set_circuit_state(pc, NR4STCON);
      } else {
	send_l4_packet(pc, NR4OPCONAK | NR4CHOKE, NULLBUF);
	del_nr(pc);
      }
      break;
    case NR4STCON:
      send_l4_packet(pc, NR4OPCONAK, NULLBUF);
      break;
    default:
      goto discard;
    }
    break;

  case NR4OPCONAK:
    if (pc->state != NR4STCPEND) goto discard;
    pc->remoteindex = uchar(bp->data[2]);
    pc->remoteid = uchar(bp->data[3]);
    if (pc->window > uchar(bp->data[5])) pc->window = uchar(bp->data[5]);
    if (pc->window < 1) pc->window = 1;
    if (bp->data[4] & NR4CHOKE) {
      pc->reason = NR4RREFUSED;
      set_circuit_state(pc, NR4STDISC);
    } else
      set_circuit_state(pc, NR4STCON);
    break;

  case NR4OPDISRQ:
    send_l4_packet(pc, NR4OPDISAK, NULLBUF);
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
    if (bp->data[4] & NR4CHOKE) {
      if (!pc->remote_busy) pc->remote_busy = msclock();
      set_timer(&pc->timer_t4, nr_tbsydelay * 1000L);
      start_timer(&pc->timer_t4);
    } else {
      pc->remote_busy = 0;
      stop_timer(&pc->timer_t4);
    }
    if (uchar(pc->send_state - bp->data[3]) < pc->unack) {
      pc->retry = 0;
      if (pc->sndtime[uchar(bp->data[3]-1)]) {
	int32 rtt = msclock() - pc->sndtime[uchar(bp->data[3]-1)];
	int32 abserr = (rtt > pc->srtt) ? rtt - pc->srtt : pc->srtt - rtt;
	pc->srtt = ((AGAIN - 1) * pc->srtt + rtt + (AGAIN / 2)) / AGAIN;
	pc->mdev = ((DGAIN - 1) * pc->mdev + abserr + (DGAIN / 2)) / DGAIN;
	reset_t1(pc);
	if (pc->cwind < pc->window) pc->cwind++;
      }
      while (uchar(pc->send_state - bp->data[3]) < pc->unack) {
	pc->resndq = free_p(pc->resndq);
	pc->unack--;
      }
    }
    nakrcvd = bp->data[4] & NR4NAK;
    if ((bp->data[4] & NR4OPCODE) == NR4OPINFO) {
      pc->response = 1;
      if (uchar(bp->data[2] - pc->recv_state) < pc->window) {
	if (!pc->reseq || (bp->data[2] - pc->reseq->data[2]) & 0x80) {
	  bp->anext = pc->reseq;
	  pc->reseq = bp;
	  bp = NULLBUF;
	} else {
	  for (p = pc->reseq;
	       p->next && (p->data[2] - bp->data[2] - 1) & 0x80;
	       p = p->anext) ;
	  if (p->data[2] != bp->data[2]) {
	    bp->anext = p->anext;
	    p->anext = bp;
	    bp = NULLBUF;
	  }
	}
	while (pc->reseq && !uchar(pc->reseq->data[2] - pc->recv_state)) {
	  p = pc->reseq;
	  pc->reseq = p->anext;
	  p->anext = NULLBUF;
	  pc->recv_state = uchar(pc->recv_state + 1);
	  pullup(&p, NULLCHAR, 5);
	  if (p) {
	    pc->rcvcnt += len_p(p);
	    append(&pc->rcvq, p);
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
      pc->send_state = uchar(pc->send_state - pc->unack);
      pc->sndtime[pc->send_state] = 0;
      dup_p(&bp1, pc->resndq, 0, NR4MAXINFO);
      send_l4_packet(pc, NR4OPINFO, bp1);
      pc->send_state = old_send_state;
    }
    try_send(pc, 1);
    if (pc->response) send_l4_packet(pc, NR4OPACK, NULLBUF);
    if (pc->unack && !pc->remote_busy) start_timer(&pc->timer_t1);
    if (pc->closed && !pc->sndq && !pc->unack)
      set_circuit_state(pc, NR4STDPEND);
    break;
  }

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/
/********************************* User Calls ********************************/
/*---------------------------------------------------------------------------*/

struct circuit *open_nr(char *node, char *cuser, int window, void (*r_upcall)(struct circuit *p, int cnt), void (*t_upcall)(struct circuit *p, int cnt), void (*s_upcall)(struct circuit *p, int oldstate, int newstate), char *user)
{
  struct circuit *pc;

  if (!nodeptr(node, 0)) {
    Net_error = INVALID;
    return 0;
  }
  if (!cuser) cuser = Mycall;
  if (!window) window = nr_twindow;
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

int send_nr(struct circuit *pc, struct mbuf *bp)
{
  int cnt;

  if (!(pc && bp)) {
    free_p(bp);
    Net_error = INVALID;
    return (-1);
  }
  switch (pc->state) {
  case NR4STDISC:
    free_p(bp);
    Net_error = NO_CONN;
    return (-1);
  case NR4STCPEND:
  case NR4STCON:
    if (!pc->closed) {
      if ((cnt = len_p(bp))) {
	append(&pc->sndq, bp);
	try_send(pc, 0);
      }
      return cnt;
    }
  case NR4STDPEND:
    free_p(bp);
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int space_nr(struct circuit *pc)
{
  int cnt;

  if (!pc) {
    Net_error = INVALID;
    return (-1);
  }
  switch (pc->state) {
  case NR4STDISC:
    Net_error = NO_CONN;
    return (-1);
  case NR4STCPEND:
  case NR4STCON:
    if (!pc->closed) {
      cnt = (pc->cwind - pc->unack) * NR4MAXINFO - len_p(pc->sndq);
      return (cnt > 0) ? cnt : 0;
    }
  case NR4STDPEND:
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int recv_nr(struct circuit *pc, struct mbuf **bpp, int cnt)
{
  if (!(pc && bpp)) {
    Net_error = INVALID;
    return (-1);
  }
  if (pc->rcvcnt) {
    if (!cnt || pc->rcvcnt <= cnt) {
      *bpp = dequeue(&pc->rcvq);
      cnt = len_p(*bpp);
    } else {
      if (!(*bpp = alloc_mbuf(cnt))) {
	Net_error = NO_MEM;
	return (-1);
      }
      pullup(&pc->rcvq, (*bpp)->data, cnt);
      (*bpp)->cnt = cnt;
    }
    pc->rcvcnt -= cnt;
    if (pc->chokesent && !nrbusy(pc)) send_l4_packet(pc, NR4OPACK, NULLBUF);
    return cnt;
  }
  switch (pc->state) {
  case NR4STCPEND:
  case NR4STCON:
    *bpp = NULLBUF;
    Net_error = WOULDBLK;
    return (-1);
  case NR4STDISC:
  case NR4STDPEND:
    *bpp = NULLBUF;
    return 0;
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int close_nr(struct circuit *pc)
{
  if (!pc) {
    Net_error = INVALID;
    return (-1);
  }
  if (pc->closed) {
    Net_error = CON_CLOS;
    return (-1);
  }
  pc->closed = 1;
  switch (pc->state) {
  case NR4STDISC:
    Net_error = NO_CONN;
    return (-1);
  case NR4STCPEND:
    set_circuit_state(pc, NR4STDISC);
    return 0;
  case NR4STCON:
    if (!pc->sndq && !pc->unack) set_circuit_state(pc, NR4STDPEND);
    return 0;
  case NR4STDPEND:
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int reset_nr(struct circuit *pc)
{
  if (!pc) {
    Net_error = INVALID;
    return (-1);
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
      return (-1);
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
/******************************** Login Server *******************************/
/*---------------------------------------------------------------------------*/

#include "login.h"

/*---------------------------------------------------------------------------*/

static void nrserv_recv_upcall(struct circuit *pc, int cnt)
{
  struct mbuf *bp;

  recv_nr(pc, &bp, 0);
  login_write((struct login_cb *) pc->user, bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_send_upcall(struct circuit *pc, int cnt)
{
  struct mbuf *bp;

  if ((bp = login_read((struct login_cb *) pc->user, space_nr(pc))))
    send_nr(pc, bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_state_upcall(struct circuit *pc, int oldstate, int newstate)
{
  switch (newstate) {
  case NR4STCON:
    pc->user = (char *) login_open(nr_addr2str(pc), "NETROM", (void (*)()) nrserv_send_upcall, (void (*)()) close_nr, pc);
    if (!pc->user) close_nr(pc);
    break;
  case NR4STDISC:
    login_close((struct login_cb *) pc->user);
    del_nr(pc);
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
  if (!(Current && Current->type == NRSESSION && Current->cb.netrom)) return;
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return;
  send_nr(Current->cb.netrom, qdata(buf, n));
  if (Current->record) {
    if (buf[n-1] == '\r') buf[n-1] = '\n';
    fwrite(buf, 1, n, Current->record);
  }
}

/*---------------------------------------------------------------------------*/

void nrclient_send_upcall(struct circuit *pc, int cnt)
{

  char *p;
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
    send_nr(pc, bp);
  else
    free_p(bp);
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

static void nrclient_state_upcall(struct circuit *pc, int oldstate, int newstate)
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

  char node[AXALEN], cuser[AXALEN];
  struct session *s;

  if (setcall(node, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (!nodeptr(node, 0)) {
    printf("Unknown node \"%s\"\n", argv[1]);
    return 1;
  }
  if (argc < 3)
    addrcp(cuser, Mycall);
  else if (setcall(cuser, argv[2])) {
    printf("Invalid call \"%s\"\n", argv[2]);
    return 1;
  }
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  Current = s;
  s->type = NRSESSION;
  s->name = NULLCHAR;
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

  if (Nr_iface || if_lookup(ifname) != NULLIF) {
    printf("Interface %s already exists\n", ifname);
    return (-1);
  }
  Nr_iface = callocw(1, sizeof(*Nr_iface));
  Nr_iface->addr = Ip_addr;
  Nr_iface->broadcast = 0xffffffffL;
  Nr_iface->netmask = 0xffffffffL;
  Nr_iface->name = strdup(ifname);
  Nr_iface->hwaddr = mallocw(AXALEN);
  memcpy(Nr_iface->hwaddr, Mycall, AXALEN);
  Nr_iface->mtu = NR4MAXINFO;
  setencap(Nr_iface, "NETROM");
  Nr_iface->next = Ifaces;
  Ifaces = Nr_iface;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dobroadcast(int argc, char *argv[], void *p)
{
  struct broadcast *bp;

  if (argc < 3) {
    puts("Interface  Path");
    for (bp = broadcasts; bp; bp = bp->next)
      printf("%-9s  %s\n", bp->iface->name, ax25hdr_to_string(&bp->hdr));
    return 0;
  }

  bp = (struct broadcast *) calloc(1, sizeof(*bp));
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
  bp->next = broadcasts;
  broadcasts = bp;
  return 0;
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
      mynode->ident[i] = *cp ? Xtoupper(*cp++) : ' ';
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Force a retransmission */

static int donkick(int argc, char *argv[], void *p)
{
  struct circuit *pc;

  pc = (struct circuit *) ltop(htol(argv[1]));
  if (!valid_nr(pc)) {
    printf(Notval);
    return 1;
  }
  kick_nr(pc);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dolinks(int argc, char *argv[], void *p)
{

  char buf1[20], buf2[20];
  char call[AXALEN];
  int quality;
  long timestamp;
  struct link *pl;
  struct linkinfo *pi;
  struct node *pn1, *pn2;

  if (argc >= 2) {
    if (setcall(call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    if (argc > 2)
      pn1 = nodeptr(call, 1);
    else if (!(pn1 = nodeptr(call, 0))) {
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
	    printf("%-9s  %-9s  %5i  %7i  %4i\n", buf1, buf2, pl->info->source, pl->info->quality, secclock() - pl->info->time);
	  else
	    printf("%-9s  %-9s  %5i  %7i\n", buf1, buf2, pl->info->source, pl->info->quality);
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
  pn2 = nodeptr(call, 1);
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
  char call[AXALEN];
  struct node *pn, *pn1;

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
  printf("Node       Ident   Neighbor   Level  Quality\n");
  for (pn = nodes; pn; pn = pn->next)
    if (argc < 2 || pn == pn1) {
      pax25(buf1, pn->call);
      if (pn->neighbor)
	pax25(buf2, pn->neighbor->call);
      else
	*buf2 = '\0';
      printf("%-9s  %-6.6s  %-9s  %5i  %7i\n", buf1, pn->ident, buf2, pn->hopcnt, (int) pn->quality);
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
    printf(Notval);
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
      printf("Remote_busy:  %lu ms\n", msclock() - pc->remote_busy);
    else
      printf("Remote_busy:  No\n");
    printf("CWind:        %d\n", pc->cwind);
    printf("Retry:        %d\n", pc->retry);
    printf("Srtt:         %ld ms\n", pc->srtt);
    printf("Mean dev:     %ld ms\n", pc->mdev);
    printf("Timer T1:     ");
    if (run_timer(&pc->timer_t1))
      printf("%lu", read_timer(&pc->timer_t1));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&pc->timer_t1));
    printf("Timer T3:     ");
    if (run_timer(&pc->timer_t3))
      printf("%lu", read_timer(&pc->timer_t3));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&pc->timer_t3));
    printf("Timer T4:     ");
    if (run_timer(&pc->timer_t4))
      printf("%lu", read_timer(&pc->timer_t4));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&pc->timer_t4));
    printf("Rcv queue:    %d\n", pc->rcvcnt);
    if (pc->reseq) {
      printf("Reassembly queue:\n");
      for (bp = pc->reseq; bp; bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n", uchar(bp->data[2]), len_p(bp));
    }
    printf("Snd queue:    %d\n", len_p(pc->sndq));
    if (pc->resndq) {
      printf("Resend queue:\n");
      for (i = 0, bp = pc->resndq; bp; i++, bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n",
	       uchar(pc->send_state - pc->unack + i), len_p(bp));
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int donetrom(int argc, char *argv[], void *p)
{

  static struct cmds netromcmds[] = {
    "broadcast",dobroadcast,0, 0, NULLCHAR,
    "connect",  donconnect, 0, 2, "netrom connect <node> [<user>]",
    "ident",    doident,    0, 0, NULLCHAR,
    "kick",     donkick,    0, 2, "netrom kick <nrcb>",
    "links",    dolinks,    0, 0, NULLCHAR,
    "nodes",    donodes,    0, 0, NULLCHAR,
    "parms",    doparms,    0, 0, NULLCHAR,
    "reset",    donreset,   0, 2, "netrom reset <nrcb>",
    "status",   donstatus,  0, 0, NULLCHAR,
    NULLCHAR,   NULLFP,     0, 0, NULLCHAR
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

