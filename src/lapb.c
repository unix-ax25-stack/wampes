/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/lapb.c,v 1.22 1992/05/14 13:20:12 deyke Exp $ */

/* Link Access Procedures Balanced (LAPB), the upper sublayer of
 * AX.25 Level 2.
 */
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "asy.h"
#include "cmdparse.h"
#include "netuser.h"

extern struct ax25_cb *netrom_server_axcb;

char  *ax25states[] = {
  "Disconnected",
  "Conn pending",
  "Connected",
  "Disc pending"
};

char  *ax25reasons[] = {
  "Normal",
  "Reset",
  "Timeout",
  "Network"
};

int  ax_maxframe =      7;      /* Transmit flow control level */
int  ax_paclen   =    236;      /* Maximum outbound packet size */
int  ax_pthresh  =     64;      /* Send polls for packets larger than this */
int  ax_retry    =     10;      /* Retry limit */
int32 ax_t1init  =   5000;      /* Retransmission timeout */
int32 ax_t2init  =   2000;      /* Acknowledgement delay timeout */
int32 ax_t3init  = 900000;      /* No-activity timeout */
int32 ax_t4init  =  60000;      /* Busy timeout */
int32 ax_t5init  =   1000;      /* Packet assembly timeout */
int  ax_window   =   2048;      /* Local flow control limit */
struct ax25_cb *axcb_server;    /* Server control block */

static struct ax25_cb *axcb_head;

static int is_flexnet __ARGS((char *call, int store));
static void reset_t1 __ARGS((struct ax25_cb *cp));
static void inc_t1 __ARGS((struct ax25_cb *cp));
static void send_packet __ARGS((struct ax25_cb *cp, int type, int cmdrsp, struct mbuf *data));
static int busy __ARGS((struct ax25_cb *cp));
static void send_ack __ARGS((struct ax25_cb *cp, int cmdrsp));
static void try_send __ARGS((struct ax25_cb *cp, int fill_sndq));
static void setaxstate __ARGS((struct ax25_cb *cp, int newstate));
static void t1_timeout __ARGS((struct ax25_cb *cp));
static void t2_timeout __ARGS((struct ax25_cb *cp));
static void t3_timeout __ARGS((struct ax25_cb *cp));
static void t4_timeout __ARGS((struct ax25_cb *cp));
static void t5_timeout __ARGS((struct ax25_cb *cp));
static struct ax25_cb *create_axcb __ARGS((struct ax25_cb *prototype));
static void build_path __ARGS((struct ax25_cb *cp, struct iface *ifp, struct ax25 *hdr, int reverse));
static int put_reseq __ARGS((struct ax25_cb *cp, struct mbuf *bp, int ns));
static int dodigipeat __ARGS((int argc, char *argv [], void *p));
static int domaxframe __ARGS((int argc, char *argv [], void *p));
static int domycall __ARGS((int argc, char *argv [], void *p));
static int dopaclen __ARGS((int argc, char *argv [], void *p));
static int dopthresh __ARGS((int argc, char *argv [], void *p));
static int doaxreset __ARGS((int argc, char *argv [], void *p));
static int doretry __ARGS((int argc, char *argv [], void *p));
static int dorouteadd __ARGS((int argc, char *argv [], void *p));
static void doroutelistentry __ARGS((struct ax_route *rp));
static int doroutelist __ARGS((int argc, char *argv [], void *p));
static int doroutestat __ARGS((int argc, char *argv [], void *p));
static int doaxroute __ARGS((int argc, char *argv [], void *p));
static int doaxstatus __ARGS((int argc, char *argv [], void *p));
static int dot1 __ARGS((int argc, char *argv [], void *p));
static int dot2 __ARGS((int argc, char *argv [], void *p));
static int dot3 __ARGS((int argc, char *argv [], void *p));
static int dot4 __ARGS((int argc, char *argv [], void *p));
static int dot5 __ARGS((int argc, char *argv [], void *p));
static int doaxwindow __ARGS((int argc, char *argv [], void *p));

/*---------------------------------------------------------------------------*/

static int is_flexnet(call, store)
char *call;
int store;
{

#define FTABLESIZE 23

  struct ftable_t {
    struct ftable_t *next;
    char call[AXALEN];
  };

  char *cp;
  long hashval;
  static struct ftable_t *ftable[FTABLESIZE];
  struct ftable_t **tp;
  struct ftable_t *p;

  cp = call;
  hashval  = ((*cp++ << 23) & 0x0f000000);
  hashval |= ((*cp++ << 19) & 0x00f00000);
  hashval |= ((*cp++ << 15) & 0x000f0000);
  hashval |= ((*cp++ << 11) & 0x0000f000);
  hashval |= ((*cp++ <<  7) & 0x00000f00);
  hashval |= ((*cp++ <<  3) & 0x000000f0);
  hashval |= ((*cp   >>  1) & 0x0000000f);
  tp = ftable + (hashval % FTABLESIZE);
  for (p = *tp; p && !addreq(p->call, call); p = p->next) ;
  if (!p && store) {
    p = malloc(sizeof(*p));
    addrcp(p->call, call);
    p->next = *tp;
    *tp = p;
  }
  return (int) (p != 0);
}

/*---------------------------------------------------------------------------*/

static void reset_t1(cp)
struct ax25_cb *cp;
{
  int32 tmp;

  tmp = cp->srtt + 4 * cp->mdev;
  if (tmp < 500) tmp = 500;
  set_timer(&cp->timer_t1, tmp);
}

/*---------------------------------------------------------------------------*/

static void inc_t1(cp)
struct ax25_cb *cp;
{
  int32 tmp;

  tmp = (dur_timer(&cp->timer_t1) * 5 + 2) / 4;
  if (tmp > 10 * cp->srtt) tmp = 10 * cp->srtt;
  if (tmp < 500) tmp = 500;
  set_timer(&cp->timer_t1, tmp);
}

/*---------------------------------------------------------------------------*/

#define next_seq(n)  (((n) + 1) & 7)

/*---------------------------------------------------------------------------*/

