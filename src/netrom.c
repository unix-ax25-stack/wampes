/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/netrom.c,v 1.8 1990/03/01 11:03:16 deyke Exp $ */

#include <memory.h>
#include <stdio.h>
#include <string.h>

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

static int  nr_maxdest     =   400;     /* not used */
static int  nr_minqual     =     0;     /* not used */
static int  nr_hfqual      =   192;
static int  nr_rsqual      =   255;     /* not used */
static int  nr_obsinit     =     3;
static int  nr_minobs      =     0;     /* not used */
static int  nr_bdcstint    =  1800;
static int  nr_ttlinit     =    16;
static int  nr_ttimeout    =    60;
static int  nr_tretry      =     5;
static int  nr_tackdelay   =     4;
static int  nr_tbsydelay   =   180;
static int  nr_twindow     =     8;
static int  nr_tnoackbuf   =     8;
static int  nr_timeout     =  1800;
static int  nr_persistance =    64;     /* not used */
static int  nr_slottime    =    10;     /* not used */
static int  nr_callcheck   =     0;     /* not used */
static int  nr_beacon      =     0;     /* not used */
static int  nr_cq          =     0;     /* not used */

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
  " 6 Obsolescence count min to be broadcast     ", &nr_minobs,      0,   255,
  " 7 Auto-update broadcast interval (sec, 0=off)", &nr_bdcstint,    0, 65535,
  " 8 Network 'time-to-live' initializer         ", &nr_ttlinit,     1,   255,
  " 9 Transport timeout (sec)                    ", &nr_ttimeout,    5,   600,
  "10 Transport maximum tries                    ", &nr_tretry,      1,   127,
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

#define IDENTLEN     6
#define MAXLEVEL   256

struct broadcast {
  struct interface *ifp;
  char *path;
  struct broadcast *next;
};

struct node {
  struct ax25_addr *call;
  char  ident[IDENTLEN];
  int  level;
  struct link *links;
  struct node *neighbor, *old_neighbor;
  double  quality, old_quality, tmp_quality;
  int  force_broadcast;
  struct axcb *crosslink;
  struct node *prev, *next;
};

struct axcb *netrom_server_axcb;

static struct broadcast *broadcasts;
static struct node *nodes, *mynode;
static void calculate_all();

/*---------------------------------------------------------------------------*/

static struct node *nodeptr(call, create)
struct ax25_addr *call;
int  create;
{
  register struct node *pn;

  for (pn = nodes; pn && !addreq(call, pn->call); pn = pn->next) ;
  if (!pn && create) {
    pn = (struct node *) calloc(1, sizeof(struct node ));
    pn->call = (struct ax25_addr *) malloc(sizeof(struct ax25_addr ));
    addrcp(pn->call, call);
    memset(pn->ident, ' ', IDENTLEN);
    pn->level = MAXLEVEL;
    if (nodes) {
      pn->next = nodes;
      nodes->prev = pn;
    }
    nodes = pn;
  }
  return pn;
}

/*---------------------------------------------------------------------------*/

