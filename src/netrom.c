#include <memory.h>
#include <stdio.h>

#include "global.h"
#include "config.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "arp.h"
#include "ax25.h"
#include "axproto.h"
#include "cmdparse.h"

extern int  digipeat;
extern long  currtime;
extern void free();

static int  nr_maxdest     =    50;     /* not used */
static int  nr_minqual     =     0;     /* not used */
static int  nr_hfqual      =   192;
static int  nr_rsqual      =   255;     /* not used */
static int  nr_obsinit     =     3;
static int  nr_minobs      =     0;     /* not used */
static int  nr_bdcstint    =  1800;
static int  nr_ttlinit     =    16;
static int  nr_ttimeout    =   120;
static int  nr_tretry      =     5;
static int  nr_tackdelay   =     6;
static int  nr_tbsydelay   =   180;
static int  nr_twindow     =     8;
static int  nr_tnoackbuf   =     4;     /* not used */
static int  nr_timeout     =  1800;
static int  nr_persistance =    64;     /* not used */
static int  nr_slottime    =    10;     /* not used */
static int  nr_callcheck   =     0;     /* not used */
static int  nr_beacon      =     0;     /* not used */
static int  nr_cq          =     1;     /* not used */

static struct parms {
  char  *text;
  int  *valptr;
  int  minval;
  int  maxval;
} parms[] = {
  "",                                               (int *) 0,       0,     0,
  " 1 Maximum destination list entries           ", &nr_maxdest,     1,   400,
  " 2 Worst quality for auto-updates             ", &nr_minqual,     0,   255,
  " 3 Channel 0 (HDLC) quality                   ", &nr_hfqual,      0,   255,
  " 4 Channel 1 (RS232) quality                  ", &nr_rsqual,      0,   255,
  " 5 Obsolescence count initializer (0=off)     ", &nr_obsinit,     0,   255,
  " 6 Obsolescence count min to be broadcast     ", &nr_minobs,      1,   255,
  " 7 Auto-update broadcast interval (sec, 0=off)", &nr_bdcstint,    0, 65535,
  " 8 Network 'time-to-live' initializer         ", &nr_ttlinit,     0,   255,
  " 9 Transport timeout (sec)                    ", &nr_ttimeout,    5,   600,
  "10 Transport maximum tries                    ", &nr_tretry,      2,   127,
  "11 Transport acknowledge delay (sec)          ", &nr_tackdelay,   1,    60,
  "12 Transport busy delay (sec)                 ", &nr_tbsydelay,   1,  1000,
  "13 Transport requested window size (frames)   ", &nr_twindow,     1,   127,
  "14 Congestion control threshold (frames)      ", &nr_tnoackbuf,   1,   127,
  "15 No-activity timeout (sec, 0=off)           ", &nr_timeout,     0, 65535,
  "16 Persistance                                ", &nr_persistance, 0,   255,
  "17 Slot time (10msec increments)              ", &nr_slottime,    0,   127,
  "18 Link T1 timeout 'FRACK' (sec)              ", &ax_t1init,      1,    15,
  "19 Link TX window size 'MAXFRAME' (frames)    ", &ax_maxframe,    1,     7,
  "20 Link maximum tries (0=forever)             ", &ax_retry,       0,   127,
  "21 Link T2 timeout (sec)                      ", &ax_t2init,      1, 65535,
  "22 Link T3 timeout (sec)                      ", &ax_t3init,      0, 65535,
  "23 AX.25 digipeating  (0=off 1=dumb 2=s&f)    ", &digipeat,       0,     2,
  "24 Validate callsigns (0=off 1=on)            ", &nr_callcheck,   0,     1,
  "25 Station ID beacons (0=off 1=after 2=every) ", &nr_beacon,      0,     2,
  "26 CQ UI frames       (0=off 1=on)            ", &nr_cq,          0,     1,
};

#define NPARMS 26

/*---------------------------------------------------------------------------*/
/******************************** Link Manager *******************************/
/*---------------------------------------------------------------------------*/

#define FAILED  0       /* Set to 1 if broadcasts are used */

struct broadcast {
  struct interface *ifp;
  char *path;
  struct broadcast *next;
};

struct neighbor {
  struct ax25_addr call;
  int  quality;
  int  failed;
  int  permanent;
  int  usecount;
  struct axcb *crosslink;
  struct neighbor *prev, *next;
};

struct axcb *netrom_server_axcb;

static struct broadcast *broadcasts;
static struct neighbor *neighbors;
static void calculate_routes();

/*---------------------------------------------------------------------------*/

static void addrcp(to, from)
register struct ax25_addr *to, *from;
{
  *((long *)to)++ = *((long *)from)++;
  *((short *)to)++ = *((short *)from)++;
  *((char *)to) = (*((char *)from) & SSID) | 0x60;
}

/*---------------------------------------------------------------------------*/

static struct neighbor *neighborptr(call, create)
struct ax25_addr *call;
int  create;
{
  register struct neighbor *pn;

  for (pn = neighbors; pn && !addreq(call, &pn->call); pn = pn->next) ;
  if (!pn && create) {
    pn = (struct neighbor *) calloc(1, sizeof(struct neighbor ));
    addrcp(&pn->call, call);
    pn->quality = nr_hfqual;
    if (neighbors) {
      pn->next = neighbors;
      neighbors->prev = pn;
    }
    neighbors = pn;
  }
  return pn;
}

/*---------------------------------------------------------------------------*/

static void delete_neighbor(pn)
register struct neighbor *pn;
{
  if (pn->prev) pn->prev->next = pn->next;
  if (pn->next) pn->next->prev = pn->prev;
  if (pn == neighbors) neighbors = pn->next;
  free((char *) pn);
}

/*---------------------------------------------------------------------------*/