static void send_packet(cp, type, cmdrsp, data)
struct ax25_cb *cp;
int  type;
int  cmdrsp;
struct mbuf *data;
{

  int  control;
  struct mbuf *bp;

  if (cp->mode == STREAM && (type == I || type == UI)) {
    if (!(bp = pushdown(data, 1))) {
      free_p(data);
      return;
    }
    *bp->data = PID_NO_L3;
    data = bp;
  }

  control = type;
  if (type == I) {
    control |= (cp->vs << 1);
    cp->vs = next_seq(cp->vs);
  }
  if ((type & 3) != U) {
    control |= (cp->vr << 5);
    stop_timer(&cp->timer_t2);
  }
  if (cmdrsp & PF) control |= PF;
  if (!(bp = pushdown(data, 1))) {
    free_p(data);
    return;
  }
  *bp->data = control;
  data = bp;

  if (cmdrsp & DST_C)
    cp->hdr.cmdrsp = LAPB_COMMAND;
  else if (cmdrsp & SRC_C)
    cp->hdr.cmdrsp = LAPB_RESPONSE;
  else
    cp->hdr.cmdrsp = VERS1;
  if (!(bp = htonax25(&cp->hdr, data))) {
    free_p(data);
    return;
  }
  data = bp;

  if (type == RR || type == REJ || type == UA) cp->rnrsent = 0;
  if (type == RNR) cp->rnrsent = 1;
  if (type == REJ) cp->rejsent = 1;
  if (cmdrsp == POLL) cp->polling = 1;
  if (type == I || cmdrsp == POLL) start_timer(&cp->timer_t1);

  axroute(cp, data);
}

/*---------------------------------------------------------------------------*/

static int  busy(cp)
struct ax25_cb *cp;
{
  return cp->peer ? space_ax(cp->peer) <= 0 : cp->rcvcnt >= ax_window;
}

/*---------------------------------------------------------------------------*/

static void send_ack(cp, cmdrsp)
struct ax25_cb *cp;
int  cmdrsp;
{
  if (busy(cp))
    send_packet(cp, RNR, cmdrsp, NULLBUF);
  else if (!cp->rejsent && (cp->reseq[0].bp || cp->reseq[1].bp ||
			    cp->reseq[2].bp || cp->reseq[3].bp ||
			    cp->reseq[4].bp || cp->reseq[5].bp ||
			    cp->reseq[6].bp || cp->reseq[7].bp))
    send_packet(cp, REJ, cmdrsp, NULLBUF);
  else
    send_packet(cp, RR, cmdrsp, NULLBUF);
}

/*---------------------------------------------------------------------------*/

static void try_send(cp, fill_sndq)
struct ax25_cb *cp;
int  fill_sndq;
{

  int  cnt;
  struct mbuf *bp;

  stop_timer(&cp->timer_t5);
  while (cp->unack < cp->cwind) {
    if (cp->state != CONNECTED || cp->remote_busy) return;
    if (fill_sndq && cp->t_upcall) {
      cnt = space_ax(cp);
      if (cnt > 0) {
	(*cp->t_upcall)(cp, cnt);
	if (cp->unack >= cp->cwind) return;
      }
    }
    if (!cp->sndq) return;
    if (cp->mode == STREAM) {
      cnt = len_p(cp->sndq);
      if (cnt < ax_paclen) {
	if (cp->unack) return;
	if (!cp->peer && cp->sndqtime + ax_t5init - msclock() > 0) {
	  set_timer(&cp->timer_t5, cp->sndqtime + ax_t5init - msclock());
	  start_timer(&cp->timer_t5);
	  return;
	}
      }
      if (cnt > ax_paclen) cnt = ax_paclen;
      if (!(bp = alloc_mbuf(cnt))) return;
      pullup(&cp->sndq, bp->data, bp->cnt = cnt);
    } else {
      bp = dequeue(&cp->sndq);
    }
    enqueue(&cp->resndq, bp);
    cp->unack++;
    cp->sndtime[cp->vs] = msclock();
    dup_p(&bp, bp, 0, MAXINT16);
    send_packet(cp, I, CMD, bp);
  }
}

/*---------------------------------------------------------------------------*/

