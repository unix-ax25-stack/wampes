#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "global.h"
#include "netuser.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "axproto.h"
#include "cmdparse.h"
#include "asy.h"

extern int  ax_output();
extern int  debug;
extern long  currtime;
extern struct axcb *netrom_server_axcb;
extern void free();

#define AXROUTEHOLDTIME (60l*60*24*90)
#define AXROUTESAVETIME (60l*10)
#define AXROUTESIZE     499

struct axroute_tab {
  struct ax25_addr call;
  struct axroute_tab *digi;
  struct interface *ifp;
  int  perm;
  long  time;
  struct axroute_tab *next;
};

struct axroutesaverecord {
  struct ax25_addr call, digi;
  short  dev;
  long  time;
};

char  *ax25states[] = {
  "Disconnected",
  "Connecting",
  "Connected",
  "Disconnecting"
};

char  *ax25reasons[] = {
  "Normal",
  "Reset",
  "Timeout",
  "Network"
};

int  ax_maxframe =    1;        /* Transmit flow control level */
int  ax_paclen   =  236;        /* Maximum outbound packet size */
int  ax_retry    =   10;        /* Retry limit */
int  ax_t1init   =    5;        /* Retransmission timeout */
int  ax_t2init   =    2;        /* Acknowledgement delay timeout */
int  ax_t3init   =  600;        /* No-activity timeout */
int  ax_t4init   =   60;        /* Busy timeout */
int  ax_window   = 1024;        /* Local flow control limit */
struct axcb *axcb_server;       /* Server control block */

static char  axroutefile[] = "/tcp/axroute_data";
static char  axroutetmpfile[] = "/tcp/axroute_tmp";
static struct axcb *axcb_head;
static struct axroute_tab *axroute_tab[AXROUTESIZE];
static struct interface *axroute_default_ifp;

/*---------------------------------------------------------------------------*/