static void ax25_state_upcall(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  register struct node *pn;

  if (cp->user)
    pn = (struct node *) cp->user;
  else
    cp->user = (char *) (pn = nodeptr(axptr(cp->path), 1));
  switch (newstate) {
  case CONNECTING:
    break;
  case CONNECTED:
    pn->crosslink = cp;
    if (update_link(mynode, pn, 1, nr_hfqual)) calculate_all();
    break;
  case DISCONNECTING:
    if (cp->reason != NORMAL)
      if (update_link(mynode, pn, 1, 0)) calculate_all();
    break;
  case DISCONNECTED:
    pn->crosslink = NULLAXCB;
    if (cp->reason != NORMAL)
      if (update_link(mynode, pn, 1, 0)) calculate_all();
    del_ax(cp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void ax25_recv_upcall(cp)
struct axcb *cp;
{

  char  pid;
  struct mbuf *bp;

  while (cp->rcvq) {
    recv_ax(cp, &bp, 0);
    if (pullup(&bp, &pid, 1) != 1) continue;
    if (uchar(pid) == (PID_NETROM | PID_FIRST | PID_LAST))
      nr3_input(bp, axptr(cp->path));
    else
      free_p(bp);
  }
}

/*---------------------------------------------------------------------------*/

static void send_packet_to_neighbor(data, pn)
struct mbuf *data;
struct node *pn;
{

  char  path[10*AXALEN];
  struct mbuf *bp;

  if (!pn->crosslink) {
    addrcp(axptr(path), pn->call);
    addrcp(axptr(path + AXALEN), &mycall);
    path[AXALEN+6] |= E;
    pn->crosslink = open_ax(path, AX25_ACTIVE, ax25_recv_upcall, NULLVFP, ax25_state_upcall, (char *) pn);
    if (!pn->crosslink) {
      if (update_link(mynode, pn, 1, 0)) calculate_all();
      free_p(data);
      return;
    }
    pn->crosslink->mode = DGRAM;
  }
  if (!(bp = pushdown(data, 1))) {
    free_p(data);
    return;
  }
  bp->data[0] = (PID_NETROM | PID_FIRST | PID_LAST);
  send_ax(pn->crosslink, bp);
}

/*---------------------------------------------------------------------------*/

static void send_broadcast_packet(data)
struct mbuf *data;
{

  struct broadcast *p;
  struct mbuf *bp;

  if (!broadcasts)
    free_p(data);
  else
    for (p = broadcasts; p; p = p->next)
      if (p->next) {
	dup_p(&bp, data, 0, MAXINT16);
	ax_output(p->ifp, p->path, (char *) &mycall,
		  PID_NETROM | PID_FIRST | PID_LAST, bp);
      } else
	ax_output(p->ifp, p->path, (char *) &mycall,
		  PID_NETROM | PID_FIRST | PID_LAST, data);
}

/*---------------------------------------------------------------------------*/

static void link_manager_initialize()
{

  mynode = nodeptr(&mycall, 1);
  free((char *) mynode->call);
  mynode->call = &mycall;
  calculate_all();

  netrom_server_axcb = open_ax(NULLCHAR, AX25_SERVER, ax25_recv_upcall, NULLVFP, ax25_state_upcall, NULLCHAR);
  netrom_server_axcb->mode = DGRAM;

}

/*---------------------------------------------------------------------------*/
/****************************** Routing Manager ******************************/
/*---------------------------------------------------------------------------*/

struct link {
  struct node *node;
  struct linkinfo *info;
  struct link *prev, *next;
};

struct linkinfo {
  int  level;
  int  quality;
  long  time;
};

struct routes_stat {
  int  rcvd;
  int  sent;
};

static struct interface nr_interface;
static struct routes_stat routes_stat;
static struct timer broadcast_timer;

/*---------------------------------------------------------------------------*/

int  isnetrom(call)
struct ax25_addr *call;
{
  return (nodeptr(call, 0) != 0);
}

/*---------------------------------------------------------------------------*/

static struct linkinfo *linkinfoptr(node1, node2)
struct node *node1, *node2;
{

  register struct link *pl;
  struct linkinfo *pi;

  for (pl = node1->links; pl; pl = pl->next)
    if (pl->node == node2) return pl->info;
  pi = (struct linkinfo *) calloc(1, sizeof(struct linkinfo ));
  pi->level = MAXLEVEL;
  pl = (struct link *) calloc(1, sizeof(struct link ));
  pl->node = node2;
  pl->info = pi;
  if (node1->links) {
    pl->next = node1->links;
    node1->links->prev = pl;
  }
  node1->links = pl;
  pl = (struct link *) calloc(1, sizeof(struct link ));
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

static int  update_link(node1, node2, level, quality)
struct node *node1, *node2;
int  level, quality;
{

  int  ret;
  struct linkinfo *pi;

  if (node1 == node2) return 0;
  if (level > node1->level + 1 || level > node2->level + 1) return 0;
  pi = linkinfoptr(node1, node2);
  if (pi->level < level || pi->time == MAX_TIME) return 0;
  ret = 0;
  if (pi->level != level) {
    pi->level = level;
    ret = 1;
  }
  if (quality > 255) quality = 255;
  if (pi->quality != quality) {
    pi->quality = quality;
    ret = 1;
  }
  pi->time = currtime;
  if (node1->level > level) {
    node1->level = level;
    ret = 1;
  }
  if (node2->level > level) {
    node2->level = level;
    ret = 1;
  }
  return ret;
}

/*---------------------------------------------------------------------------*/

static void calculate_node(pn)
struct node *pn;
{

  double  quality;
  register struct link *pl;

  for (pl = pn->links; pl; pl = pl->next) {
    quality = pn->tmp_quality * pl->info->quality / 256.0;
    if (pl->node->tmp_quality < quality) {
      pl->node->tmp_quality = quality;
      calculate_node(pl->node);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void calculate_all()
{

  int  start_broadcast_timer;
  long  timelimit;
  register struct link *pl;
  register struct node *pn;
  struct link *pl1, *plnext;
  struct node *neighbor;
  struct node *pn1, *pnnext;

  /*** remove obsolete links ***/

  if (nr_obsinit && nr_bdcstint)
    timelimit = currtime - nr_obsinit * nr_bdcstint;
  else
    timelimit = 0;
  for (pn = nodes; pn; pn = pn->next)
    for (pl = pn->links; pl; pl = plnext) {
      plnext = pl->next;
      if (pl->info->time < timelimit ||
	  pl->info->time != MAX_TIME && pl->info->level > pn->level + 1) {
	if (pl->prev) pl->prev->next = pl->next;
	if (pl->next) pl->next->prev = pl->prev;
	if (pl == pn->links) pn->links = pl->next;
	pn1 = pl->node;
	for (pl1 = pn1->links; pl1->node != pn; pl1 = pl1->next) ;
	if (pl1->prev) pl1->prev->next = pl1->next;
	if (pl1->next) pl1->next->prev = pl1->prev;
	if (pl1 == pn1->links) pn1->links = pl1->next;
	free((char *) pl->info);
	free((char *) pl1);
	free((char *) pl);
      }
    }

  /*** fix node levels ***/

  for (pn = nodes; pn; pn = pn->next) {
    pn->level = MAXLEVEL;
    for (pl = pn->links; pl; pl = pl->next)
      if (pn->level > pl->info->level) pn->level = pl->info->level;
  }
  mynode->level = 0;

  /*** preset neighbor and quality ***/

  for (pn = nodes; pn; pn = pn->next) {
    pn->old_neighbor = pn->neighbor;
    pn->neighbor = 0;
    pn->old_quality = pn->quality;
    pn->quality = 0.0;
  }

  /*** calculate new neighbor and quality values ***/

  for (pl = mynode->links; pl; pl = pl->next) {
    for (pn = nodes; pn; pn = pn->next) pn->tmp_quality = 0.0;
    mynode->tmp_quality = 256.0;
    neighbor = pl->node;
    neighbor->tmp_quality = pl->info->quality;
    calculate_node(neighbor);
    for (pn = nodes; pn; pn = pn->next)
      if (pn->quality < pn->tmp_quality ||
	  pn->quality == pn->tmp_quality && neighbor == pn->old_neighbor) {
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
    set_timer(&broadcast_timer, 10 * 1000l);
    start_timer(&broadcast_timer);
  }

  /*** remove obsolete nodes ***/

  for (pn = nodes; pn; pn = pnnext) {
    pnnext = pn->next;
    if (pn != mynode && !pn->links && !pn->crosslink && !pn->force_broadcast) {
      if (pn->prev) pn->prev->next = pn->next;
      if (pn->next) pn->next->prev = pn->prev;
      if (pn == nodes) nodes = pn->next;
      free((char *) pn->call);
      free((char *) pn);
    }
  }
}

/*---------------------------------------------------------------------------*/

int  new_neighbor(call)
struct ax25_addr *call;
{
  if (update_link(mynode, nodeptr(call, 1), 1, nr_hfqual)) calculate_all();
}

/*---------------------------------------------------------------------------*/

static void broadcast_recv(bp, pn)
struct mbuf *bp;
struct node *pn;
{

  char  buf[AXALEN+IDENTLEN+AXALEN+1];
  char  id;
  char  ident[IDENTLEN];
  int  quality;
  struct linkinfo *pi;
  struct node *pb, *pd;

  routes_stat.rcvd++;
  if (pn == mynode) goto discard;
  if (pullup(&bp, &id, 1) != 1 || uchar(id) != 0xff) goto discard;
  if (pullup(&bp, ident, IDENTLEN) != IDENTLEN) goto discard;
  if (*ident > ' ') memcpy(pn->ident, ident, IDENTLEN);
  update_link(mynode, pn, 1, nr_hfqual);
  while (pullup(&bp, buf, sizeof(buf)) == sizeof(buf)) {
    if (ismycall(axptr(buf))) continue;
    pd = nodeptr(axptr(buf), 1);
    if (buf[AXALEN] > ' ') memcpy(pd->ident, buf + AXALEN, IDENTLEN);
    pb = nodeptr(axptr(buf + AXALEN + IDENTLEN), 1);
    quality = uchar(buf[AXALEN+IDENTLEN+AXALEN]);
    if (pb == mynode) {
      if (quality >= pd->quality) pd->force_broadcast = 1;
      continue;
    }
    if (pn == pb || pb == pd)
      update_link(pn, pd, 2, quality);
    else {
      pi = linkinfoptr(pn, pb);
      if (pi->time != MAX_TIME) pi->time = currtime;
      if (pi->quality) {
	int  q = quality * 256 / pi->quality;
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

static struct mbuf *alloc_broadcast_packet()
{
  struct mbuf *bp;

  if (bp = alloc_mbuf(256)) {
    *bp->data = 0xff;
    memcpy(bp->data + 1, mynode->ident, IDENTLEN);
    bp->cnt = 1 + IDENTLEN;
  }
  return bp;
}

/*---------------------------------------------------------------------------*/

static void send_broadcast()
{

  int  level, nextlevel;
  register char  *p;
  struct mbuf *bp;
  struct node *pn;

  set_timer(&broadcast_timer, nr_bdcstint * 1000l);
  start_timer(&broadcast_timer);
  bp = alloc_broadcast_packet();
  for (level = 1; level <= MAXLEVEL; level = nextlevel) {
    nextlevel = MAXLEVEL + 1;
    for (pn = nodes; pn; pn = pn->next)
      if (pn->level >= level && (((int) pn->quality) || pn->force_broadcast))
	if (pn->level == level) {
	  pn->force_broadcast = 0;
	  if (!bp) bp = alloc_broadcast_packet();
	  p = bp->data + bp->cnt;
	  addrcp(axptr(p), pn->call);
	  p += AXALEN;
	  memcpy(p, pn->ident, IDENTLEN);
	  p += IDENTLEN;
	  addrcp(axptr(p), pn->neighbor ? pn->neighbor->call : pn->call);
	  p += AXALEN;
	  *p++ = pn->quality;
	  if ((bp->cnt = p - bp->data) > 256 - 21) {
	    send_broadcast_packet(bp);
	    routes_stat.sent++;
	    bp = NULLBUF;
	  }
	} else if (pn->level < nextlevel)
	  nextlevel = pn->level;
  }
  if (bp) {
    send_broadcast_packet(bp);
    routes_stat.sent++;
  }
}

/*---------------------------------------------------------------------------*/

static void route_packet(bp, fromneighbor)
struct mbuf *bp;
struct node *fromneighbor;
{

  int  ttl;
  register struct node *pn;
  void circuit_manager();

  if (!bp || bp->cnt < 15) goto discard;

  if (fromneighbor != mynode) {
    int  calc = update_link(mynode, fromneighbor, 1, nr_hfqual);
    pn = nodeptr(axptr(bp->data), 1);
    if (pn == mynode) goto discard;  /* ROUTING ERROR */
    if (!pn->neighbor) {
      struct linkinfo *pi = linkinfoptr(mynode, fromneighbor);
      if (pi->quality) {
	int  q = 1;
	while (q * pi->quality / 256 < 1) q++;
	calc |= update_link(fromneighbor, pn, 2, q);
      }
    }
    if (calc) calculate_all();
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
    if (fromneighbor == mynode) {
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

  pn = nodeptr(axptr(bp->data + AXALEN), 1);
  if (!pn->neighbor) {
    pn->force_broadcast = 1;
    send_broadcast();
    goto discard;
  }

  if (pn->neighbor == fromneighbor ||
      addreq(pn->neighbor->call, axptr(bp->data))) send_broadcast();

  send_packet_to_neighbor(bp, pn->neighbor);
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
  if (++ttl > 255) ttl = 255;
  bp->data[2*AXALEN] = ttl;
  route_packet(bp, mynode);
}

/*---------------------------------------------------------------------------*/

static int  nr_send(bp, interface, gateway)
struct mbuf *bp;
struct interface *interface;
int32 gateway;
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

nr3_input(bp, fromcall)
struct mbuf *bp;
struct ax25_addr *fromcall;
{
  if (bp && bp->cnt && uchar(*bp->data) == 0xff)
    broadcast_recv(bp, nodeptr(fromcall, 1));
  else
    route_packet(bp, nodeptr(fromcall, 1));
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
  set_timer(&broadcast_timer, 10 * 1000l);
  start_timer(&broadcast_timer);

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

static int  server_enabled;
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

static void reset_t1(pc)
struct circuit *pc;
{
  pc->timer_t1.start = (pc->srtt + 2 * pc->mdev + MSPTICK) / MSPTICK;
  if (pc->timer_t1.start < 4) pc->timer_t1.start = 4;
}

/*---------------------------------------------------------------------------*/

static void inc_t1(pc)
struct circuit *pc;
{
  int32 tmp;

  if (tmp = pc->timer_t1.start / 4)
    pc->timer_t1.start += tmp;
  else
    pc->timer_t1.start++;
  tmp = (10 * pc->srtt + MSPTICK) / MSPTICK;
  if (pc->timer_t1.start > tmp) pc->timer_t1.start = tmp;
  if (pc->timer_t1.start < 4) pc->timer_t1.start = 4;
}

/*---------------------------------------------------------------------------*/

static int  busy(pc)
struct circuit *pc;
{
  return pc->rcvcnt >= nr_tnoackbuf * NR4MAXINFO;
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
    if (pc->chokesent = busy(pc)) opcode |= NR4CHOKE;
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

  stop_timer(&pc->timer_t5);
  while (pc->unack < pc->cwind) {
    if (pc->state != CONNECTED || pc->remote_busy) return;
    if (fill_sndq && pc->t_upcall) {
      cnt = space_nr(pc);
      if (cnt > 0) (*pc->t_upcall)(pc, cnt);
    }
    if (!pc->sndq) return;
    cnt = len_mbuf(pc->sndq);
    if (cnt < NR4MAXINFO) {
      if (pc->unack) return;
      if (pc->sndqtime + 2 > currtime) {
	pc->timer_t5.start = pc->sndqtime + 2 - currtime;
	start_timer(&pc->timer_t5);
	return;
      }
    }
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
  stop_timer(&pc->timer_t5);
  reset_t1(pc);
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

  inc_t1(pc);
  pc->cwind = 1;
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
  if (!run_timer(&pc->timer_t1)) close_nr(pc);
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

static void l4_t5_timeout(pc)
struct circuit *pc;
{
  try_send(pc, 1);
}

/*---------------------------------------------------------------------------*/

static struct circuit *create_circuit()
{

  register struct circuit *pc;
  static int  nextid;

  pc = (struct circuit *) calloc(1, sizeof(struct circuit ));
  nextid++;
  pc->localindex = uchar(nextid >> 8);
  pc->localid = uchar(nextid);
  pc->remoteindex = -1;
  pc->remoteid = -1;
  pc->cwind = 1;
  pc->srtt = 500l * nr_ttimeout;
  pc->mdev = pc->srtt / 2;
  reset_t1(pc);
  pc->timer_t1.func = l4_t1_timeout;
  pc->timer_t1.arg = (char *) pc;
  pc->timer_t2.func = l4_t2_timeout;
  pc->timer_t2.arg = (char *) pc;
  pc->timer_t3.func = l4_t3_timeout;
  pc->timer_t3.arg = (char *) pc;
  pc->timer_t4.func = l4_t4_timeout;
  pc->timer_t4.arg = (char *) pc;
  pc->timer_t5.func = l4_t5_timeout;
  pc->timer_t5.arg = (char *) pc;
  pc->next = circuits;
  return circuits = pc;
}

/*---------------------------------------------------------------------------*/

static void circuit_manager(bp)
struct mbuf *bp;
{

  int  nakrcvd;
  register struct circuit *pc;
  void nrserv_recv_upcall(), nrserv_send_upcall(), nrserv_state_upcall();

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
      pc = create_circuit();
      pc->remoteindex = uchar(bp->data[0]);
      pc->remoteid = uchar(bp->data[1]);
      addrcp(&pc->node, axptr(bp->data + 13));
      addrcp(&pc->orguser, axptr(bp->data + 6));
      addrcp(&pc->orgnode, axptr(bp->data + 13));
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
    if (bp->data[4] & NR4CHOKE) {
      if (!pc->remote_busy) pc->remote_busy = currtime;
      stop_timer(&pc->timer_t1);
      set_timer(&pc->timer_t4, nr_tbsydelay * 1000l);
      start_timer(&pc->timer_t4);
      pc->cwind = 1;
    } else {
      pc->remote_busy = 0;
      stop_timer(&pc->timer_t4);
    }
    if (uchar(pc->send_state - bp->data[3]) < pc->unack) {
      pc->retry = 0;
      stop_timer(&pc->timer_t1);
      if (pc->sndtime[uchar(bp->data[3]-1)]) {
	int32 rtt = (currtime - pc->sndtime[uchar(bp->data[3]-1)]) * 1000l;
	int32 abserr = (rtt > pc->srtt) ? rtt - pc->srtt : pc->srtt - rtt;
	pc->srtt = ((AGAIN - 1) * pc->srtt + rtt) / AGAIN;
	pc->mdev = ((DGAIN - 1) * pc->mdev + abserr) / DGAIN;
	reset_t1(pc);
	if (pc->cwind < pc->window && !pc->remote_busy) {
	  pc->cwind++;
	  pc->mdev += 2500;
	}
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
	      goto search_again;
	    }
	  if (pc->r_upcall && pc->rcvcnt) (*pc->r_upcall)(pc, pc->rcvcnt);
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
      pc->cwind = 1;
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
  if (!(pc = create_circuit())) {
    net_error = NO_SPACE;
    return 0;
  }
  pc->outbound = 1;
  addrcp(&pc->node, node);
  addrcp(&pc->orguser, orguser);
  addrcp(&pc->orgnode, &mycall);
  pc->window = window;
  pc->r_upcall = r_upcall;
  pc->t_upcall = t_upcall;
  pc->s_upcall = s_upcall;
  pc->user = user;
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
      if (cnt = len_mbuf(bp)) {
	append(&pc->sndq, bp);
	pc->sndqtime = currtime;
	try_send(pc, 0);
      }
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
  int  cnt;

  if (!pc) {
    net_error = INVALID;
    return (-1);
  }
  switch (pc->state) {
  case DISCONNECTED:
    net_error = NO_CONN;
    return (-1);
  case CONNECTING:
  case CONNECTED:
    if (!pc->closed) {
      cnt = (pc->cwind - pc->unack) * NR4MAXINFO - len_mbuf(pc->sndq);
      return (cnt > 0) ? cnt : 0;
    }
  case DISCONNECTING:
    net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
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
    if (pc->chokesent && !busy(pc)) {
      set_timer(&pc->timer_t2, nr_tackdelay * 1000l);
      start_timer(&pc->timer_t2);
    }
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
  stop_timer(&pc->timer_t5);
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

static void nrserv_recv_upcall(pc)
struct circuit *pc;
{
  struct mbuf *bp;

  recv_nr(pc, &bp, 0);
  login_write((struct login_cb *) pc->user, bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_send_upcall(pc)
struct circuit *pc;
{
  struct mbuf *bp;

  if (bp = login_read((struct login_cb *) pc->user, space_nr(pc)))
    send_nr(pc, bp);
}

/*---------------------------------------------------------------------------*/

static void nrserv_state_upcall(pc, oldstate, newstate)
struct circuit *pc;
int  oldstate, newstate;
{
  switch (newstate) {
  case CONNECTED:
    pc->user = (char *) login_open(print_address(pc), "NETROM", nrserv_send_upcall, close_nr, (char *) pc);
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

#include "session.h"

/*---------------------------------------------------------------------------*/

static int  nrclient_parse(buf, n)
char  *buf;
int16 n;
{
  if (!(current && current->type == NRSESSION && current->cb.netrom)) return (-1);
  if (n >= 1 && buf[n-1] == '\n') n--;
  if (!n) return (-1);
  return send_nr(current->cb.netrom, qdata(buf, n));
}

/*---------------------------------------------------------------------------*/

void nrclient_recv_upcall(pc)
struct circuit *pc;
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
    printf("Ident %-6.6s\n", mynode->ident);
  else {
    for (cp = argv[1], i = 0; i < IDENTLEN; i++)
      mynode->ident[i] = *cp ? toupper(*cp++) : ' ';
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dolinks(argc, argv)
int  argc;
char  *argv[];
{

  char  buf1[20], buf2[20];
  int  quality;
  long  timestamp;
  struct ax25_addr call;
  struct link *pl;
  struct linkinfo *pi;
  struct node *pn1, *pn2;

  if (argc >= 2) {
    if (setcall(&call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    if (argc > 2)
      pn1 = nodeptr(&call, 1);
    else if (!(pn1 = nodeptr(&call, 0))) {
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
	  if (pl->info->time != MAX_TIME)
	    printf("%-9s  %-9s  %5i  %7i  %4i\n", buf1, buf2, pl->info->level, pl->info->quality, currtime - pl->info->time);
	  else
	    printf("%-9s  %-9s  %5i  %7i\n", buf1, buf2, pl->info->level, pl->info->quality);
	}
      }
    return 0;
  }

  if (argc < 4 || argc > 5) return (-1);

  if (setcall(&call, argv[2])) {
    printf("Invalid call \"%s\"\n", argv[2]);
    return 1;
  }
  pn2 = nodeptr(&call, 1);
  if (pn1 == pn2) {
    printf("Both calls are identical\n");
    return 1;
  }
  if (pn1->level == MAXLEVEL && pn2->level == MAXLEVEL) {
    printf("Both nodes are unreachable\n");
    return 1;
  }

  quality = atoi(argv[3]);
  if (quality < 0 || quality > 255) {
    printf("Quality must be 0..255\n");
    return 1;
  }

  if (argc < 5)
    timestamp = currtime;
  else {
    if (strncmp(argv[4], "permanent", strlen(argv[4]))) return (-1);
    timestamp = MAX_TIME;
  }

  pi = linkinfoptr(pn1, pn2);
  pi->quality = quality;
  pi->level = min(pn1->level, pn2->level) + 1;
  pi->time = timestamp;
  calculate_all();
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  donodes(argc, argv)
int  argc;
char  *argv[];
{

  char  buf1[20], buf2[20];
  struct ax25_addr call;
  struct node *pn, *pn1;

  if (argc >= 2) {
    if (setcall(&call, argv[1])) {
      printf("Invalid call \"%s\"\n", argv[1]);
      return 1;
    }
    if (!(pn1 = nodeptr(&call, 0))) {
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
      printf("%-9s  %-6.6s  %-9s  %5i  %7.3f\n", buf1, pn->ident, buf2, pn->level, pn->quality);
    }
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
    return 0;
  default:
    printf("Usage: netrom parms [<parm#> [<parm value>]]\n");
    return 1;
  }
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
      printf("%8lx %5u%c%3u/%u%c %2d %5.1f  %-13s  %s\n",
	     (long) pc,
	     pc->rcvcnt,
	     pc->chokesent ? '*' : ' ',
	     pc->unack,
	     pc->cwind,
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
    printf("CHOKEsent:    %s\n", pc->chokesent ? "Yes" : "No");
    printf("Closed:       %s\n", pc->closed ? "Yes" : "No");
    if (pc->remote_busy)
      printf("Remote_busy:  %ld sec\n", currtime - pc->remote_busy);
    else
      printf("Remote_busy:  No\n");
    printf("CWind:        %d\n", pc->cwind);
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
    if (pc->timer_t5.state == TIMER_RUN)
      printf("Timer T5:     %ld/%ld sec\n", pc->timer_t5.start - pc->timer_t5.count, pc->timer_t5.start);
    else
      printf("Timer T5:     Stopped\n");
    printf("Rcv queue:    %d\n", pc->rcvcnt);
    if (pc->reseq) {
      printf("Reassembly queue:\n");
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
    "links",    dolinks,    0, NULLCHAR,
	"Usage: netrom links [<node> [<node2> <quality> [permanent]]]",
    "nodes",    donodes,    0, NULLCHAR, NULLCHAR,
    "parms",    doparms,    0, NULLCHAR, NULLCHAR,
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