static void setaxstate(cp, newstate)
struct ax25_cb *cp;
int  newstate;
{
  int  oldstate;

  oldstate = cp->state;
  cp->state = newstate;
  cp->polling = 0;
  cp->retry = 0;
  stop_timer(&cp->timer_t1);
  stop_timer(&cp->timer_t2);
  stop_timer(&cp->timer_t4);
  stop_timer(&cp->timer_t5);
  reset_t1(cp);
  switch (newstate) {
  case DISCONNECTED:
    if (cp->peer) close_ax(cp->peer);
    if (cp->s_upcall) (*cp->s_upcall)(cp, oldstate, newstate);
    if (cp->peer && cp->peer->state == DISCONNECTED) {
      del_ax(cp->peer);
      del_ax(cp);
    }
    break;
  case CONNECTING:
    if (cp->s_upcall) (*cp->s_upcall)(cp, oldstate, newstate);
    send_packet(cp, SABM, POLL, NULLBUF);
    break;
  case CONNECTED:
    if (cp->peer && cp->peer->state == DISCONNECTED) {
      send_packet(cp->peer, UA, FINAL, NULLBUF);
      setaxstate(cp->peer, CONNECTED);
    }
    if (cp->s_upcall) (*cp->s_upcall)(cp, oldstate, newstate);
    try_send(cp, 1);
    break;
  case DISCONNECTING:
    if (cp->peer) close_ax(cp->peer);
    if (cp->s_upcall) (*cp->s_upcall)(cp, oldstate, newstate);
    send_packet(cp, DISC, POLL, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void t1_timeout(cp)
struct ax25_cb *cp;
{
  inc_t1(cp);
  cp->cwind = 1;
  if (++cp->retry > ax_retry) cp->reason = TIMEOUT;
  switch (cp->state) {
  case DISCONNECTED:
    break;
  case CONNECTING:
    if (cp->peer && cp->peer->state == DISCONNECTED)
      if (cp->retry > 2)
	setaxstate(cp, DISCONNECTED);
      else
	start_timer(&cp->timer_t1);
    else if (cp->retry > ax_retry)
      setaxstate(cp, DISCONNECTED);
    else
      send_packet(cp, SABM, POLL, NULLBUF);
    break;
  case CONNECTED:
    if (cp->retry > ax_retry) {
      setaxstate(cp, DISCONNECTING);
    } else if (!cp->polling && !cp->remote_busy && cp->unack &&
	       len_p(cp->resndq) <= ax_pthresh) {
      int  old_vs;
      struct mbuf *bp;
      old_vs = cp->vs;
      cp->vs = (cp->vs - cp->unack) & 7;
      cp->sndtime[cp->vs] = 0;
      dup_p(&bp, cp->resndq, 0, MAXINT16);
      send_packet(cp, I, POLL, bp);
      cp->vs = old_vs;
    } else {
      send_ack(cp, POLL);
    }
    break;
  case DISCONNECTING:
    if (cp->retry > ax_retry)
      setaxstate(cp, DISCONNECTED);
    else
      send_packet(cp, DISC, POLL, NULLBUF);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void t2_timeout(cp)
struct ax25_cb *cp;
{
  send_ack(cp, RESP);
}

/*---------------------------------------------------------------------------*/

static void t3_timeout(cp)
struct ax25_cb *cp;
{
  if (!run_timer(&cp->timer_t1)) send_ack(cp, POLL);
}

/*---------------------------------------------------------------------------*/

static void t4_timeout(cp)
struct ax25_cb *cp;
{
  if (!cp->polling) send_ack(cp, POLL);
}

/*---------------------------------------------------------------------------*/

static void t5_timeout(cp)
struct ax25_cb *cp;
{
  try_send(cp, 1);
}

/*---------------------------------------------------------------------------*/

static struct ax25_cb *create_axcb(prototype)
struct ax25_cb *prototype;
{
  struct ax25_cb *cp;

  if (prototype) {
    cp = (struct ax25_cb *) malloc(sizeof(struct ax25_cb ));
    *cp = *prototype;
  } else
    cp = (struct ax25_cb *) calloc(1, sizeof(struct ax25_cb ));
  cp->cwind = 1;
  cp->timer_t1.func = (void (*)()) t1_timeout;
  cp->timer_t1.arg = cp;
  cp->timer_t2.func = (void (*)()) t2_timeout;
  cp->timer_t2.arg = cp;
  cp->timer_t3.func = (void (*)()) t3_timeout;
  cp->timer_t3.arg = cp;
  cp->timer_t4.func = (void (*)()) t4_timeout;
  cp->timer_t4.arg = cp;
  cp->timer_t5.func = (void (*)()) t5_timeout;
  cp->timer_t5.arg = cp;
  cp->next = axcb_head;
  return axcb_head = cp;
}

/*---------------------------------------------------------------------------*/

static void build_path(cp, ifp, hdr, reverse)
struct ax25_cb *cp;
struct iface *ifp;
struct ax25 *hdr;
int  reverse;
{

  char  *dest;
  int  d;
  int  i;
  struct ax_route *rp;

  if (reverse) {

    /*** copy hdr into control block ***/

    addrcp(cp->hdr.dest, hdr->source);
    addrcp(cp->hdr.source, hdr->dest);
    for (i = 0; i < hdr->ndigis; i++)
      addrcp(cp->hdr.digis[i], hdr->digis[hdr->ndigis-1-i]);
    cp->hdr.ndigis = hdr->ndigis;

    /*** find my last address ***/

    cp->hdr.nextdigi = 0;
    for (i = 0; i < cp->hdr.ndigis; i++)
      if (ismyax25addr(cp->hdr.digis[i])) cp->hdr.nextdigi = i + 1;

    cp->ifp = ifp;

  } else {

    /*** copy hdr into control block ***/

    cp->hdr = *hdr;

    /*** find my last address ***/

    cp->hdr.nextdigi = 0;
    for (i = 0; i < cp->hdr.ndigis; i++)
      if (ismyax25addr(cp->hdr.digis[i])) cp->hdr.nextdigi = i + 1;

    /*** remove all digipeaters before me ***/

    d = cp->hdr.nextdigi - 1;
    if (d > 0) {
      for (i = d; i < cp->hdr.ndigis; i++)
	addrcp(cp->hdr.digis[i-d], cp->hdr.digis[i]);
      cp->hdr.ndigis -= d;
      cp->hdr.nextdigi = 1;
    }

    /*** add necessary digipeaters and find interface ***/

    dest = cp->hdr.nextdigi < cp->hdr.ndigis ? cp->hdr.digis[cp->hdr.nextdigi] : cp->hdr.dest;
    for (rp = ax_routeptr(dest, 0); rp; rp = rp->digi) {
      if (rp->digi && cp->hdr.ndigis < MAXDIGIS) {
	for (i = cp->hdr.ndigis - 1; i >= cp->hdr.nextdigi; i--)
	  addrcp(cp->hdr.digis[i+1], cp->hdr.digis[i]);
	cp->hdr.ndigis++;
	addrcp(cp->hdr.digis[cp->hdr.nextdigi], rp->digi->call);
      }
      cp->ifp = rp->ifp;
    }
    if (!cp->ifp) cp->ifp = axroute_default_ifp;

    /*** replace my address with hwaddr of interface ***/

    addrcp(cp->hdr.nextdigi ? cp->hdr.digis[0] : cp->hdr.source,
	   cp->ifp ? cp->ifp->hwaddr : Mycall);

  }

  cp->srtt = (ax_t1init * (1 + 2 * (cp->hdr.ndigis - cp->hdr.nextdigi))) / 2;
  cp->mdev = cp->srtt / 4;
  reset_t1(cp);
}

/*---------------------------------------------------------------------------*/

static int  put_reseq(cp, bp, ns)
struct ax25_cb *cp;
struct mbuf *bp;
int  ns;
{

  char  *p;
  int  cnt, sum;
  struct axreseq *rp;
  struct mbuf *tp;

  if (next_seq(ns) == cp->vr) return 0;
  rp = &cp->reseq[ns];
  if (rp->bp) return 0;
  for (sum = 0, tp = bp; tp; tp = tp->next) {
    cnt = tp->cnt;
    p = tp->data;
    while (cnt--) sum += uchar(*p++);
  }
  if (ns != cp->vr && sum == rp->sum) return 0;
  rp->bp = bp;
  rp->sum = sum;
  return 1;
}

/*---------------------------------------------------------------------------*/

int  lapb_input(iface, hdr, bp)
struct iface *iface;
struct ax25 *hdr;
struct mbuf *bp;
{

  int  cmdrsp;
  int  control;
  int  for_me;
  int  nr;
  int  ns;
  int  pid;
  int  type;
  struct ax25_cb *cp;
  struct ax25_cb *cpp;

  if (!bp) return (-1);
  control = uchar(*bp->data);
  type = ftype(control);
  for_me = (ismyax25addr(hdr->dest) != NULLIF);

  if (!for_me &&
      (type == UI ||
       addreq(hdr->source, hdr->dest) ||
       is_flexnet(hdr->source, 0) && is_flexnet(hdr->dest, 0))) {
    struct mbuf *hbp;
    if (!(hbp = htonax25(hdr, bp))) {
      free_p(bp);
      return (-1);
    }
    axroute(NULLAXCB, hbp);
    return 0;
  }

  (void) PULLCHAR(&bp);
  if (bp) {
    pid = uchar(*bp->data);
    if (pid == PID_FLEXNET) is_flexnet(hdr->source, 1);
  }

  switch (hdr->cmdrsp) {
  case LAPB_COMMAND:
    cmdrsp = DST_C | (control & PF);
    break;
  case LAPB_RESPONSE:
    cmdrsp = SRC_C | (control & PF);
    break;
  default:
    cmdrsp = VERS1;
    break;
  }

  for (cp = axcb_head; cp; cp = cp->next)
    if (addreq(hdr->source, cp->hdr.dest) && addreq(hdr->dest, cp->hdr.source)) break;
  if (!cp) {
    if (for_me && netrom_server_axcb && isnetrom(hdr->source))
      cp = create_axcb(netrom_server_axcb);
    else if (for_me && axcb_server)
      cp = create_axcb(axcb_server);
    else
      cp = create_axcb(NULLAXCB);
    build_path(cp, iface, hdr, 1);
    if (!for_me) {
      cp->peer = cpp = create_axcb(NULLAXCB);
      cpp->peer = cp;
      build_path(cpp, NULLIF, hdr, 0);
    } else
      cpp = NULLAXCB;
  } else
    cpp = cp->peer;

  if (type == SABM) {
    int  i;
    build_path(cp, iface, hdr, 1);
    if (cp->unack)
      start_timer(&cp->timer_t1);
    else
      stop_timer(&cp->timer_t1);
    stop_timer(&cp->timer_t2);
    stop_timer(&cp->timer_t4);
    cp->polling = 0;
    cp->rnrsent = 0;
    cp->rejsent = 0;
    cp->remote_busy = 0;
    cp->vr = 0;
    cp->vs = cp->unack;
    cp->retry = 0;
    for (i = 0; i < 8; i++)
      if (cp->reseq[i].bp) {
	free_p(cp->reseq[i].bp);
	cp->reseq[i].bp = 0;
      }
  }

  if (cp->mode == STREAM && type == I && pid != PID_NO_L3) {
    cp->mode = DGRAM;
    if (cpp) cpp->mode = DGRAM;
  }

  set_timer(&cp->timer_t3, ax_t3init);
  start_timer(&cp->timer_t3);

  switch (cp->state) {

  case DISCONNECTED:
    if (for_me) {
      if (type == SABM && cmdrsp != VERS1 && cp->r_upcall) {
	send_packet(cp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	setaxstate(cp, CONNECTED);
      } else {
	if (cmdrsp != RESP && cmdrsp != FINAL)
	  send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	del_ax(cp);
      }
    } else {
      if (type == SABM && cmdrsp != VERS1 && cpp->state == DISCONNECTED) {
	setaxstate(cpp, CONNECTING);
      } else if (type == SABM && cmdrsp != VERS1 && cpp->state == CONNECTING) {
	build_path(cpp, NULLIF, hdr, 0);
	send_packet(cpp, SABM, POLL, NULLBUF);
      } else {
	if (cmdrsp != RESP && cmdrsp != FINAL)
	  send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	if (cpp->state == DISCONNECTED) {
	  del_ax(cpp);
	  del_ax(cp);
	}
      }
    }
    break;

  case CONNECTING:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
      break;
    case SABM:
      if (cmdrsp != VERS1) {
	send_packet(cp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
	setaxstate(cp, CONNECTED);
      }
      break;
    case UA:
      if (cmdrsp != VERS1) {
	setaxstate(cp, CONNECTED);
      } else {
	if (cpp && cpp->state == DISCONNECTED)
	  send_packet(cpp, DM, FINAL, NULLBUF);
	cp->reason = RESET;
	setaxstate(cp, DISCONNECTING);
      }
      break;
    case DISC:
      send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
    case FRMR:
      if (cpp && cpp->state == DISCONNECTED)
	send_packet(cpp, DM, FINAL, NULLBUF);
      cp->reason = RESET;
      setaxstate(cp, DISCONNECTED);
      break;
    }
    break;

  case CONNECTED:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
      stop_timer(&cp->timer_t1);
      nr = control >> 5;
      if (((cp->vs - nr) & 7) < cp->unack) {
	if (!cp->polling) {
	  cp->retry = 0;
	  if (cp->sndtime[(nr-1)&7]) {
	    int32 rtt = msclock() - cp->sndtime[(nr-1)&7];
	    int32 abserr = (rtt > cp->srtt) ? rtt - cp->srtt : cp->srtt - rtt;
	    cp->srtt = ((AGAIN - 1) * cp->srtt + rtt + (AGAIN / 2)) / AGAIN;
	    cp->mdev = ((DGAIN - 1) * cp->mdev + abserr + (DGAIN / 2)) / DGAIN;
	    reset_t1(cp);
	    if (cp->cwind < ax_maxframe) {
	      cp->mdev += ((cp->srtt / cp->cwind + 2) / 4);
	      cp->cwind++;
	    }
	  }
	}
	while (((cp->vs - nr) & 7) < cp->unack) {
	  cp->resndq = free_p(cp->resndq);
	  cp->unack--;
	}
	if (cpp && cpp->rnrsent && !busy(cpp)) send_ack(cpp, RESP);
      }
      if (type == I) {
	if (for_me &&
	    pid == PID_NETROM &&
	    cp->r_upcall != netrom_server_axcb->r_upcall) {
	  new_neighbor(hdr->source);
	  setaxstate(cp, DISCONNECTING);
	  free_p(bp);
	  return 0;
	}
	ns = (control >> 1) & 7;
	if (!bp) bp = alloc_mbuf(0);
	if (put_reseq(cp, bp, ns))
	  while (bp = cp->reseq[cp->vr].bp) {
	    cp->reseq[cp->vr].bp = 0;
	    cp->vr = next_seq(cp->vr);
	    cp->rejsent = 0;
	    if (cp->mode == STREAM) (void) PULLCHAR(&bp);
	    if (for_me) {
	      cp->rcvcnt += len_p(bp);
	      if (cp->mode == STREAM)
		append(&cp->rcvq, bp);
	      else
		enqueue(&cp->rcvq, bp);
	    } else
	      send_ax(cpp, bp);
	  }
	if (cmdrsp == POLL)
	  send_ack(cp, FINAL);
	else {
	  set_timer(&cp->timer_t2, ax_t2init);
	  start_timer(&cp->timer_t2);
	}
	if (cp->r_upcall && cp->rcvcnt) (*cp->r_upcall)(cp, cp->rcvcnt);
      } else {
	if (cmdrsp == POLL) send_ack(cp, FINAL);
	if (cp->polling && cmdrsp == FINAL) cp->retry = cp->polling = 0;
	if (type == RNR) {
	  if (!cp->remote_busy) cp->remote_busy = msclock();
	  set_timer(&cp->timer_t4, ax_t4init);
	  start_timer(&cp->timer_t4);
	  cp->cwind = 1;
	} else {
	  cp->remote_busy = 0;
	  stop_timer(&cp->timer_t4);
	  if (cp->unack && type == REJ) {
	    int  old_vs;
	    struct mbuf *bp1;
	    old_vs = cp->vs;
	    cp->vs = (cp->vs - cp->unack) & 7;
	    cp->sndtime[cp->vs] = 0;
	    dup_p(&bp1, cp->resndq, 0, MAXINT16);
	    send_packet(cp, I, CMD, bp1);
	    cp->vs = old_vs;
	    cp->cwind = 1;
	  } else if (cp->unack && cmdrsp == FINAL) {
	    struct mbuf *bp1, *qp;
	    cp->vs = (cp->vs - cp->unack) & 7;
	    for (qp = cp->resndq; qp; qp = qp->anext) {
	      cp->sndtime[cp->vs] = 0;
	      dup_p(&bp1, qp, 0, MAXINT16);
	      send_packet(cp, I, CMD, bp1);
	    }
	  }
	}
      }
      try_send(cp, 1);
      if (cp->polling || cp->unack && !cp->remote_busy)
	start_timer(&cp->timer_t1);
      if (cp->closed && !cp->sndq && !cp->unack ||
	  cp->remote_busy && msclock() - cp->remote_busy > 900000L)
	setaxstate(cp, DISCONNECTING);
      break;
    case SABM:
      send_packet(cp, UA, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
      try_send(cp, 1);
      break;
    case DISC:
      send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
      setaxstate(cp, DISCONNECTED);
      break;
    case UA:
      cp->remote_busy = 0;
      stop_timer(&cp->timer_t4);
      if (cp->unack) start_timer(&cp->timer_t1);
      try_send(cp, 1);
      break;
    case FRMR:
      setaxstate(cp, DISCONNECTING);
      break;
    }
    break;

  case DISCONNECTING:
    switch (type) {
    case I:
    case RR:
    case RNR:
    case REJ:
    case SABM:
      if (cmdrsp != RESP && cmdrsp != FINAL)
	send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
      break;
    case DISC:
      send_packet(cp, DM, (cmdrsp == POLL) ? FINAL : RESP, NULLBUF);
    case DM:
    case UA:
    case FRMR:
      setaxstate(cp, DISCONNECTED);
      break;
    }
    break;
  }

  free_p(bp);
  return 0;
}

/*---------------------------------------------------------------------------*/
/******************************* AX25 Commands *******************************/
/*---------------------------------------------------------------------------*/

/* Control AX.25 digipeating */

static int  dodigipeat(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&Digipeat, "Digipeat", argc, argv, 0, 2);
}

/*---------------------------------------------------------------------------*/

/* Force a retransmission */

static int  doaxkick(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  struct ax25_cb *axp;

  axp = (struct ax25_cb *) ltop(htol(argv[1]));
  if (!valid_ax(axp)) {
    printf(Notval);
    return 1;
  }
  kick_ax(axp);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set maximum number of frames that will be allowed in flight */

static int  domaxframe(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_maxframe, "Maxframe", argc, argv, 1, 7);
}

/*---------------------------------------------------------------------------*/

/* Display or change our AX.25 address */

static int  domycall(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  char  buf[15];

  if (argc < 2) {
    pax25(buf, Mycall);
    printf("Mycall %s\n", buf);
  } else {
    if (setcall(Mycall, argv[1]) == -1) return 1;
    Mycall[ALEN] |= E;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set maximum length of I-frame data field */

static int  dopaclen(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_paclen, "Paclen", argc, argv, 1, MAXINT16);
}

/*---------------------------------------------------------------------------*/

/* Set size of I-frame above which polls will be sent after a timeout */

static int  dopthresh(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_pthresh, "Pthresh", argc, argv, 0, MAXINT16);
}

/*---------------------------------------------------------------------------*/

/* Eliminate a AX25 connection */

static int  doaxreset(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  struct ax25_cb *cp;

  cp = (struct ax25_cb *) htol(argv[1]);
  if (!valid_ax(cp)) {
    printf(Notval);
    return 1;
  }
  reset_ax(cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set retry limit count */

static int  doretry(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_retry, "Retry", argc, argv, 0, MAXINT16);
}

/*---------------------------------------------------------------------------*/

static int  dorouteadd(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  char  tmp[AXALEN];
  int  i, j, perm;
  struct ax25 hdr;
  struct iface *iface;

  argc--;
  argv++;

  if (perm = !strcmp(*argv, "permanent")) {
    argc--;
    argv++;
  }

  if (!(iface = if_lookup(*argv))) {
    printf("Interface \"%s\" unknown\n", *argv);
    return 1;
  }
  if (iface->output != ax_output) {
    printf("Interface \"%s\" not kiss\n", *argv);
    return 1;
  }
  argc--;
  argv++;

  if (argc <= 0) {
    printf("Usage: ax25 route add [permanent] <interface> default|<path>\n");
    return 1;
  }

  if (!strcmp(*argv, "default")) {
    axroute_default_ifp = iface;
    return 0;
  }

  if (setcall(hdr.source, *argv)) {
    printf("Invalid call \"%s\"\n", *argv);
    return 1;
  }
  argc--;
  argv++;

  for (hdr.nextdigi = 0; argc > 0; argc--, argv++)
    if (strncmp("via", *argv, strlen(*argv))) {
      if (hdr.nextdigi >= MAXDIGIS) {
	printf("Too many digipeaters (max %d)\n", MAXDIGIS);
	return 1;
      }
      if (setcall(hdr.digis[hdr.nextdigi++], *argv)) {
	printf("Invalid call \"%s\"\n", *argv);
	return 1;
      }
    }
  for (i = 0, j = hdr.nextdigi - 1; i < j; i++, j--) {
    addrcp(tmp, hdr.digis[i]);
    addrcp(hdr.digis[i], hdr.digis[j]);
    addrcp(hdr.digis[j], tmp);
  }

  axroute_add(iface, &hdr, perm);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void doroutelistentry(rp)
struct ax_route *rp;
{

  char  *cp, buf[1024];
  int  i, n;
  int  perm;
  struct ax_route *rp_stack[20];
  struct iface *ifp;
  struct tm *tm;

  tm = gmtime(&rp->time);
  pax25(cp = buf, rp->call);
  perm = rp->perm;
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    ifp = rp->ifp;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, rp_stack[i]->call);
  }
  printf("%2d-%.3s  %-9s  %c %s\n",
	 tm->tm_mday,
	 "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	 ifp ? ifp->name : "???",
	 perm ? '*' : ' ',
	 buf);
}

/*---------------------------------------------------------------------------*/

static int  doroutelist(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  char  call[AXALEN];
  int  i;
  struct ax_route *rp;

  puts("Date    Interface  P Path");
  if (argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = Ax_routes[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if (setcall(call, *argv) || !(rp = ax_routeptr(call, 0)))
      printf("*** Not in table *** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int doroutestat(argc, argv, p)
int argc;
char *argv[];
void *p;
{

#define NIFACES 128

  struct ifptable_t {
    struct iface *ifp;
    int count;
  };

  int dev;
  int i;
  int total;
  struct ax_route *dp;
  struct ax_route *rp;
  struct iface *ifp;
  struct ifptable_t ifptable[NIFACES];

  memset(ifptable, 0, sizeof(ifptable));
  for (dev = 0, ifp = Ifaces; ifp; dev++, ifp = ifp->next)
    ifptable[dev].ifp = ifp;
  for (i = 0; i < AXROUTESIZE; i++)
    for (rp = Ax_routes[i]; rp; rp = rp->next) {
      for (dp = rp; dp->digi; dp = dp->digi) ;
      if (dp->ifp)
	for (dev = 0; dev < NIFACES; dev++)
	  if (ifptable[dev].ifp == dp->ifp) {
	    ifptable[dev].count++;
	    break;
	  }
    }
  puts("Interface  Count");
  total = 0;
  for (dev = 0; dev < NIFACES; dev++) {
    if (ifptable[dev].count || ifptable[dev].ifp == axroute_default_ifp)
      printf("%c %-7s  %5d\n",
	     ifptable[dev].ifp == axroute_default_ifp ? '*' : ' ',
	     ifptable[dev].ifp->name,
	     ifptable[dev].count);
    total += ifptable[dev].count;
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doaxroute(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  static struct cmds routecmds[] = {

    "add",  dorouteadd,  0, 3, "ax25 route add [permanent] <interface> default|<path>",
    "list", doroutelist, 0, 0, NULLCHAR,
    "stat", doroutestat, 0, 0, NULLCHAR,

    NULLCHAR, NULLFP,    0, 0, NULLCHAR
  };

  axroute_loadfile();
  if (argc >= 2) return subcmd(routecmds, argc, argv, p);
  doroutestat(argc, argv, p);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Display AX.25 link level control blocks */

static int  doaxstatus(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  int  i;
  struct ax25_cb *cp;
  struct mbuf *bp;

  if (argc < 2) {
    printf("   &AXCB Rcv-Q Unack  Rt  Srtt  State          Remote socket\n");
    for (cp = axcb_head; cp; cp = cp->next)
      printf("%8lx %5u%c%3u/%u%c %2d%6lu  %-13s  %s\n",
	     (long) cp,
	     cp->rcvcnt,
	     cp->rnrsent ? '*' : ' ',
	     cp->unack,
	     cp->cwind,
	     cp->remote_busy ? '*' : ' ',
	     cp->retry,
	     cp->srtt,
	     ax25states[cp->state],
	     ax25hdr_to_string(&cp->hdr));
    if (axcb_server)
      printf("%8lx                        Listen (S)     *\n",
	     (long) axcb_server);
  } else {
    cp = (struct ax25_cb *) htol(argv[1]);
    if (!valid_ax(cp)) {
      printf("Not a valid control block address\n");
      return 1;
    }
    printf("Path:         %s\n", ax25hdr_to_string(&cp->hdr));
    printf("Interface:    %s\n", cp->ifp ? cp->ifp->name : "---");
    printf("State:        %s\n", (cp == axcb_server) ? "Listen (S)" : ax25states[cp->state]);
    if (cp->reason)
      printf("Reason:       %s\n", ax25reasons[cp->reason]);
    printf("Mode:         %s\n", (cp->mode == STREAM) ? "Stream" : "Dgram");
    printf("Closed:       %s\n", cp->closed ? "Yes" : "No");
    printf("Polling:      %s\n", cp->polling ? "Yes" : "No");
    printf("RNRsent:      %s\n", cp->rnrsent ? "Yes" : "No");
    printf("REJsent:      %s\n", cp->rejsent ? "Yes" : "No");
    if (cp->remote_busy)
      printf("Remote_busy:  %lu ms\n", msclock() - cp->remote_busy);
    else
      printf("Remote_busy:  No\n");
    printf("CWind:        %d\n", cp->cwind);
    printf("Retry:        %d\n", cp->retry);
    printf("Srtt:         %ld ms\n", cp->srtt);
    printf("Mean dev:     %ld ms\n", cp->mdev);
    printf("Timer T1:     ");
    if (run_timer(&cp->timer_t1))
      printf("%lu", read_timer(&cp->timer_t1));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&cp->timer_t1));
    printf("Timer T2:     ");
    if (run_timer(&cp->timer_t2))
      printf("%lu", read_timer(&cp->timer_t2));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&cp->timer_t2));
    printf("Timer T3:     ");
    if (run_timer(&cp->timer_t3))
      printf("%lu", read_timer(&cp->timer_t3));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&cp->timer_t3));
    printf("Timer T4:     ");
    if (run_timer(&cp->timer_t4))
      printf("%lu", read_timer(&cp->timer_t4));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&cp->timer_t4));
    printf("Timer T5:     ");
    if (run_timer(&cp->timer_t5))
      printf("%lu", read_timer(&cp->timer_t5));
    else
      printf("stop");
    printf("/%lu ms\n", dur_timer(&cp->timer_t5));
    printf("Rcv queue:    %d\n", cp->rcvcnt);
    if (cp->reseq[0].bp || cp->reseq[1].bp ||
	cp->reseq[2].bp || cp->reseq[3].bp ||
	cp->reseq[4].bp || cp->reseq[5].bp ||
	cp->reseq[6].bp || cp->reseq[7].bp) {
      printf("Reassembly queue:\n");
      for (i = next_seq(cp->vr); i != cp->vr; i = next_seq(i))
	if (cp->reseq[i].bp)
	  printf("              Seq %3d: %3d bytes\n",
		 i, len_p(cp->reseq[i].bp));
    }
    printf("Snd queue:    %d\n", len_p(cp->sndq));
    if (cp->resndq) {
      printf("Resend queue:\n");
      for (i = 0, bp = cp->resndq; bp; i++, bp = bp->anext)
	printf("              Seq %3d: %3d bytes\n",
	       (cp->vs - cp->unack + i) & 7, len_p(bp));
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set retransmission timer */

static int  dot1(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_t1init, "T1 (ms)", argc, argv, 1, 0x7fffffff);
}

/*---------------------------------------------------------------------------*/

/* Set acknowledgement delay timer */

static int  dot2(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_t2init, "T2 (ms)", argc, argv, 1, 0x7fffffff);
}

/*---------------------------------------------------------------------------*/

/* Set no-activity timer */

static int  dot3(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_t3init, "T3 (ms)", argc, argv, 0, 0x7fffffff);
}

/*---------------------------------------------------------------------------*/

/* Set busy timer */

static int  dot4(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_t4init, "T4 (ms)", argc, argv, 1, 0x7fffffff);
}

/*---------------------------------------------------------------------------*/

/* Set packet assembly timer */

static int  dot5(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_t5init, "T5 (ms)", argc, argv, 1, 0x7fffffff);
}

/*---------------------------------------------------------------------------*/

/* Set high water mark on receive queue that triggers RNR */

static int  doaxwindow(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  return setintrc(&ax_window, "Window", argc, argv, 1, MAXINT16);
}

/*---------------------------------------------------------------------------*/

/* Multiplexer for top-level ax25 command */

int  doax25(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  static struct cmds axcmds[] = {

    "digipeat", dodigipeat, 0, 0, NULLCHAR,
    "kick",     doaxkick,   0, 2, "ax25 kick <axcb>",
    "maxframe", domaxframe, 0, 0, NULLCHAR,
    "mycall",   domycall,   0, 0, NULLCHAR,
    "paclen",   dopaclen,   0, 0, NULLCHAR,
    "pthresh",  dopthresh,  0, 0, NULLCHAR,
    "reset",    doaxreset,  0, 2, "ax25 reset <axcb>",
    "retry",    doretry,    0, 0, NULLCHAR,
    "route",    doaxroute,  0, 0, NULLCHAR,
    "status",   doaxstatus, 0, 0, NULLCHAR,
    "t1",       dot1,       0, 0, NULLCHAR,
    "t2",       dot2,       0, 0, NULLCHAR,
    "t3",       dot3,       0, 0, NULLCHAR,
    "t4",       dot4,       0, 0, NULLCHAR,
    "t5",       dot5,       0, 0, NULLCHAR,
    "window",   doaxwindow, 0, 0, NULLCHAR,

    NULLCHAR,   NULLFP,     0, 0, NULLCHAR
  };

  return subcmd(axcmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/
/***************************** User Calls to AX25 ****************************/
/*---------------------------------------------------------------------------*/

struct ax25_cb *open_ax(path, mode, r_upcall, t_upcall, s_upcall, user)
char  *path;
int  mode;
void (*r_upcall) __ARGS((struct ax25_cb *p, int cnt));
void (*t_upcall) __ARGS((struct ax25_cb *p, int cnt));
void (*s_upcall) __ARGS((struct ax25_cb *p, int oldstate, int newstate));
char  *user;
{

  char  *ap;
  struct ax25 hdr;
  struct ax25_cb *cp;

  switch (mode) {
  case AX_ACTIVE:
    hdr.ndigis = hdr.nextdigi = 0;
    ap = path;
    addrcp(hdr.dest, ap);
    ap += AXALEN;
    addrcp(hdr.source, ap);
    ap += AXALEN;
    while (!(ap[-1] & E)) {
      addrcp(hdr.digis[hdr.ndigis++], ap);
      ap += AXALEN;
    }
    for (cp = axcb_head; cp; cp = cp->next)
      if (!cp->peer && addreq(hdr.dest, cp->hdr.dest)) {
	Net_error = CON_EXISTS;
	return NULLAXCB;
      }
    if (!(cp = create_axcb(NULLAXCB))) {
      Net_error = NO_MEM;
      return NULLAXCB;
    }
    build_path(cp, NULLIF, &hdr, 0);
    cp->r_upcall = r_upcall;
    cp->t_upcall = t_upcall;
    cp->s_upcall = s_upcall;
    cp->user = user;
    setaxstate(cp, CONNECTING);
    return cp;
  case AX_SERVER:
    if (!(cp = (struct ax25_cb *) calloc(1, sizeof(struct ax25_cb )))) {
      Net_error = NO_MEM;
      return NULLAXCB;
    }
    cp->r_upcall = r_upcall;
    cp->t_upcall = t_upcall;
    cp->s_upcall = s_upcall;
    cp->user = user;
    return cp;
  default:
    Net_error = INVALID;
    return NULLAXCB;
  }
}

/*---------------------------------------------------------------------------*/

int  send_ax(cp, bp)
struct ax25_cb *cp;
struct mbuf *bp;
{
  int cnt;

  if (!(cp && bp)) {
    free_p(bp);
    Net_error = INVALID;
    return (-1);
  }
  switch (cp->state) {
  case DISCONNECTED:
    free_p(bp);
    Net_error = NO_CONN;
    return (-1);
  case CONNECTING:
  case CONNECTED:
    if (!cp->closed) {
      if (cnt = len_p(bp)) {
	if (cp->mode == STREAM)
	  append(&cp->sndq, bp);
	else
	  enqueue(&cp->sndq, bp);
	cp->sndqtime = msclock();
	try_send(cp, 0);
      }
      return cnt;
    }
  case DISCONNECTING:
    free_p(bp);
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  space_ax(cp)
struct ax25_cb *cp;
{
  int  cnt;

  if (!cp) {
    Net_error = INVALID;
    return (-1);
  }
  switch (cp->state) {
  case DISCONNECTED:
    Net_error = NO_CONN;
    return (-1);
  case CONNECTING:
  case CONNECTED:
    if (!cp->closed) {
      cnt = (cp->cwind - cp->unack) * ax_paclen - len_p(cp->sndq);
      return (cnt > 0) ? cnt : 0;
    }
  case DISCONNECTING:
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  recv_ax(cp, bpp, cnt)
struct ax25_cb *cp;
struct mbuf **bpp;
int cnt;
{
  if (!(cp && bpp)) {
    Net_error = INVALID;
    return (-1);
  }
  if (cp->rcvcnt) {
    if (cp->mode == DGRAM || !cnt || cp->rcvcnt <= cnt) {
      *bpp = dequeue(&cp->rcvq);
      cnt = len_p(*bpp);
    } else {
      if (!(*bpp = alloc_mbuf(cnt))) {
	Net_error = NO_MEM;
	return (-1);
      }
      pullup(&cp->rcvq, (*bpp)->data, cnt);
      (*bpp)->cnt = cnt;
    }
    cp->rcvcnt -= cnt;
    if (cp->rnrsent && !busy(cp)) send_ack(cp, RESP);
    return cnt;
  }
  switch (cp->state) {
  case CONNECTING:
  case CONNECTED:
    *bpp = NULLBUF;
    Net_error = WOULDBLK;
    return (-1);
  case DISCONNECTED:
  case DISCONNECTING:
    *bpp = NULLBUF;
    return 0;
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  close_ax(cp)
struct ax25_cb *cp;
{
  if (!cp) {
    Net_error = INVALID;
    return (-1);
  }
  if (cp->closed) {
    Net_error = CON_CLOS;
    return (-1);
  }
  cp->closed = 1;
  switch (cp->state) {
  case DISCONNECTED:
    Net_error = NO_CONN;
    return (-1);
  case CONNECTING:
    setaxstate(cp, DISCONNECTED);
    return 0;
  case CONNECTED:
    if (!cp->sndq && !cp->unack) setaxstate(cp, DISCONNECTING);
    return 0;
  case DISCONNECTING:
    Net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  reset_ax(cp)
struct ax25_cb *cp;
{
  if (!cp) {
    Net_error = INVALID;
    return (-1);
  }
  if (cp == axcb_server) {
    free(axcb_server);
    axcb_server = NULLAXCB;
    return 0;
  }
  cp->reason = RESET;
  setaxstate(cp, DISCONNECTED);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  del_ax(cp)
struct ax25_cb *cp;
{

  int  i;
  struct ax25_cb *p, *q;

  for (q = 0, p = axcb_head; p != cp; q = p, p = p->next)
    if (!p) {
      Net_error = INVALID;
      return (-1);
    }
  if (q)
    q->next = p->next;
  else
    axcb_head = p->next;
  stop_timer(&cp->timer_t1);
  stop_timer(&cp->timer_t2);
  stop_timer(&cp->timer_t3);
  stop_timer(&cp->timer_t4);
  stop_timer(&cp->timer_t5);
  for (i = 0; i < 8; i++) free_p(cp->reseq[i].bp);
  free_q(&cp->rcvq);
  free_q(&cp->sndq);
  free_q(&cp->resndq);
  free(cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  valid_ax(cp)
struct ax25_cb *cp;
{
  struct ax25_cb *p;

  if (!cp) return 0;
  if (cp == axcb_server) return 1;
  for (p = axcb_head; p; p = p->next)
    if (p == cp) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Force a retransmission */

int  kick_ax(axp)
struct ax25_cb *axp;
{
  if (!valid_ax(axp)) return -1;
  t1_timeout(axp);
  return 0;
}