static void ax25_state_upcall(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  register struct neighbor *pn;

  if (cp->user)
    pn = (struct neighbor *) cp->user;
  else
    cp->user = (char *) (pn = neighborptr(axptr(cp->path), 1));
  switch (newstate) {
  case CONNECTING:
    break;
  case CONNECTED:
    if (pn->failed) {
      pn->failed = 0;
      calculate_routes();
    }
    pn->crosslink = cp;
    break;
  case DISCONNECTING:
    if (cp->reason != NORMAL && !pn->failed) {
      pn->failed = FAILED;
      calculate_routes();
    }
    break;
  case DISCONNECTED:
    if (cp->reason != NORMAL && !pn->failed) {
      pn->failed = FAILED;
      calculate_routes();
    }
    del_ax(cp);
    pn->crosslink = NULLAXCB;
    if (!pn->permanent && !pn->usecount) delete_neighbor(pn);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void ax25_recv_upcall(cp, cnt)
struct axcb *cp;
int16 cnt;
{

  char  pid;
  struct mbuf *bp;
  void route_packet();

  recv_ax(cp, &bp, 0);
  if (pullup(&bp, &pid, 1) != 1) return;
  if (uchar(pid) == (PID_NETROM | PID_FIRST | PID_LAST))
    route_packet(bp, (struct neighbor *) cp->user);
  else
    free_p(bp);
}

/*---------------------------------------------------------------------------*/

static void send_packet_to_neighbor(data, neighbor)
struct mbuf *data;
struct neighbor *neighbor;
{

  char  path[10*AXALEN];
  struct mbuf *bp;

  if (!neighbor->crosslink) {
    addrcp(axptr(path), &neighbor->call);
    addrcp(axptr(path + AXALEN), &mycall);
    path[AXALEN+6] |= E;
    neighbor->crosslink = open_ax(path, AX25_ACTIVE, ax25_recv_upcall, NULLVFP, ax25_state_upcall, (char *) neighbor);
    if (!neighbor->crosslink) {
      if (!neighbor->failed) {
	neighbor->failed = FAILED;
	calculate_routes();
      }
      free_p(data);
      return;
    }
    neighbor->crosslink->mode = DGRAM;
  }
  if (!(bp = pushdown(data, 1))) {
    free_p(data);
    return;
  }
  bp->data[0] = (PID_NETROM | PID_FIRST | PID_LAST);
  if (send_ax(neighbor->crosslink, bp) == -1 && !neighbor->failed) {
    neighbor->failed = FAILED;
    calculate_routes();
  }
}

/*---------------------------------------------------------------------------*/

static void send_broadcast_packet(data)
struct mbuf *data;
{

  struct broadcast *p;
  struct mbuf *bp;

  for (p = broadcasts; p; p = p->next) {
    dup_p(&bp, data, 0, MAXINT16);
    ax_output(p->ifp, p->path, (char *) &mycall,
	      PID_NETROM | PID_FIRST | PID_LAST, bp);
  }
  free_p(data);
}

/*---------------------------------------------------------------------------*/

static void link_manager_initialize()
{
  netrom_server_axcb = open_ax(NULLCHAR, AX25_SERVER, ax25_recv_upcall, NULLVFP, ax25_state_upcall, NULLCHAR);
  netrom_server_axcb->mode = DGRAM;
}

/*---------------------------------------------------------------------------*/
/****************************** Routing Manager ******************************/
/*---------------------------------------------------------------------------*/

#define IDENTLEN   6

struct route {
  int  quality;
  int  obsolescence;
  struct neighbor *neighbor;
  struct route *prev, *next;
};

struct destination {
  struct ax25_addr call;
  char  ident[IDENTLEN];
  struct neighbor *neighbor;
  int  quality;
  int  force_broadcast;
  struct route *routes;
  struct destination *prev, *next;
};

struct routes_stat {
  int  rcvd;
  int  sent;
};

static char  myident[IDENTLEN] = { ' ', ' ', ' ', ' ', ' ', ' ' };
static struct destination *destinations;
static struct interface nr_interface;
static struct routes_stat routes_stat;
static struct timer broadcast_timer;
static struct timer obsolescence_timer;

/*---------------------------------------------------------------------------*/

int  isnetrom(call)
register struct ax25_addr *call;
{
  register struct destination *pd;

  if (ismycall(call)) return 1;
  for (pd = destinations; pd; pd = pd->next)
    if (addreq(call, &pd->call)) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static struct destination *destinationptr(call, create)
struct ax25_addr *call;
int  create;
{

  register int  i;
  register struct destination *pd;

  for (pd = destinations; pd && !addreq(call, &pd->call); pd = pd->next) ;
  if (!pd && create) {
    pd = (struct destination *) calloc(1, sizeof(struct destination ));
    addrcp(&pd->call, call);
    for (i = 0; i < IDENTLEN; i++) pd->ident[i] = ' ';
    if (destinations) {
      pd->next = destinations;
      destinations->prev = pd;
    }
    destinations = pd;
  }
  return pd;
}

/*---------------------------------------------------------------------------*/

static void calculate_routes()
{

  int  start_broadcast_timer;
  register int  best_quality;
  register struct destination *pd;
  register struct route *pr;
  struct neighbor *pn;

  start_broadcast_timer = 0;
  for (pd = destinations; pd; pd = pd->next) {
    pn = 0;
    best_quality = 0;
    for (pr = pd->routes; pr; pr = pr->next)
      if (pr->quality > best_quality && !pr->neighbor->failed) {
	pn = pr->neighbor;
	best_quality = pr->quality;
      }
    if (pn != pd->neighbor || best_quality != pd->quality) {
      pd->neighbor = pn;
      pd->quality = best_quality;
      pd->force_broadcast = 1;
    }
    if (pd->force_broadcast) start_broadcast_timer = 1;
  }
  if (start_broadcast_timer) {
    set_timer(&broadcast_timer, 10 * 1000l);
    start_timer(&broadcast_timer);
  }
}

/*---------------------------------------------------------------------------*/

static void delete_route(pd, pr)
register struct destination *pd;
register struct route *pr;
{
  register struct neighbor *pn;

  pn = pr->neighbor;
  pn->usecount--;
  if (!pn->permanent && !pn->usecount && !pn->crosslink) delete_neighbor(pn);
  if (pr->prev) pr->prev->next = pr->next;
  if (pr->next) pr->next->prev = pr->prev;
  if (pr == pd->routes) pd->routes = pr->next;
  free((char *) pr);
}

/*---------------------------------------------------------------------------*/

static void update_route(call, ident, neighbor, quality, obsolescence, force)
struct ax25_addr *call;
char  *ident;
struct neighbor *neighbor;
int  quality, obsolescence, force;
{

  register struct destination *pd;
  register struct route *pr;

  if (ismycall(call)) return;
  pd = destinationptr(call, 1);
  if (ident && *ident && *ident != ' ') memcpy(pd->ident, ident, IDENTLEN);
  for (pr = pd->routes; pr; pr = pr->next)
    if (pr->neighbor == neighbor) {
      if (pr->obsolescence || force) {
	pr->quality = quality;
	pr->obsolescence = obsolescence;
      }
      return;
    }
  pr = (struct route *) calloc(1, sizeof(struct route ));
  pr->quality = quality;
  pr->obsolescence = obsolescence;
  pr->neighbor = neighbor;
  pr->neighbor->usecount++;
  if (pd->routes) {
    pr->next = pd->routes;
    pd->routes->prev = pr;
  }
  pd->routes = pr;
}

/*---------------------------------------------------------------------------*/

static void delete_destination(pd)
register struct destination *pd;
{
  if (pd->prev) pd->prev->next = pd->next;
  if (pd->next) pd->next->prev = pd->prev;
  if (pd == destinations) destinations = pd->next;
  free((char *) pd);
}

/*---------------------------------------------------------------------------*/

int  new_neighbor(call)
struct ax25_addr *call;
{
  register struct neighbor *pn;

  if (!ismycall(call)) {
    pn = neighborptr(call, 1);
    pn->failed = 0;
    update_route(call, NULLCHAR, pn, pn->quality, nr_obsinit, 0);
    calculate_routes();
  }
}

/*---------------------------------------------------------------------------*/

int  nr_nodercv(fromcall, bp)
struct ax25_addr *fromcall;
struct mbuf *bp;
{

  char  buf[AXALEN+IDENTLEN+AXALEN+1];
  char  id;
  char  ident[IDENTLEN];
  int  quality;
  register struct neighbor *pn;
  struct destination *pd;

  routes_stat.rcvd++;
  if (ismycall(fromcall)) goto discard;
  if (pullup(&bp, &id, 1) != 1 || uchar(id) != 0xff) goto discard;
  if (pullup(&bp, ident, IDENTLEN) != IDENTLEN) goto discard;
  pn = neighborptr(fromcall, 1);
  pn->failed = 0;
  update_route(fromcall, ident, pn, pn->quality, nr_obsinit, 0);
  while (pullup(&bp, buf, sizeof(buf)) == sizeof(buf))
    if (!ismycall(axptr(buf))) {
      pd = destinationptr(axptr(buf), 1);
      quality = uchar(buf[AXALEN+IDENTLEN+AXALEN]);
      if (ismycall(axptr(buf + AXALEN + IDENTLEN))) {
	if (quality > pd->quality) pd->force_broadcast = 1;
	quality = 0;
      } else
	quality = (quality * pn->quality) / 256;
      update_route(axptr(buf), buf + AXALEN, pn, quality, nr_obsinit, 0);
    }
  calculate_routes();

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/

static struct mbuf *alloc_broadcast_packet()
{
  struct mbuf *bp;

  if (bp = alloc_mbuf(256)) {
    *bp->data = 0xff;
    memcpy(bp->data + 1, myident, IDENTLEN);
    bp->cnt = 1 + IDENTLEN;
  }
  return bp;
}

/*---------------------------------------------------------------------------*/

static void send_broadcast()
{

  register char  *p;
  struct destination *pd;
  struct mbuf *bp;

  set_timer(&broadcast_timer, nr_bdcstint * 1000l);
  start_timer(&broadcast_timer);
  bp = alloc_broadcast_packet();
  for (pd = destinations; pd; pd = pd->next)
    if (pd->quality || pd->force_broadcast) {
      pd->force_broadcast = 0;
      if (!bp) bp = alloc_broadcast_packet();
      p = bp->data + bp->cnt;
      addrcp(axptr(p), &pd->call);
      p += AXALEN;
      memcpy(p, pd->ident, IDENTLEN);
      p += IDENTLEN;
      addrcp(axptr(p), pd->neighbor ? &pd->neighbor->call : &mycall);
      p += AXALEN;
      *p++ = pd->quality;
      if ((bp->cnt = p - bp->data) > 256 - 21) {
	send_broadcast_packet(bp);
	routes_stat.sent++;
	bp = NULLBUF;
      }
    }
  if (bp) {
    send_broadcast_packet(bp);
    routes_stat.sent++;
  }
}

/*---------------------------------------------------------------------------*/

static void decrement_obsolescence_counters()
{

  struct destination *pd, *pdnext;
  struct route *pr, *prnext;

  set_timer(&obsolescence_timer, nr_bdcstint * 1000l);
  start_timer(&obsolescence_timer);

  for (pd = destinations; pd; pd = pd->next)
    for (pr = pd->routes; pr; pr = prnext) {
      prnext = pr->next;
      if (pr->obsolescence && --pr->obsolescence == 0) delete_route(pd, pr);
    }
  calculate_routes();
  for (pd = destinations; pd; pd = pdnext) {
    pdnext = pd->next;
    if (!pd->routes && !pd->force_broadcast) delete_destination(pd);
  }
}

/*---------------------------------------------------------------------------*/

static void route_packet(bp, fromneighbor)
struct mbuf *bp;
struct neighbor *fromneighbor;
{

  int  ttl;
  register struct destination *pd;
  register struct route *pr;
  void circuit_manager();

  if (!bp || bp->cnt < 15) goto discard;

  if (fromneighbor) {
    if (ismycall(axptr(bp->data))) goto discard;  /* ROUTING ERROR */
    pd = destinationptr(axptr(bp->data), 1);
    for (pr = pd->routes; pr && pr->neighbor != fromneighbor; pr = pr->next) ;
    if (!pr) {
      pr = (struct route *) calloc(1, sizeof(struct route ));
      if (neighborptr(&pd->call, 0) == fromneighbor)
	pr->quality = fromneighbor->quality;
      else
	pr->quality = 1;
      pr->obsolescence = nr_obsinit;
      pr->neighbor = fromneighbor;
      pr->neighbor->usecount++;
      if (pd->routes) {
	pr->next = pd->routes;
	pd->routes->prev = pr;
      }
      pd->routes = pr;
      calculate_routes();
    } else if (pr->obsolescence)
      pr->obsolescence = nr_obsinit;
  }

  if (ismycall(axptr(bp->data + AXALEN))) {
    if (bp->cnt >= 40                 &&
	uchar(bp->data[19]) == 0      &&
	uchar(bp->data[15]) == PID_IP &&
	uchar(bp->data[16]) == PID_IP) {

      int32 src_ipaddr;
      struct arp_tab *arp, *arp_add();
      struct ax25_addr hw_addr;

      src_ipaddr = (uchar(bp->data[32]) << 24) |
		   (uchar(bp->data[33]) << 16) |
		   (uchar(bp->data[34]) <<  8) |
		    uchar(bp->data[35]);
      rt_add(src_ipaddr, 32, 0, 0, &nr_interface);
      addrcp(&hw_addr, axptr(bp->data));
      hw_addr.ssid |= E;
      if (arp = arp_add(src_ipaddr, ARP_NETROM, (char *) &hw_addr, AXALEN, 0)) {
	stop_timer(&arp->timer);
	arp->timer.count = arp->timer.start = 0;
      }
      pullup(&bp, NULLCHAR, 20);
      ip_route(bp, 0);
      return;
    }
    pullup(&bp, NULLCHAR, 15);
    if (!bp) return;
    if (!fromneighbor) {
      struct mbuf *hbp = copy_p(bp, len_mbuf(bp));
      free_p(bp);
      bp = hbp;
    }
    circuit_manager(bp);
    return;
  }

  ttl = uchar(bp->data[2*AXALEN]);
  if (--ttl <= 0) goto discard;
  bp->data[2*AXALEN] = ttl;

  pd = destinationptr(axptr(bp->data + AXALEN), 1);
  if (!pd->neighbor) {
    pd->force_broadcast = 1;
    send_broadcast();
    goto discard;
  }

  if (pd->neighbor == fromneighbor ||
      addreq(&pd->neighbor->call, axptr(bp->data))) send_broadcast();

  send_packet_to_neighbor(bp, pd->neighbor);
  return;

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/

static void send_l3_packet(source, dest, ttl, data)
struct ax25_addr *source, *dest;
int  ttl;
struct mbuf *data;
{
  struct mbuf *bp;

  if (!(bp = pushdown(data, 2 * AXALEN + 1))) {
    free_p(data);
    return;
  }
  addrcp(axptr(bp->data), source);
  addrcp(axptr(bp->data + AXALEN), dest);
  bp->data[2*AXALEN] = ttl;
  route_packet(bp, (struct neighbor *) 0);
}

/*---------------------------------------------------------------------------*/

static int  nr_send(bp, interface, gateway, precedence, delay, throughput, reliability)
struct mbuf *bp;
struct interface *interface;
int32 gateway;
char  precedence;
char  delay;
char  throughput;
char  reliability;
{

  struct arp_tab *arp;
  struct mbuf *nbp;

  if (!(arp = arp_lookup(ARP_NETROM, gateway))) {
    free_p(bp);
    return;
  }
  if (!(nbp = pushdown(bp, 5))) {
    free_p(bp);
    return;
  }
  bp = nbp;
  bp->data[0] = PID_IP;
  bp->data[1] = PID_IP;
  bp->data[2] = 0;
  bp->data[3] = 0;
  bp->data[4] = 0;
  send_l3_packet(&mycall, axptr(arp->hw_addr), nr_ttlinit, bp);
}

/*---------------------------------------------------------------------------*/

static void routing_manager_initialize()
{
  int  psax25(), setpath();

  nr_interface.name = "netrom";
  nr_interface.mtu = 236;
  nr_interface.send = nr_send;
  nr_interface.next = ifaces;
  ifaces = &nr_interface;

  arp_init(ARP_NETROM, AXALEN, 0, 0, NULLCHAR, psax25, setpath);

  broadcast_timer.func = send_broadcast;
  broadcast_timer.arg = NULLCHAR;
  set_timer(&broadcast_timer, nr_bdcstint * 1000l);
  start_timer(&broadcast_timer);

  obsolescence_timer.func = decrement_obsolescence_counters;
  obsolescence_timer.arg = NULLCHAR;
  set_timer(&obsolescence_timer, nr_bdcstint * 1000l);
  start_timer(&obsolescence_timer);

}

/*---------------------------------------------------------------------------*/
/****************************** Circuit Manager ******************************/
/*---------------------------------------------------------------------------*/

#include "netrom.h"

static char  *nrreasons[] = {
  "Normal",
  "Reset",
  "Timeout",
  "Network"
};

static int  nextid, server_enabled;
static struct circuit *circuits;

/*---------------------------------------------------------------------------*/

static char  *print_address(pc)
register struct circuit *pc;
{

  register char  *p;
  static char  buf[128];

  pax25(buf, &pc->orguser);
  for (p = buf; *p; p++) ;
  *p++ = ' ';
  if (pc->outbound) {
    *p++ = '-';
    *p++ = '>';
    *p++ = ' ';
    *p++ = '@';
    pax25(p, &pc->node);
  } else {
    *p++ = '@';
    pax25(p, &pc->orgnode);
  }
  return buf;
}

/*---------------------------------------------------------------------------*/

static void send_l4_packet(pc, opcode, data)
struct circuit *pc;
int  opcode;
struct mbuf *data;
{

  int  start_t1_timer = 0;
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
    addrcp(axptr(bp->data + 6), &pc->orguser);
    addrcp(axptr(bp->data + 13), &pc->orgnode);
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
    stop_timer(&pc->timer_t2);
    bp->data[0] = pc->remoteindex;
    bp->data[1] = pc->remoteid;
    bp->data[2] = pc->send_state;
    bp->data[3] = pc->recv_state;
    bp->data[4] = opcode;
    if ((opcode & NR4OPCODE) == NR4OPINFO)
      pc->send_state = uchar(pc->send_state + 1);
    break;
  }
  if (start_t1_timer) start_timer(&pc->timer_t1);
  send_l3_packet(&mycall, &pc->node, nr_ttlinit, bp);
}

/*---------------------------------------------------------------------------*/

static void try_send(pc, fill_sndq)
struct circuit *pc;
int  fill_sndq;
{

  int  cnt;
  struct mbuf *bp;

  while (pc->unack < pc->window) {
    if (pc->state != CONNECTED || pc->remote_busy) return;
    if (fill_sndq && pc->t_upcall && !pc->closed) {
      cnt = (pc->window - pc->unack) * NR4MAXINFO - len_mbuf(pc->sndq);
      if (cnt > 0) (*pc->t_upcall)(pc, cnt);
    }
    cnt = len_mbuf(pc->sndq);
    if (!cnt || pc->unack && cnt < NR4MAXINFO) return;
    if (cnt > NR4MAXINFO) cnt = NR4MAXINFO;
    if (!(bp = alloc_mbuf(cnt))) return;
    pullup(&pc->sndq, bp->data, bp->cnt = cnt);
    enqueue(&pc->resndq, bp);
    pc->unack++;
    pc->sndtime[pc->send_state] = currtime;
    dup_p(&bp, bp, 0, cnt);
    send_l4_packet(pc, NR4OPINFO, bp);
  }
}

/*---------------------------------------------------------------------------*/

static void set_circuit_state(pc, newstate)
struct circuit *pc;
int  newstate;
{
  int  oldstate;

  oldstate = pc->state;
  pc->state = newstate;
  pc->retry = 0;
  stop_timer(&pc->timer_t1);
  stop_timer(&pc->timer_t2);
  stop_timer(&pc->timer_t4);
  switch (newstate) {
  case DISCONNECTED:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    break;
  case CONNECTING:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    send_l4_packet(pc, NR4OPCONRQ, NULLBUF);
    break;
  case CONNECTED:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    try_send(pc, 1);
    break;
  case DISCONNECTING:
    if (pc->s_upcall) (*pc->s_upcall)(pc, oldstate, newstate);
    send_l4_packet(pc, NR4OPDISRQ, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t1_timeout(pc)
struct circuit *pc;
{
  struct mbuf *bp, *qp;

  if (++pc->retry > nr_tretry) pc->reason = TIMEOUT;
  switch (pc->state) {
  case DISCONNECTED:
    break;
  case CONNECTING:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, DISCONNECTED);
    else
      send_l4_packet(pc, NR4OPCONRQ, NULLBUF);
    break;
  case CONNECTED:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, DISCONNECTING);
    else {
      pc->send_state = uchar(pc->send_state - pc->unack);
      for (qp = pc->resndq; qp; qp = qp->anext) {
	pc->sndtime[pc->send_state] = 0;
	dup_p(&bp, qp, 0, NR4MAXINFO);
	send_l4_packet(pc, NR4OPINFO, bp);
      }
    }
    break;
  case DISCONNECTING:
    if (pc->retry > nr_tretry)
      set_circuit_state(pc, DISCONNECTED);
    else
      send_l4_packet(pc, NR4OPDISRQ, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void l4_t2_timeout(pc)
struct circuit *pc;
{
  send_l4_packet(pc, NR4OPACK, NULLBUF);
}

/*---------------------------------------------------------------------------*/

static void l4_t3_timeout(pc)
struct circuit *pc;
{
  close_nr(pc);
}

/*---------------------------------------------------------------------------*/

static void l4_t4_timeout(pc)
struct circuit *pc;
{
  pc->remote_busy = 0;
  if (pc->unack) start_timer(&pc->timer_t1);
  try_send(pc, 1);
}

/*---------------------------------------------------------------------------*/

static void circuit_manager(bp)
struct mbuf *bp;
{

  int  nakrcvd;
  register struct circuit *pc;
  void nrserv_recv_upcall(), nrserv_state_upcall();

  if (!bp || bp->cnt < 5) goto discard;

  if ((bp->data[4] & NR4OPCODE) == NR4OPCONRQ) {
    if (bp->cnt != 20) goto discard;
    for (pc = circuits; pc; pc = pc->next)
      if (pc->remoteindex == uchar(bp->data[0]) &&
	  pc->remoteid == uchar(bp->data[1]) &&
	  addreq(&pc->node, axptr(bp->data + 13)) &&
	  addreq(&pc->orguser, axptr(bp->data + 6)) &&
	  addreq(&pc->orgnode, axptr(bp->data + 13))) break;
    if (!pc) {
      pc = (struct circuit *) calloc(1, sizeof(struct circuit ));
      nextid++;
      pc->localindex = uchar(nextid >> 8);
      pc->localid = uchar(nextid);
      pc->remoteindex = uchar(bp->data[0]);
      pc->remoteid = uchar(bp->data[1]);
      addrcp(&pc->node, axptr(bp->data + 13));
      addrcp(&pc->orguser, axptr(bp->data + 6));
      addrcp(&pc->orgnode, axptr(bp->data + 13));
      set_timer(&pc->timer_t1, nr_ttimeout * 1000l);
      pc->timer_t1.func = l4_t1_timeout;
      pc->timer_t1.arg = (char *) pc;
      pc->timer_t2.func = l4_t2_timeout;
      pc->timer_t2.arg = (char *) pc;
      pc->timer_t3.func = l4_t3_timeout;
      pc->timer_t3.arg = (char *) pc;
      pc->timer_t4.func = l4_t4_timeout;
      pc->timer_t4.arg = (char *) pc;
      pc->r_upcall = nrserv_recv_upcall;
      pc->s_upcall = nrserv_state_upcall;
      pc->next = circuits;
      circuits = pc;
    }
  } else
    for (pc = circuits; ; pc = pc->next) {
      if (!pc) goto discard;
      if (pc->localindex == uchar(bp->data[0]) &&
	  pc->localid == uchar(bp->data[1])) break;
    }

  set_timer(&pc->timer_t3, nr_timeout * 1000l);
  start_timer(&pc->timer_t3);

  switch (bp->data[4] & NR4OPCODE) {

  case NR4OPCONRQ:
    switch (pc->state) {
    case DISCONNECTED:
      pc->window = uchar(bp->data[5]);
      if (pc->window > nr_twindow) pc->window = nr_twindow;
      if (pc->window < 1) pc->window = 1;
      if (server_enabled) {
	send_l4_packet(pc, NR4OPCONAK, NULLBUF);
	set_circuit_state(pc, CONNECTED);
      } else {
	send_l4_packet(pc, NR4OPCONAK | NR4CHOKE, NULLBUF);
	del_nr(pc);
      }
      break;
    case CONNECTED:
      send_l4_packet(pc, NR4OPCONAK, NULLBUF);
      break;
    default:
      goto discard;
    }
    break;

  case NR4OPCONAK:
    if (pc->state != CONNECTING) goto discard;
    pc->remoteindex = uchar(bp->data[2]);
    pc->remoteid = uchar(bp->data[3]);
    if (pc->window > uchar(bp->data[5])) pc->window = uchar(bp->data[5]);
    if (pc->window < 1) pc->window = 1;
    if (bp->data[4] & NR4CHOKE) {
      pc->reason = RESET;
      set_circuit_state(pc, DISCONNECTED);
    } else
      set_circuit_state(pc, CONNECTED);
    break;

  case NR4OPDISRQ:
    send_l4_packet(pc, NR4OPDISAK, NULLBUF);
    set_circuit_state(pc, DISCONNECTED);
    break;

  case NR4OPDISAK:
    if (pc->state != DISCONNECTING) goto discard;
    set_circuit_state(pc, DISCONNECTED);
    break;

  case NR4OPINFO:
  case NR4OPACK:
    if (pc->state != CONNECTED) goto discard;
    if (pc->remote_busy = (bp->data[4] & NR4CHOKE)) {
      stop_timer(&pc->timer_t1);
      set_timer(&pc->timer_t4, nr_tbsydelay * 1000l);
      start_timer(&pc->timer_t4);
    } else
      stop_timer(&pc->timer_t4);
    if (uchar(pc->send_state - bp->data[3]) < pc->unack) {
      pc->retry = 0;
      stop_timer(&pc->timer_t1);
      if (pc->sndtime[uchar(bp->data[3]-1)]) {
	int32 rtt, abserr;
	rtt = (currtime - pc->sndtime[uchar(bp->data[3]-1)]) * 1000l;
	if (pc->srtt || pc->mdev) {
	  pc->srtt = ((AGAIN - 1) * pc->srtt + rtt) / AGAIN;
	  abserr = (rtt > pc->srtt) ? rtt - pc->srtt : pc->srtt - rtt;
	  pc->mdev = ((DGAIN - 1) * pc->mdev + abserr) / DGAIN;
	} else
	  pc->srtt = pc->mdev = rtt;
	pc->timer_t1.start = (pc->srtt + 2 * pc->mdev + MSPTICK) / MSPTICK;
	if (pc->timer_t1.start < 60) pc->timer_t1.start = 60;
      }
      while (uchar(pc->send_state - bp->data[3]) < pc->unack) {
	pc->resndq = free_p(pc->resndq);
	pc->unack--;
      }
      if (pc->unack && !pc->remote_busy) start_timer(&pc->timer_t1);
    }
    nakrcvd = bp->data[4] & NR4NAK;
    if ((bp->data[4] & NR4OPCODE) == NR4OPINFO) {
      set_timer(&pc->timer_t2, nr_tackdelay * 1000l);
      start_timer(&pc->timer_t2);
      if (uchar(bp->data[2] - pc->recv_state) < pc->window) {
	struct mbuf *curr, *prev;
	for (curr = pc->reseq; curr && curr->data[2] != bp->data[2]; curr = curr->anext) ;
	if (!curr) {
	  int  found = 0;
	  bp->anext = pc->reseq;
	  pc->reseq = bp;
	  bp = NULLBUF;
search_again:
	  for (prev = 0, curr = pc->reseq; curr; prev = curr, curr = curr->anext)
	    if (uchar(curr->data[2]) == pc->recv_state) {
	      if (prev)
		prev->anext = curr->anext;
	      else
		pc->reseq = curr->anext;
	      curr->anext = NULLBUF;
	      pullup(&curr, NULLCHAR, 5);
	      if (curr) {
		pc->rcvcnt += len_mbuf(curr);
		append(&pc->rcvq, curr);
	      }
	      pc->recv_state = uchar(pc->recv_state + 1);
	      pc->naksent = 0;
	      found = 1;
	      goto search_again;
	    }
	  if (found && pc->r_upcall) (*pc->r_upcall)(pc, pc->rcvcnt);
	}
      }
    }
    if (nakrcvd && pc->unack) {
      int  old_send_state;
      struct mbuf *bp1;
      old_send_state = pc->send_state;
      pc->send_state = uchar(pc->send_state - pc->unack);
      pc->sndtime[pc->send_state] = 0;
      dup_p(&bp1, pc->resndq, 0, NR4MAXINFO);
      send_l4_packet(pc, NR4OPINFO, bp1);
      pc->send_state = old_send_state;
    }
    try_send(pc, 1);
    if (pc->closed && !pc->sndq && !pc->unack)
      set_circuit_state(pc, DISCONNECTING);
    break;
  }

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/
/********************************* User Calls ********************************/
/*---------------------------------------------------------------------------*/

struct circuit *open_nr(node, orguser, window, r_upcall, t_upcall, s_upcall, user)
struct ax25_addr *node, *orguser;
int  window;
void (*r_upcall)();
void (*t_upcall)();
void (*s_upcall)();
char  *user;
{
  struct circuit *pc;

  if (!isnetrom(node)) {
    net_error = INVALID;
    return 0;
  }
  if (!orguser) orguser = &mycall;
  if (!window) window = nr_twindow;
  if (!(pc = (struct circuit *) calloc(1, sizeof(struct circuit )))) {
    net_error = NO_SPACE;
    return 0;
  }
  nextid++;
  pc->localindex = uchar(nextid >> 8);
  pc->localid = uchar(nextid);
  pc->remoteindex = -1;
  pc->outbound = 1;
  addrcp(&pc->node, node);
  addrcp(&pc->orguser, orguser);
  addrcp(&pc->orgnode, &mycall);
  pc->window = window;
  set_timer(&pc->timer_t1, nr_ttimeout * 1000l);
  pc->timer_t1.func = l4_t1_timeout;
  pc->timer_t1.arg = (char *) pc;
  pc->timer_t2.func = l4_t2_timeout;
  pc->timer_t2.arg = (char *) pc;
  pc->timer_t3.func = l4_t3_timeout;
  pc->timer_t3.arg = (char *) pc;
  pc->timer_t4.func = l4_t4_timeout;
  pc->timer_t4.arg = (char *) pc;
  pc->r_upcall = r_upcall;
  pc->t_upcall = t_upcall;
  pc->s_upcall = s_upcall;
  pc->user = user;
  pc->next = circuits;
  circuits = pc;
  set_circuit_state(pc, CONNECTING);
  return pc;
}

/*---------------------------------------------------------------------------*/

int  send_nr(pc, bp)
struct circuit *pc;
struct mbuf *bp;
{
  int16 cnt;

  if (!(pc && bp)) {
    free_p(bp);
    net_error = INVALID;
    return (-1);
  }
  switch (pc->state) {
  case DISCONNECTED:
    free_p(bp);
    net_error = NO_CONN;
    return (-1);
  case CONNECTING:
  case CONNECTED:
    if (!pc->closed) {
      cnt = len_mbuf(bp);
      append(&pc->sndq, bp);
      try_send(pc, 0);
      return cnt;
    }
  case DISCONNECTING:
    free_p(bp);
    net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  space_nr(pc)
struct circuit *pc;
{
  if (!pc) {
    net_error = INVALID;
    return (-1);
  }
  return (pc->window - pc->unack) * NR4MAXINFO - len_mbuf(pc->sndq);
}

/*---------------------------------------------------------------------------*/

int  recv_nr(pc, bpp, cnt)
struct circuit *pc;
struct mbuf **bpp;
int16 cnt;
{
  if (!(pc && bpp)) {
    net_error = INVALID;
    return (-1);
  }
  if (pc->rcvcnt) {
    if (!cnt || pc->rcvcnt <= cnt) {
      *bpp = dequeue(&pc->rcvq);
      cnt = len_mbuf(*bpp);
    } else {
      if (!(*bpp = alloc_mbuf(cnt))) {
	net_error = NO_SPACE;
	return (-1);
      }
      pullup(&pc->rcvq, (*bpp)->data, cnt);
      (*bpp)->cnt = cnt;
    }
    pc->rcvcnt -= cnt;
    return cnt;
  }
  switch (pc->state) {
  case CONNECTING:
  case CONNECTED:
    *bpp = NULLBUF;
    net_error = WOULDBLK;
    return (-1);
  case DISCONNECTED:
  case DISCONNECTING:
    *bpp = NULLBUF;
    return 0;
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  close_nr(pc)
struct circuit *pc;
{
  if (!pc) {
    net_error = INVALID;
    return (-1);
  }
  if (pc->closed) {
    net_error = CON_CLOS;
    return (-1);
  }
  pc->closed = 1;
  switch (pc->state) {
  case DISCONNECTED:
    net_error = NO_CONN;
    return (-1);
  case CONNECTING:
    set_circuit_state(pc, DISCONNECTED);
    return 0;
  case CONNECTED:
    if (!pc->sndq && !pc->unack) set_circuit_state(pc, DISCONNECTING);
    return 0;
  case DISCONNECTING:
    net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  reset_nr(pc)
struct circuit *pc;
{
  if (!pc) {
    net_error = INVALID;
    return (-1);
  }
  pc->reason = RESET;
  set_circuit_state(pc, DISCONNECTED);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  del_nr(pc)
struct circuit *pc;
{
  register struct circuit *p, *q;

  for (q = 0, p = circuits; p != pc; q = p, p = p->next)
    if (!p) {
      net_error = INVALID;
      return (-1);
    }
  if (q)
    q->next = p->next;
  else
    circuits = p->next;
  stop_timer(&pc->timer_t1);
  stop_timer(&pc->timer_t2);
  stop_timer(&pc->timer_t3);
  stop_timer(&pc->timer_t4);
  free_q(&pc->reseq);
  free_q(&pc->rcvq);
  free_q(&pc->sndq);
  free_q(&pc->resndq);
  free((char *) pc);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  valid_nr(pc)
register struct circuit *pc;
{
  register struct circuit *p;

  if (!pc) return 0;
  for (p = circuits; p; p = p->next)
    if (p == pc) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/
/******************************** Login Server *******************************/
/*---------------------------------------------------------------------------*/

#include "login.h"

/*---------------------------------------------------------------------------*/

static void nrserv_poll(pc)
struct circuit *pc;
{

  char  *p;
  int  c;
  int  cnt;
  struct login_cb *tp;
  struct mbuf *bp;

  tp = (struct login_cb *) pc->user;
  start_timer(&tp->poll_timer);
  if ((cnt = space_nr(pc)) <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (p - bp->data < cnt && (c = login_read(tp)) != -1)
    if (c != '\n') *p++ = c;
  if (bp->cnt = p - bp->data)
    send_nr(pc, bp);
  else {
    free_p(bp);
    if (login_dead(tp)) {
      stop_timer(&tp->poll_timer);
      close_nr(pc);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void nrserv_recv_upcall(pc, cnt)
struct circuit *pc;
int16 cnt;
{

  char  c;
  struct login_cb *tp;
  struct mbuf *bp;

  tp = (struct login_cb *) pc->user;
  recv_nr(pc, &bp, 0);
  while (pullup(&bp, &c, 1) == 1) login_write(tp, c);
}

/*---------------------------------------------------------------------------*/

static void nrserv_state_upcall(pc, oldstate, newstate)
struct circuit *pc;
int  oldstate, newstate;
{
  switch (newstate) {
  case CONNECTED:
    pc->user = (char *) login_open(print_address(pc), "NETROM", nrserv_poll, (char *) pc);
    if (!pc->user) close_nr(pc);
    break;
  case DISCONNECTED:
    login_close((struct login_cb *) pc->user);
    del_nr(pc);
    break;
  }
}

/*---------------------------------------------------------------------------*/
/*********************************** Client **********************************/
/*---------------------------------------------------------------------------*/

/* #include <memory.h> */
/* #include <stdio.h> */

/* #include "global.h" */
/* #include "netuser.h" */
/* #include "mbuf.h" */
/* #include "timer.h" */
/* #include "ax25.h" */
/* #include "axproto.h" */
#include "session.h"

/*---------------------------------------------------------------------------*/

static int  nrclient_parse(buf, n)
char  *buf;
int16 n;
{
  if (!(current && current->type == NRSESSION && current->cb.netrom)) return (-1);
  if (buf[n-1] == '\n') n--;
  if (n <= 0) return (-1);
  return send_nr(current->cb.netrom, qdata(buf, n));
}

/*---------------------------------------------------------------------------*/

void nrclient_recv_upcall(pc, cnt)
struct circuit *pc;
int16 cnt;
{

  char  c;
  struct mbuf *bp;

  if (!(mode == CONV_MODE && current && current->type == NRSESSION && current->cb.netrom == pc)) return;
  recv_nr(pc, &bp, 0);
  while (pullup(&bp, &c, 1) == 1) putchar((c == '\r') ? '\n' : c);
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void nrclient_state_upcall(pc, oldstate, newstate)
struct circuit *pc;
int  oldstate, newstate;
{
  int  notify;

  notify = (current && current->type == NRSESSION && current == (struct session *) pc->user);
  if (newstate != DISCONNECTED) {
    if (notify) printf("%s\n", ax25states[newstate]);
  } else {
    if (notify) printf("%s (%s)\n", ax25states[newstate], nrreasons[pc->reason]);
    if (pc->user) freesession((struct session *) pc->user);
    del_nr(pc);
    if (notify) cmdmode();
  }
}

/*---------------------------------------------------------------------------*/

print_netrom_session(s)
struct session *s;
{
  printf("%c%-3d%8lx NETROM  %4d  %-13s%-s",
	 (current == s) ? '*' : ' ',
	 (int) (s - sessions),
	 (long) s->cb.netrom,
	 s->cb.netrom->rcvcnt,
	 ax25states[s->cb.netrom->state],
	 print_address(s->cb.netrom));
}

/*---------------------------------------------------------------------------*/

static int  doconnect(argc, argv)
int  argc;
char  *argv[];
{

  struct ax25_addr node, user;
  struct session *s;

  if (setcall(&node, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (!isnetrom(&node)) {
    printf("Unknown node \"%s\"\n", argv[1]);
    return 1;
  }
  if (argc < 3)
    addrcp(&user, &mycall);
  else if (setcall(&user, argv[2])) {
    printf("Invalid call \"%s\"\n", argv[2]);
    return 1;
  }
  if (!(s = newsession())) {
    printf("Too many sessions\n");
    return 1;
  }
  current = s;
  s->type = NRSESSION;
  s->name = NULLCHAR;
  s->cb.netrom = 0;
  s->parse = nrclient_parse;
  if (!(s->cb.netrom = open_nr(&node, &user, 0, nrclient_recv_upcall, NULLVFP, nrclient_state_upcall, (char *) s))) {
    freesession(s);
    switch (net_error) {
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
    case NO_SPACE:
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
  go();
  return 0;
}

/*---------------------------------------------------------------------------*/
/****************************** NETROM Commands ******************************/
/*---------------------------------------------------------------------------*/

static int  dobroadcast(argc, argv)
int  argc;
char  *argv[];
{

  char  buf[1024];
  struct broadcast *p;
  struct interface *if_lookup();
  struct interface *ifp;

  if (argc < 3) {
    puts("Interface  Path");
    for (p = broadcasts; p; p = p->next) {
      psax25(buf, p->path);
      printf("%-9s  %s\n", p->ifp->name, buf);
    }
    return 0;
  }
  if (!(ifp = if_lookup(argv[1]))) {
    printf("Interface \"%s\" unknown\n", argv[1]);
    return 1;
  }
  if (ifp->output != ax_output) {
    printf("Interface \"%s\" not kiss\n", argv[1]);
    return 1;
  }
  p = (struct broadcast *) malloc(sizeof(struct broadcast ));
  p->ifp = ifp;
  p->path = malloc((unsigned) ((argc - 2) * AXALEN));
  setpath(p->path, argv + 2, argc - 2);
  p->next = broadcasts;
  broadcasts = p;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doident(argc, argv)
int  argc;
char  *argv[];
{

  char  *cp;
  int  i;

  if (argc < 2)
    printf("Ident %-6.6s\n", myident);
  else {
    for (cp = argv[1], i = 0; i < IDENTLEN; i++)
      myident[i] = *cp ? toupper(*cp++) : ' ';
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static char  *print_destname(pd)
struct destination *pd;
{

  char  *p;
  int  i;
  static char  buf[20];

  p = buf;
  if (*pd->ident != ' ') {
    for (i = 0; i < IDENTLEN && pd->ident[i] != ' '; i++) *p++ = pd->ident[i];
    *p++ = ':';
  }
  pax25(p, &pd->call);
  return buf;
}

/*---------------------------------------------------------------------------*/

static int  donodes(argc, argv)
int  argc;
char  *argv[];
{

  char  *cp;
  char  buf[10];
  char  ident[IDENTLEN];
  int  i, quality, obsolescence;
  struct ax25_addr destination, neighbor;
  struct destination *pd;
  struct neighbor *pn;
  struct route *pr;

  if (argc < 2) {
    printf("Nodes:\n");
    for (i = 0, pd = destinations; pd; pd = pd->next)
      if (pd->quality) {
	printf((i % 4) < 3 ? "%-19s" : "%s\n", print_destname(pd));
	i++;
      }
    if (i % 4) putchar('\n');
    return 0;
  }

  memset(ident, ' ', IDENTLEN);
  cp = argv[1];
  if (strchr(cp, ':')) {
    for (i = 0; *cp != ':'; cp++)
      if (i < IDENTLEN) ident[i++] = toupper(*cp);
    cp++;
  }
  if (setcall(&destination, cp)) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }

  if (argc == 2) {
    if (!(pd = destinationptr(&destination, 0))) {
      printf("Unknown node \"%s\"\n", argv[1]);
      return 1;
    }
    printf("Routes to: %s\n", print_destname(pd));
    for (pr = pd->routes; pr; pr = pr->next) {
      pax25(buf, &pr->neighbor->call);
      printf("%c %3d  %3d  %-9s  ",
	     (pr->neighbor == pd->neighbor) ? '>' : ' ',
	     pr->quality,
	     pr->obsolescence,
	     buf);
      if (pr->neighbor->failed) printf("Failed ");
      if (pr->neighbor->crosslink)
	printf("%s", ax25states[pr->neighbor->crosslink->state]);
      putchar('\n');
    }
    return 0;
  }

  if (!strcmp(argv[2], "-")) {
    if (!(pd = destinationptr(&destination, 0))) {
      printf("Unknown node \"%s\"\n", argv[1]);
      return 1;
    }
    if (argc == 3) {
      while (pd->routes) delete_route(pd, pd->routes);
      calculate_routes();
      if (!pd->force_broadcast) delete_destination(pd);
      return 0;
    }
    if (setcall(&neighbor, argv[3])) {
      printf("Invalid call \"%s\"\n", argv[3]);
      return 1;
    }
    pn = neighborptr(&neighbor, 0);
    for (pr = pd->routes; pr; pr = pr->next)
      if (pr->neighbor == pn) {
	delete_route(pd, pr);
	calculate_routes();
	if (!pd->routes && !pd->force_broadcast) delete_destination(pd);
	return 0;
      }
    printf("No such route\n");
    return 1;
  }

  if (strcmp(argv[2], "+") || argc < 4) return (-1);
  if (setcall(&neighbor, argv[3])) {
    printf("Invalid call \"%s\"\n", argv[3]);
    return 1;
  }
  pn = neighborptr(&neighbor, 1);

  if (argc >= 5)
    quality = atoi(argv[4]);
  else
    quality = addreq(&destination, &pn->call) ? pn->quality : nr_hfqual;
  if (quality < 0 || quality > 255) {
    printf("Quality must be 0..255\n");
    return 1;
  }

  obsolescence = (argc >= 6) ? atoi(argv[5]) : nr_obsinit;
  if (obsolescence < 0 || obsolescence > 255) {
    printf("Obsolescence must be 0..255\n");
    return 1;
  }
  update_route(&destination, ident, pn, quality, obsolescence, 1);
  calculate_routes();
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doparms(argc, argv)
int  argc;
char  *argv[];
{
  int  i, j;

  switch (argc) {
  case 0:
  case 1:
    for (i = 1; i <= NPARMS; i++)
      printf("%s %5d\n", parms[i].text, *parms[i].valptr);
    return 0;
  case 2:
  case 3:
    i = atoi(argv[1]);
    if (i < 1 || i > NPARMS) {
      printf("parameter # must be 1..%d\n", NPARMS);
      return 1;
    }
    if (argc == 2) {
      printf("%s %5d\n", parms[i].text, *parms[i].valptr);
      return 0;
    }
    j = atoi(argv[2]);
    if (j < parms[i].minval || j > parms[i].maxval) {
      printf("parameter %d must be %d..%d\n", i, parms[i].minval, parms[i].maxval);
      return 1;
    }
    *parms[i].valptr = j;
    if (broadcast_timer.start != nr_bdcstint * 1000l / MSPTICK) {
      set_timer(&broadcast_timer, nr_bdcstint * 1000l);
      start_timer(&broadcast_timer);
    }
    if (obsolescence_timer.start != nr_bdcstint * 1000l / MSPTICK) {
      set_timer(&obsolescence_timer, nr_bdcstint * 1000l);
      start_timer(&obsolescence_timer);
    }
    return 0;
  default:
    printf("Usage: netrom parms [<parm#> [<parm value>]]\n");
    return 1;
  }
}

/*---------------------------------------------------------------------------*/

static int  doroutes(argc, argv)
int  argc;
char  *argv[];
{

  char  buf[10];
  int  permanent, quality;
  struct ax25_addr call;
  struct neighbor *pn;

  if (argc < 2) {
    printf("Routes:\n");
    for (pn = neighbors; pn; pn = pn->next) {
      pax25(buf, &pn->call);
      printf("%c %-9s %3d %3d %c ",
	     pn->crosslink ? '>' : ' ',
	     buf,
	     pn->quality,
	     pn->usecount,
	     pn->permanent ? '!' : ' ');
      if (pn->failed) printf("Failed ");
      if (pn->crosslink) printf("%s", ax25states[pn->crosslink->state]);
      putchar('\n');
    }
    return 0;
  }
  if (setcall(&call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }
  if (argc < 3) return (-1);
  if (!strcmp(argv[2], "+"))
    permanent = 1;
  else if (!strcmp(argv[2], "-"))
    permanent = 0;
  else
    return (-1);
  pn = neighborptr(&call, 1);
  pn->permanent = permanent;
  if (argc >= 4) {
    quality = atoi(argv[3]);
    if (quality < 0 || quality > 255) {
      printf("Quality must be 0..255\n");
      return 1;
    }
    pn->quality = quality;
  }
  if (!pn->permanent && !pn->usecount && !pn->crosslink) delete_neighbor(pn);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doreset(argc, argv)
int  argc;
char  *argv[];
{
  extern char  notval[];
  long  htol();
  struct circuit *pc;

  pc = (struct circuit *) htol(argv[1]);
  if (!valid_nr(pc)) {
    printf(notval);
    return 1;
  }
  reset_nr(pc);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dostatus(argc, argv)
int  argc;
char  *argv[];
{

  int  i;
  register struct circuit *pc;
  struct mbuf *bp;

  if (argc < 2) {
    printf("bdcsts rcvd %d bdcsts sent %d\n", routes_stat.rcvd, routes_stat.sent);
    printf("   &NRCB Rcv-Q Unack  Rt  Srtt  State          Remote socket\n");
    for (pc = circuits; pc; pc = pc->next)
      printf("%8lx %5u%c%5u%c %2d %5.1f  %-13s  %s\n",
	     (long) pc,
	     pc->rcvcnt,
	     /*** busy(pc) ? '*' :  ***/  ' ',
	     pc->unack,
	     pc->remote_busy ? '*' : ' ',
	     pc->retry,
	     pc->srtt / 1000.0,
	     ax25states[pc->state],
	     print_address(pc));
    if (server_enabled)
      printf("                                Listen (S)     *\n");
  } else {
    pc = (struct circuit *) htol(argv[1]);
    if (!valid_nr(pc)) {
      printf("Not a valid control block address\n");
      return 1;
    }
    printf("Address:      %s\n", print_address(pc));
    printf("Remote id:    %d/%d\n", pc->remoteindex, pc->remoteid);
    printf("Local id:     %d/%d\n", pc->localindex, pc->localid);
    printf("State:        %s\n", ax25states[pc->state]);
    if (pc->reason)
      printf("Reason:       %s\n", nrreasons[pc->reason]);
    printf("Window:       %d\n", pc->window);
    printf("NAKsent:      %s\n", pc->naksent ? "Yes" : "No");
    printf("Closed:       %s\n", pc->closed ? "Yes" : "No");
    printf("Remote_busy:  %s\n", pc->remote_busy ? "Yes" : "No");
    printf("Retry:        %d\n", pc->retry);
    printf("Srtt:         %ld ms\n", pc->srtt);
    printf("Mean dev:     %ld ms\n", pc->mdev);
    if (pc->timer_t1.state == TIMER_RUN)
      printf("Timer T1:     %ld/%ld sec\n", pc->timer_t1.start - pc->timer_t1.count, pc->timer_t1.start);
    else
      printf("Timer T1:     Stopped\n");
    if (pc->timer_t2.state == TIMER_RUN)
      printf("Timer T2:     %ld/%ld sec\n", pc->timer_t2.start - pc->timer_t2.count, pc->timer_t2.start);
    else
      printf("Timer T2:     Stopped\n");
    if (pc->timer_t3.state == TIMER_RUN)
      printf("Timer T3:     %ld/%ld sec\n", pc->timer_t3.start - pc->timer_t3.count, pc->timer_t3.start);
    else
      printf("Timer T3:     Stopped\n");
    if (pc->timer_t4.state == TIMER_RUN)
      printf("Timer T4:     %ld/%ld sec\n", pc->timer_t4.start - pc->timer_t4.count, pc->timer_t4.start);
    else
      printf("Timer T4:     Stopped\n");
    printf("Rcv queue:    %d\n", pc->rcvcnt);
    if (pc->reseq) {
      printf("Resequencing queue:\n");
      for (bp = pc->reseq; bp; bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n", uchar(bp->data[2]), len_mbuf(bp));
    }
    printf("Snd queue:    %d\n", len_mbuf(pc->sndq));
    if (pc->resndq) {
      printf("Resend queue:\n");
      for (i = 0, bp = pc->resndq; bp; i++, bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n",
	       uchar(pc->send_state - pc->unack + i), len_mbuf(bp));
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dostart(argc, argv)
int  argc;
char  *argv[];
{
  server_enabled = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dostop(argc, argv)
int  argc;
char  *argv[];
{
  server_enabled = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

int  donetrom(argc, argv)
int  argc;
char  *argv[];
{

  static struct cmds netromcmds[] = {
    "broadcast",dobroadcast,0, NULLCHAR, NULLCHAR,
    "connect",  doconnect,  2, "netrom connect <node> [<user>]", NULLCHAR,
    "ident",    doident,    0, NULLCHAR, NULLCHAR,
    "nodes",    donodes,    0, NULLCHAR,
	"Usage: netrom nodes [call [+|- neighbor [quality [obsolescence]]]]",
    "parms",    doparms,    0, NULLCHAR, NULLCHAR,
    "routes",   doroutes,   0, NULLCHAR,
	"Usage: netrom routes [call [+|- [quality]]]",
    "reset",    doreset,    2, "netrom reset <nrcb>", NULLCHAR,
    "status",   dostatus,   0, NULLCHAR, NULLCHAR,
    "start",    dostart,    0, NULLCHAR, NULLCHAR,
    "stop",     dostop,     0, NULLCHAR, NULLCHAR,
    NULLCHAR,   NULLFP,     0, NULLCHAR, NULLCHAR
  };

  return subcmd(netromcmds, argc, argv);
}

/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/
/*---------------------------------------------------------------------------*/

netrom_initialize()
{
  link_manager_initialize();
  routing_manager_initialize();
}