static int  axroute_hash(call)
register char  *call;
{
  register long  hashval;

  hashval =                  (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | ((*call >> 1) & 0xf);
  return hashval % AXROUTESIZE;
}

/*---------------------------------------------------------------------------*/

static struct axroute_tab *axroute_tabptr(call, create)
struct ax25_addr *call;
int  create;
{

  int  hashval;
  register struct axroute_tab *rp;

  hashval = axroute_hash(call);
  for (rp = axroute_tab[hashval]; rp && !addreq(&rp->call, call); rp = rp->next) ;
  if (!rp && create) {
    rp = (struct axroute_tab *) calloc(1, sizeof(struct axroute_tab ));
    rp->call = *call;
    rp->next = axroute_tab[hashval];
    axroute_tab[hashval] = rp;
  }
  return rp;
}

/*---------------------------------------------------------------------------*/

static void axroute_savefile()
{

  FILE * fp;
  register int  i;
  register struct axroute_tab *rp, *lp;
  static long  nextsavetime;
  struct axroutesaverecord buf;

  if (!nextsavetime) nextsavetime = currtime + AXROUTESAVETIME;
  if (debug || nextsavetime > currtime) return;
  nextsavetime = currtime + AXROUTESAVETIME;
  if (!(fp = fopen(axroutetmpfile, "w"))) return;
  for (i = 0; i < AXROUTESIZE; i++)
    for (lp = 0, rp = axroute_tab[i]; rp; )
      if (rp->time + AXROUTEHOLDTIME >= currtime) {
	buf.call = rp->call;
	if (rp->digi)
	  buf.digi = rp->digi->call;
	else
	  buf.digi.call[0] = '\0';
	buf.dev = rp->ifp ? rp->ifp->dev : -1;
	buf.time = rp->time;
	fwrite((char *) &buf, sizeof(buf), 1, fp);
	lp = rp;
	rp = rp->next;
      } else if (lp) {
	lp->next = rp->next;
	free((char *) rp);
	rp = lp->next;
      } else {
	axroute_tab[i] = rp->next;
	free((char *) rp);
	rp = axroute_tab[i];
      }
  fclose(fp);
  rename(axroutetmpfile, axroutefile);
}

/*---------------------------------------------------------------------------*/

static void axroute_loadfile()
{

  FILE * fp;
  static int  done;
  struct axroute_tab *rp;
  struct axroutesaverecord buf;
  struct interface *ifp, *ifptable[ASY_MAX];

  if (done) return;
  done = 1;
  if (debug || !(fp = fopen(axroutefile, "r"))) return;
  memset((char *) ifptable, 0, sizeof(ifptable));
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
  while (fread((char *) & buf, sizeof(buf), 1, fp)) {
    if (buf.time + AXROUTEHOLDTIME < currtime) continue;
    rp = axroute_tabptr(&buf.call, 1);
    if (buf.digi.call[0]) rp->digi = axroute_tabptr(&buf.digi, 1);
    if (buf.dev >= 0 && buf.dev < ASY_MAX) rp->ifp = ifptable[buf.dev];
    rp->time = buf.time;
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void axroute_add(cp, perm)
struct axcb *cp;
int  perm;
{

  int  ncalls = 0;
  register char  *ap;
  register int  i;
  register struct axroute_tab *rp;
  struct ax25_addr calls[20];
  struct axroute_tab *lastnode = 0;

  for (ap = cp->path + AXALEN; !ismycall(axptr(ap)); ap += AXALEN) ;
  do {
    ap += AXALEN;
    if (ap >= cp->path + cp->pathlen) ap = cp->path;
    if (!*ap || ismycall(axptr(ap))) return;
    for (i = 0; i < ncalls; i++)
      if (addreq(calls + i, axptr(ap))) return;
    calls[ncalls++] = *axptr(ap);
  } while (ap != cp->path);

  for (i = 0; i < ncalls; i++) {
    rp = axroute_tabptr(calls + i, 1);
    if (perm || !rp->perm) {
      if (lastnode) {
	rp->digi = lastnode;
	rp->ifp = 0;
      } else {
	rp->digi = 0;
	rp->ifp = cp->ifp;
      }
      rp->perm = perm;
    }
    rp->time = currtime;
    lastnode = rp;
  }
  axroute_savefile();
}

/*---------------------------------------------------------------------------*/

int  axroute(cp, bp)
struct axcb *cp;
struct mbuf *bp;
{

  register char  *dest;
  register struct axroute_tab *rp;
  struct interface *ifp;

  if (cp && cp->ifp)
    ifp = cp->ifp;
  else {
    if (bp->data[AXALEN + 6] & E)
      dest = bp->data;
    else
      for (dest = bp->data + 2 * AXALEN; ; dest += AXALEN) {
	if (!(dest[6] & REPEATED)) break;
	if (dest[6] & E) {
	  dest = bp->data;
	  break;
	}
      }
    rp = axroute_tabptr(axptr(dest), 0);
    ifp = (rp && rp->ifp) ? rp->ifp : axroute_default_ifp;
  }
  if (ifp) {
    if (ifp->forw) ifp = ifp->forw;
    (*ifp->raw)(ifp, bp);
  } else
    free_p(bp);
}

/*---------------------------------------------------------------------------*/

static void reset_t1(cp)
struct axcb *cp;
{
  cp->timer_t1.start = (cp->srtt + 2 * cp->mdev + MSPTICK) / MSPTICK;
  if (cp->timer_t1.start < 2) cp->timer_t1.start = 2;
}

/*---------------------------------------------------------------------------*/

static void inc_t1(cp)
struct axcb *cp;
{
  int32 tmp;

  if (tmp = cp->timer_t1.start / 4)
    cp->timer_t1.start += tmp;
  else
    cp->timer_t1.start++;
  tmp = (10 * cp->srtt + MSPTICK) / MSPTICK;
  if (cp->timer_t1.start > tmp) cp->timer_t1.start = tmp;
  if (cp->timer_t1.start < 2) cp->timer_t1.start = 2;
}

/*---------------------------------------------------------------------------*/

static void send_packet(cp, type, cmdrsp, data)
struct axcb *cp;
int  type;
int  cmdrsp;
struct mbuf *data;
{

  char  *p;
  int  control;
  struct mbuf *bp;

  if (!(bp = alloc_mbuf(cp->pathlen + 2))) {
    free_p(data);
    return;
  }
  p = bp->data;
  memcpy(p, cp->path, cp->pathlen);
  if (cmdrsp & DST_C) p[6] |= C;
  if (cmdrsp & SRC_C) p[AXALEN+6] |= C;
  p += cp->pathlen;
  control = type;
  if (type == I) control |= (cp->vs << 1);
  if ((type & 3) != U) {
    control |= (cp->vr << 5);
    stop_timer(&cp->timer_t2);
  }
  if (cmdrsp & PF) control |= PF;
  *p++ = control;
  if (cp->mode == STREAM && (type == I || type == UI))
    *p++ = (PID_FIRST | PID_LAST | PID_NO_L3);
  if (type == RR || type == REJ || type == UA) cp->rnrsent = 0;
  if (type == RNR) cp->rnrsent = 1;
  if (type == REJ) cp->rejsent = 1;
  if (cmdrsp == POLL) cp->polling = 1;
  if (type == I || cmdrsp == POLL) start_timer(&cp->timer_t1);
  bp->cnt = p - bp->data;
  bp->next = data;
  axroute(cp, bp);
}

/*---------------------------------------------------------------------------*/

static int  busy(cp)
register struct axcb *cp;
{
  return cp->rcvcnt >= ax_window ||
	 cp->peer && (cp->peer->state != CONNECTED ||
		      cp->peer->sndcnt >= ax_window);
}

/*---------------------------------------------------------------------------*/

static void send_ack(cp, cmdrsp)
struct axcb *cp;
int  cmdrsp;
{
  if (busy(cp))
    send_packet(cp, RNR, cmdrsp, NULLBUF);
  else if (cp->wrongseq && !cp->rejsent)
    send_packet(cp, REJ, cmdrsp, NULLBUF);
  else
    send_packet(cp, RR, cmdrsp, NULLBUF);
}

/*---------------------------------------------------------------------------*/

static void try_send(cp, fill_sndq)
register struct axcb *cp;
int  fill_sndq;
{
  struct mbuf *bp;

  if (cp->state != CONNECTED || cp->remote_busy || run_timer(&cp->timer_t1))
    return;
  if (fill_sndq && cp->t_upcall && !cp->segsize && cp->sndcnt < ax_paclen && !cp->closed)
    (*cp->t_upcall)(cp, ax_paclen - cp->sndcnt);
  if (!cp->segsize)
    cp->segsize = dup_p(&bp, cp->sndq, 0, (cp->mode == STREAM) ? ax_paclen : MAXINT16);
  else
    dup_p(&bp, cp->sndq, 0, cp->segsize);
  if (bp) send_packet(cp, I, CMD, bp);
}

/*---------------------------------------------------------------------------*/

static void setaxstate(cp, newstate)
struct axcb *cp;
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
struct axcb *cp;
{
  struct mbuf *bp;

  inc_t1(cp);
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
    if (cp->retry > ax_retry)
      setaxstate(cp, DISCONNECTING);
    else if (!cp->remote_busy && cp->retry <= 1 && cp->segsize && cp->segsize <= 64) {
      dup_p(&bp, cp->sndq, 0, cp->segsize);
      send_packet(cp, I, POLL, bp);
    } else
      send_ack(cp, POLL);
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
struct axcb *cp;
{
  send_ack(cp, RESP);
}

/*---------------------------------------------------------------------------*/

static void t3_timeout(cp)
struct axcb *cp;
{
  if (cp->state == CONNECTED && !run_timer(&cp->timer_t1)) send_ack(cp, POLL);
}

/*---------------------------------------------------------------------------*/

static void t4_timeout(cp)
struct axcb *cp;
{
  if (!cp->polling) send_ack(cp, POLL);
}

/*---------------------------------------------------------------------------*/

static void build_path(cp, ifp, newpath, reverse)
register struct axcb *cp;
struct interface *ifp;
char  *newpath;
int  reverse;
{

  char  buf[10*AXALEN];
  int  len, ndigi;
  register char  *ap, *tp, *myaddr;
  register struct axroute_tab *rp;

  /*** find address length and copy address into control block ***/

  for (ap = newpath; !(ap[6] & E); ap += AXALEN) ;
  cp->pathlen = ap - newpath + AXALEN;
  if (reverse) {
    memcpy(cp->path, newpath + AXALEN, AXALEN);
    memcpy(cp->path + AXALEN, newpath, AXALEN);
    for (tp = cp->path + 2 * AXALEN;
	 tp < cp->path + cp->pathlen;
	 tp += AXALEN, ap -= AXALEN)
      memcpy(tp, ap, AXALEN);
  } else
    memcpy(cp->path, newpath, cp->pathlen);

  /*** store interface pointer into control block ***/

  cp->ifp = ifp;

  /*** save address for auto routing ***/

  if (reverse && ifp) axroute_add(cp, 0);

  /*** find my digipeater address (use the last one) ***/

  myaddr = NULLCHAR;
  for (ap = cp->path + 2 * AXALEN; ap < cp->path + cp->pathlen; ap += AXALEN)
    if (ismycall(axptr(ap))) myaddr = ap;

  /*** autorouting ***/

  if (!reverse) {

    /*** remove all digipeaters before me ***/

    if (myaddr && myaddr > cp->path + 2 * AXALEN) {
      len = (cp->path + cp->pathlen) - myaddr;
      memcpy(buf, myaddr, len);
      myaddr = cp->path + 2 * AXALEN;
      memcpy(myaddr, buf, len);
      cp->pathlen = 2 * AXALEN + len;
    }

    /*** add necessary digipeaters and find interface ***/

    ap = myaddr ? myaddr + AXALEN : cp->path + 2 * AXALEN;
    rp = axroute_tabptr(axptr((ap >= cp->path + cp->pathlen) ? cp->path : ap), 0);
    for (; rp; rp = rp->digi) {
      if (rp->digi && cp->pathlen < sizeof(cp->path)) {
	len = (cp->path + cp->pathlen) - ap;
	if (len) memcpy(buf, ap, len);
	memcpy(ap, &rp->digi->call, AXALEN);
	if (len) memcpy(ap + AXALEN, buf, len);
	cp->pathlen += AXALEN;
      }
      cp->ifp = rp->ifp;
    }
    if (!cp->ifp) cp->ifp = axroute_default_ifp;
  }

  /*** clear all address bits ***/

  for (ap = cp->path; ap < cp->path + cp->pathlen; ap += AXALEN)
    ap[6] = (ap[6] & SSID) | 0x60;

  /*** set REPEATED bits for all digipeaters before and including me ***/

  if (myaddr)
    for (ap = cp->path + 2 * AXALEN; ap <= myaddr; ap += AXALEN)
      ap[6] |= REPEATED;

  /*** mark end of address field ***/

  cp->path[cp->pathlen-1] |= E;

  /*** estimate round trip time ***/

  if (myaddr)
    ndigi = (cp->path + cp->pathlen - myaddr) / AXALEN - 1;
  else
    ndigi = cp->pathlen / AXALEN - 2;
  cp->srtt = 500l * ax_t1init * (1 + 2 * ndigi);
  reset_t1(cp);
}

/*---------------------------------------------------------------------------*/

char  *pathtostr(cp)
struct axcb *cp;
{

  register char  *ap, *p;
  static char  buf[128];

  if (!cp->pathlen) return "*";
  p = buf;
  ap = cp->path + AXALEN;
  if (!ismycall(axptr(ap))) {
    pax25(p, axptr(ap));
    while (*p) p++;
    *p++ = '-';
    *p++ = '>';
  }
  pax25(p, axptr(cp->path));
  while (*p) p++;
  while (!(ap[6] & E)) {
    ap += AXALEN;
    *p++ = ',';
    pax25(p, axptr(ap));
    while (*p) p++;
    if (ap[6] & REPEATED) *p++ = '*';
  }
  *p = '\0';
  return buf;
}

/*---------------------------------------------------------------------------*/

#define next_seq(n)  (((n) + 1) & 7)

/*---------------------------------------------------------------------------*/

int  axproto_recv(ifp, bp)
struct interface *ifp;
struct mbuf *bp;
{

  char  *cntrlptr;
  int  cmdrsp;
  int  control;
  int  for_me;
  int  nr;
  int  ns;
  int  type;
  int32 abserr;
  int32 rtt;
  struct axcb *cp;
  struct axcb *cpp;

  for (cntrlptr = bp->data + AXALEN; !(cntrlptr[6] & E); cntrlptr += AXALEN) ;
  cntrlptr += AXALEN;
  control = uchar(*cntrlptr);
  if (control & 1) {
    if (control & 2)
      type = control & ~PF;
    else
      type = control & 0xf;
  } else
    type = I;
  for_me = ismycall(axptr(bp->data));

  if (!for_me && (type == UI || addreq(axptr(bp->data), axptr(bp->data + AXALEN)))) {
    axroute(NULLAXCB, bp);
    return;
  }

  if ((bp->data[6] & C) == (bp->data[AXALEN+6] & C))
    cmdrsp = VERS1;
  else
    cmdrsp = ((bp->data[6] & C) ? DST_C : SRC_C) | (control & PF);

  for (cp = axcb_head; cp; cp = cp->next)
    if (addreq(axptr(bp->data + AXALEN), axptr(cp->path)) && addreq(axptr(bp->data), axptr(cp->path + AXALEN))) break;
  if (!cp) {
    cp = (struct axcb *) calloc(1, sizeof(struct axcb ));
    if (for_me) {
      if (netrom_server_axcb && isnetrom(axptr(bp->data + AXALEN)))
	memcpy(cp, netrom_server_axcb, sizeof(struct axcb ));
      else if (axcb_server)
	memcpy(cp, axcb_server, sizeof(struct axcb ));
    }
    build_path(cp, ifp, bp->data, 1);
    cp->timer_t1.func = t1_timeout;
    cp->timer_t1.arg = (char *) cp;
    cp->timer_t2.func = t2_timeout;
    cp->timer_t2.arg = (char *) cp;
    cp->timer_t3.func = t3_timeout;
    cp->timer_t3.arg = (char *) cp;
    cp->timer_t4.func = t4_timeout;
    cp->timer_t4.arg = (char *) cp;
    cp->next = axcb_head;
    axcb_head = cp;
    if (!for_me) {
      cp->peer = cpp = (struct axcb *) calloc(1, sizeof(struct axcb ));
      build_path(cpp, NULLIF, bp->data, 0);
      cpp->timer_t1.func = t1_timeout;
      cpp->timer_t1.arg = (char *) cpp;
      cpp->timer_t2.func = t2_timeout;
      cpp->timer_t2.arg = (char *) cpp;
      cpp->timer_t3.func = t3_timeout;
      cpp->timer_t3.arg = (char *) cpp;
      cpp->timer_t4.func = t4_timeout;
      cpp->timer_t4.arg = (char *) cpp;
      cpp->next = axcb_head;
      axcb_head = cpp;
      cpp->peer = cp;
    } else
      cpp = NULLAXCB;
  } else
    cpp = cp->peer;

  if (type == SABM) {
    build_path(cp, ifp, bp->data, 1);
    cp->polling = 0;
    cp->rnrsent = 0;
    cp->rejsent = 0;
    cp->wrongseq = 0;
    cp->remote_busy = 0;
    cp->vr = 0;
    cp->vs = 0;
    cp->retry = 0;
  }

  if (cp->mode == STREAM && type == I && uchar(cntrlptr[1]) != (PID_FIRST | PID_LAST | PID_NO_L3)) {
    cp->mode = DGRAM;
    if (cpp) cpp->mode = DGRAM;
  }

  cp->timer_t3.start = ax_t3init;
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
	build_path(cpp, NULLIF, bp->data, 0);
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
      nr = control >> 5;
      if (cp->segsize && nr == next_seq(cp->vs)) {
	if (cp->mode == STREAM)
	  pullup(&cp->sndq, NULLCHAR, cp->segsize);
	else
	  cp->sndq = free_p(cp->sndq);
	cp->sndcnt -= cp->segsize;
	cp->segsize = 0;
	cp->vs = next_seq(cp->vs);
	if (cpp && cpp->rnrsent && !busy(cpp)) send_ack(cpp, RESP);
	if (!cp->polling) {
	  stop_timer(&cp->timer_t1);
	  cp->retry = 0;
	  rtt = (cp->timer_t1.start - cp->timer_t1.count) * (long) MSPTICK;
	  abserr = (rtt > cp->srtt) ? rtt - cp->srtt : cp->srtt - rtt;
	  cp->srtt = ((AGAIN - 1) * cp->srtt + rtt) / AGAIN;
	  cp->mdev = ((DGAIN - 1) * cp->mdev + abserr) / DGAIN;
	  reset_t1(cp);
	}
      }
      if (type == I) {
	if (for_me &&
	    uchar(cntrlptr[1]) == (PID_NETROM | PID_FIRST | PID_LAST) &&
	    cp->r_upcall != netrom_server_axcb->r_upcall) {
	  new_neighbor(axptr(bp->data + AXALEN));
	  setaxstate(cp, DISCONNECTING);
	  free_p(bp);
	  return;
	}
	ns = (control >> 1) & 7;
	cp->wrongseq = (ns != cp->vr);
	if (!cp->wrongseq) {
	  cp->vr = next_seq(cp->vr);
	  cp->rejsent = 0;
	  pullup(&bp, NULLCHAR, cntrlptr - bp->data + 1 + (cp->mode == STREAM));
	  if (bp) {
	    if (for_me) {
	      cp->rcvcnt += len_mbuf(bp);
	      if (cp->mode == STREAM)
		append(&cp->rcvq, bp);
	      else
		enqueue(&cp->rcvq, bp);
	    } else
	      send_ax(cpp, bp);
	    bp = NULLBUF;
	  }
	}
	if (cmdrsp == POLL)
	  send_ack(cp, FINAL);
	else {
	  cp->timer_t2.start = ax_t2init;
	  start_timer(&cp->timer_t2);
	}
	if (cp->r_upcall && cp->rcvcnt) (*cp->r_upcall)(cp, cp->rcvcnt);
      } else {
	if (cp->polling && cmdrsp == FINAL) {
	  stop_timer(&cp->timer_t1);
	  cp->retry = 0;
	  cp->polling = 0;
	}
	if (type == RNR) {
	  if (!cp->remote_busy) cp->remote_busy = currtime;
	  stop_timer(&cp->timer_t1);
	  cp->timer_t4.start = ax_t4init;
	  start_timer(&cp->timer_t4);
	} else {
	  cp->remote_busy = 0;
	  stop_timer(&cp->timer_t4);
	}
	if (cmdrsp == POLL) send_ack(cp, FINAL);
      }
      try_send(cp, 1);
      if (cp->closed && !cp->sndcnt ||
	  cp->remote_busy && cp->remote_busy + 900l < currtime)
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
}

/*---------------------------------------------------------------------------*/
/******************************* AX25 Commands *******************************/
/*---------------------------------------------------------------------------*/

/* Control AX.25 digipeating */

static int  dodigipeat(argc, argv)
int  argc;
char  *argv[];
{
  extern int  digipeat;

  if (argc < 2)
    printf("Digipeat %d\n", digipeat);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 0 || tmp > 2) return (-1);
    digipeat = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set maximum number of frames that will be allowed in flight */

static domaxframe(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("Maxframe %d\n", ax_maxframe);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 1 || tmp > 7) return (-1);
    ax_maxframe = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Display or change our AX.25 address */

static int  domycall(argc, argv)
int  argc;
char  *argv[];
{
  char  buf[15];

  if (argc < 2) {
    pax25(buf, &mycall);
    printf("Mycall %s\n", buf);
  } else {
    if (setcall(&mycall, argv[1]) == -1) return (-1);
    mycall.ssid |= E;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set maximum length of I-frame data field */

static int  dopaclen(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("Paclen %d\n", ax_paclen);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 1) return (-1);
    ax_paclen = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Eliminate a AX25 connection */

static int  doaxreset(argc, argv)
int  argc;
char  *argv[];
{
  struct axcb *cp;
  extern char  notval[];
  long  htol();

  cp = (struct axcb *) htol(argv[1]);
  if (!valid_ax(cp)) {
    printf(notval);
    return 1;
  }
  reset_ax(cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set retry limit count */

static int  doretry(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("Retry %d\n", ax_retry);
  else
    ax_retry = atoi(argv[1]);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  dorouteadd(argc, argv)
int  argc;
char  *argv[];
{

  char  *p;
  int  perm;
  struct axcb cb;
  struct interface *if_lookup();

  argc--;
  argv++;

  if (perm = !strcmp(*argv, "permanent")) {
    argc--;
    argv++;
  }

  if (!(cb.ifp = if_lookup(*argv))) {
    printf("Interface \"%s\" unknown\n", *argv);
    return 1;
  }
  if (cb.ifp->output != ax_output) {
    printf("Interface \"%s\" not kiss\n", *argv);
    return 1;
  }
  argc--;
  argv++;
  if (!strcmp(*argv, "default")) {
    axroute_default_ifp = cb.ifp;
    return 0;
  }
  for (p = cb.path; argc > 0; argc--, argv++) {
    if (!strncmp("via", *argv, strlen(*argv))) continue;
    if (p >= cb.path + sizeof(cb.path)) {
      printf("Too many digipeaters (max 8)\n");
      return 1;
    }
    if (setcall(axptr(p), *argv)) {
      printf("Invalid call \"%s\"\n", *argv);
      return 1;
    }
    if (p == cb.path) {
      p += AXALEN;
      memcpy(p, (char *) &mycall, AXALEN);
      p[6] &= ~E;
    }
    p += AXALEN;
  }
  if (p < cb.path + 2 * AXALEN) {
    printf("Missing call\n");
    return 1;
  }
  p[-1] |= E;
  cb.pathlen = p - cb.path;
  axroute_add(&cb, perm);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void doroutelistentry(rp)
struct axroute_tab *rp;
{

  char  *cp, buf[1024];
  int  i, n;
  int  perm;
  struct axroute_tab *rp_stack[20];
  struct interface *ifp;
  struct tm *tm;

  tm = gmtime(&rp->time);
  pax25(cp = buf, &rp->call);
  perm = rp->perm;
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    ifp = rp->ifp;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, &(rp_stack[i]->call));
  }
  printf("%2d-%.3s  %-9s  %c %s\n",
	 tm->tm_mday,
	 "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	 ifp ? ifp->name : "???",
	 perm ? '*' : ' ',
	 buf);
}

/*---------------------------------------------------------------------------*/

static int  doroutelist(argc, argv)
int  argc;
char  *argv[];
{

  int  i;
  struct ax25_addr call;
  struct axroute_tab *rp;

  puts("Date    Interface  P Path");
  if (argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = axroute_tab[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if (setcall(&call, *argv) || !(rp = axroute_tabptr(&call, 0)))
      printf("*** Not in table *** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doroutestat()
{

  int  count[ASY_MAX], total;
  register int  i, dev;
  register struct axroute_tab *rp, *dp;
  struct interface *ifp, *ifptable[ASY_MAX];

  memset((char *) ifptable, 0, sizeof(ifptable));
  memset((char *) count, 0, sizeof(count));
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
  for (i = 0; i < AXROUTESIZE; i++)
    for (rp = axroute_tab[i]; rp; rp = rp->next) {
      for (dp = rp; dp->digi; dp = dp->digi) ;
      if (dp->ifp) count[dp->ifp->dev]++;
    }
  puts("Interface  Count");
  total = 0;
  for (dev = 0; dev < ASY_MAX && ifptable[dev]; dev++) {
    if (ifptable[dev] == axroute_default_ifp || count[dev])
      printf("%c %-7s  %5d\n", ifptable[dev] == axroute_default_ifp ? '*' : ' ', ifptable[dev]->name, count[dev]);
    total += count[dev];
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doroute(argc, argv)
int  argc;
char  *argv[];
{

  static struct cmds routecmds[] = {

    "add",    dorouteadd,  3, "ax25 route add [permanent] <interface> <path>",
								   NULLCHAR,
    "list",   doroutelist, 0, NULLCHAR,                            NULLCHAR,
    "stat",   doroutestat, 0, NULLCHAR,                            NULLCHAR,

    NULLCHAR, NULLFP,      0, NULLCHAR,                            NULLCHAR
  };

  axroute_loadfile();
  if (argc >= 2) return subcmd(routecmds, argc, argv);
  doroutestat();
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Display AX.25 link level control blocks */

static int  dostatus(argc, argv)
int  argc;
char  *argv[];
{
  register struct axcb *cp;

  if (argc < 2) {
    printf("   &AXCB Rcv-Q Snd-Q  Rt  Srtt  State          Remote socket\n");
    for (cp = axcb_head; cp; cp = cp->next)
      printf("%8lx %5u%c%5u%c %2d %5.1f  %-13s  %s\n",
	     (long) cp,
	     cp->rcvcnt,
	     cp->rnrsent ? '*' : ' ',
	     cp->sndcnt,
	     cp->remote_busy ? '*' : ' ',
	     cp->retry,
	     cp->srtt / 1000.0,
	     ax25states[cp->state],
	     pathtostr(cp));
    if (axcb_server)
      printf("%8lx                        Listen (S)     *\n",
	     (long) axcb_server);
  } else {
    cp = (struct axcb *) htol(argv[1]);
    if (!valid_ax(cp)) {
      printf("Not a valid control block address\n");
      return 1;
    }
    printf("Path:         %s\n", pathtostr(cp));
    printf("Interface:    %s\n", cp->ifp ? cp->ifp->name : "---");
    printf("State:        %s\n", (cp == axcb_server) ? "Listen (S)" : ax25states[cp->state]);
    if (cp->reason)
      printf("Reason:       %s\n", ax25reasons[cp->reason]);
    printf("Mode:         %s\n", (cp->mode == STREAM) ? "Stream" : "Dgram");
    printf("Closed:       %s\n", cp->closed ? "Yes" : "No");
    printf("Polling:      %s\n", cp->polling ? "Yes" : "No");
    printf("RNRsent:      %s\n", cp->rnrsent ? "Yes" : "No");
    printf("REJsent:      %s\n", cp->rejsent ? "Yes" : "No");
    printf("Wrongseq:     %s\n", cp->wrongseq ? "Yes" : "No");
    if (cp->remote_busy)
      printf("Remote_busy:  %ld sec\n", currtime - cp->remote_busy);
    else
      printf("Remote_busy:  No\n");
    printf("Retry:        %d\n", cp->retry);
    printf("Srtt:         %ld ms\n", cp->srtt);
    printf("Mean dev:     %ld ms\n", cp->mdev);
    if (run_timer(&cp->timer_t1))
      printf("Timer T1:     %ld/%ld sec\n",
	     cp->timer_t1.start - cp->timer_t1.count, cp->timer_t1.start);
    else
      printf("Timer T1:     Stopped\n");
    if (run_timer(&cp->timer_t2))
      printf("Timer T2:     %ld/%ld sec\n",
	      cp->timer_t2.start - cp->timer_t2.count, cp->timer_t2.start);
    else
      printf("Timer T2:     Stopped\n");
    if (run_timer(&cp->timer_t3))
      printf("Timer T3:     %ld/%ld sec\n",
	     cp->timer_t3.start - cp->timer_t3.count, cp->timer_t3.start);
    else
      printf("Timer T3:     Stopped\n");
    if (run_timer(&cp->timer_t4))
      printf("Timer T4:     %ld/%ld sec\n",
	     cp->timer_t4.start - cp->timer_t4.count, cp->timer_t4.start);
    else
      printf("Timer T4:     Stopped\n");
    printf("Rcvcnt:       %d\n", cp->rcvcnt);
    printf("Sndcnt:       %d\n", cp->sndcnt);
    printf("Segsize:      %d\n", cp->segsize);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set retransmission timer */

static int  dot1(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("T1 %d sec\n", ax_t1init);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 1 || tmp > 15) return (-1);
    ax_t1init = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set acknowledgement delay timer */

static int  dot2(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("T2 %d sec\n", ax_t2init);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 1) return (-1);
    ax_t2init = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set no-activity timer */

static int  dot3(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("T3 %d sec\n", ax_t3init);
  else
    ax_t3init = atoi(argv[1]);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set busy timer */

static int  dot4(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("T4 %d sec\n", ax_t4init);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < 1) return (-1);
    ax_t4init = tmp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set high water mark on receive queue that triggers RNR */

static int  dowindow(argc, argv)
int  argc;
char  *argv[];
{
  if (argc < 2)
    printf("Window %d\n", ax_window);
  else
    ax_window = atoi(argv[1]);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Multiplexer for top-level ax25 command */

int  doax25(argc, argv)
int  argc;
char  *argv[];
{

  int  doidigi();

  static struct cmds axcmds[] = {

    "digipeat", dodigipeat, 0, NULLCHAR, "digipeat must be 0..2",
    "idigi",    doidigi,    0, NULLCHAR, "Usage: ax25 idigi <interface> <call>",
    "maxframe", domaxframe, 0, NULLCHAR, "maxframe must be 1..7",
    "mycall",   domycall,   0, NULLCHAR, NULLCHAR,
    "paclen",   dopaclen,   0, NULLCHAR, "paclen must be > 0",
    "reset",    doaxreset,  2, "ax25 reset <axcb>", NULLCHAR,
    "retry",    doretry,    0, NULLCHAR, NULLCHAR,
    "route",    doroute,    0, NULLCHAR, NULLCHAR,
    "status",   dostatus,   0, NULLCHAR, NULLCHAR,
    "t1",       dot1,       0, NULLCHAR, "t1 must be 1..15",
    "t2",       dot2,       0, NULLCHAR, "t2 must be > 0",
    "t3",       dot3,       0, NULLCHAR, NULLCHAR,
    "t4",       dot4,       0, NULLCHAR, "t4 must be > 0",
    "window",   dowindow,   0, NULLCHAR, NULLCHAR,

    NULLCHAR,   NULLFP,     0, NULLCHAR, NULLCHAR
  };

  return subcmd(axcmds, argc, argv);
}

/*---------------------------------------------------------------------------*/
/***************************** User Calls to AX25 ****************************/
/*---------------------------------------------------------------------------*/

struct axcb *open_ax(path, mode, r_upcall, t_upcall, s_upcall, user)
char  *path;
int  mode;
void (*r_upcall)();
void (*t_upcall)();
void (*s_upcall)();
char  *user;
{
  register struct axcb *cp;

  switch (mode) {
  case AX25_ACTIVE:
    for (cp = axcb_head; cp; cp = cp->next)
      if (!cp->peer && addreq(axptr(path), axptr(cp->path))) {
	net_error = CON_EXISTS;
	return NULLAXCB;
      }
    if (!(cp = (struct axcb *) calloc(1, sizeof(struct axcb )))) {
      net_error = NO_SPACE;
      return NULLAXCB;
    }
    build_path(cp, NULLIF, path, 0);
    cp->timer_t1.func = t1_timeout;
    cp->timer_t1.arg = (char *) cp;
    cp->timer_t2.func = t2_timeout;
    cp->timer_t2.arg = (char *) cp;
    cp->timer_t3.func = t3_timeout;
    cp->timer_t3.arg = (char *) cp;
    cp->timer_t4.func = t4_timeout;
    cp->timer_t4.arg = (char *) cp;
    cp->r_upcall = r_upcall;
    cp->t_upcall = t_upcall;
    cp->s_upcall = s_upcall;
    cp->user = user;
    cp->next = axcb_head;
    axcb_head = cp;
    setaxstate(cp, CONNECTING);
    return cp;
  case AX25_SERVER:
    if (!(cp = (struct axcb *) calloc(1, sizeof(struct axcb )))) {
      net_error = NO_SPACE;
      return NULLAXCB;
    }
    cp->r_upcall = r_upcall;
    cp->t_upcall = t_upcall;
    cp->s_upcall = s_upcall;
    cp->user = user;
    return cp;
  default:
    net_error = INVALID;
    return NULLAXCB;
  }
}

/*---------------------------------------------------------------------------*/

int  send_ax(cp, bp)
struct axcb *cp;
struct mbuf *bp;
{
  int16 cnt;

  if (!(cp && bp)) {
    free_p(bp);
    net_error = INVALID;
    return (-1);
  }
  switch (cp->state) {
  case DISCONNECTED:
    free_p(bp);
    net_error = NO_CONN;
    return (-1);
  case CONNECTING:
  case CONNECTED:
    if (!cp->closed) {
      cp->sndcnt += (cnt = len_mbuf(bp));
      if (cp->mode == STREAM)
	append(&cp->sndq, bp);
      else
	enqueue(&cp->sndq, bp);
      try_send(cp, 0);
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

int  space_ax(cp)
struct axcb *cp;
{
  int  limit;

  if (!cp) {
    net_error = INVALID;
    return (-1);
  }
  limit = ax_maxframe * ax_paclen;
  if (ax_window > limit) limit = ax_window;
  return limit - cp->sndcnt;
}

/*---------------------------------------------------------------------------*/

int  recv_ax(cp, bpp, cnt)
struct axcb *cp;
struct mbuf **bpp;
int16 cnt;
{
  if (!(cp && bpp)) {
    net_error = INVALID;
    return (-1);
  }
  if (cp->rcvcnt) {
    if (cp->mode == DGRAM || !cnt || cp->rcvcnt <= cnt) {
      *bpp = dequeue(&cp->rcvq);
      cnt = len_mbuf(*bpp);
    } else {
      if (!(*bpp = alloc_mbuf(cnt))) {
	net_error = NO_SPACE;
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

int  close_ax(cp)
struct axcb *cp;
{
  if (!cp) {
    net_error = INVALID;
    return (-1);
  }
  if (cp->closed) {
    net_error = CON_CLOS;
    return (-1);
  }
  cp->closed = 1;
  switch (cp->state) {
  case DISCONNECTED:
    net_error = NO_CONN;
    return (-1);
  case CONNECTING:
    setaxstate(cp, DISCONNECTED);
    return 0;
  case CONNECTED:
    if (!cp->sndcnt) setaxstate(cp, DISCONNECTING);
    return 0;
  case DISCONNECTING:
    net_error = CON_CLOS;
    return (-1);
  }
  return (-1);
}

/*---------------------------------------------------------------------------*/

int  reset_ax(cp)
struct axcb *cp;
{
  if (!cp) {
    net_error = INVALID;
    return (-1);
  }
  if (cp == axcb_server) {
    free((char *) axcb_server);
    axcb_server = NULLAXCB;
    return 0;
  }
  cp->reason = RESET;
  setaxstate(cp, DISCONNECTED);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  del_ax(cp)
struct axcb *cp;
{
  register struct axcb *p, *q;

  for (q = 0, p = axcb_head; p != cp; q = p, p = p->next)
    if (!p) {
      net_error = INVALID;
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
  free_q(&cp->rcvq);
  free_q(&cp->sndq);
  free((char *) cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  valid_ax(cp)
register struct axcb *cp;
{
  register struct axcb *p;

  if (!cp) return 0;
  if (cp == axcb_server) return 1;
  for (p = axcb_head; p; p = p->next)
    if (p == cp) return 1;
  return 0;
}

