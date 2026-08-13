/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <syslog.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "iface.h"
#include "hpux.h"
#include "buildsaddr.h"
#include "transport.h"
#include "login.h"

int Axserver_enabled;

/* The callsigns this node answers to besides those of its interfaces, and
 * where a call to each is to be handed.
 *
 * A listener has to be configured.  The first frame of an incoming connection
 * arrives before there is any control block, so unlike a callsign one of our
 * own links is using, there is nothing to look it up in - see the note beside
 * link_answers_to() in ax25.c.
 *
 * Deliberately not tied to an interface.  An interface callsign belongs to a
 * port; this is a service of the node, and a caller may reach it over
 * whichever port they can.
 */

struct axlisten {
  struct axlisten *next;
  uint8 call[AXALEN];
  char *dest;                           /* where the session is handed */
};

static struct axlisten *Axlisten;

/*---------------------------------------------------------------------------*/

static struct axlisten *axlisten_find(const uint8 *call)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (addreq(lp->call, call)) return lp;
  return 0;
}

/*---------------------------------------------------------------------------*/

int axlisten_active(const uint8 *call)
{
  return axlisten_find(call) != 0;
}

/*---------------------------------------------------------------------------*/

/* ax25 listen                    show what we answer to
 * ax25 listen <call> <dest>      answer to <call>, hand the session to <dest>
 * ax25 listen <call> off         stop answering to it
 */

int doaxlisten(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  struct axlisten *lp, **pp;
  uint8 call[AXALEN];

  if (argc < 2) {
    if (!Axlisten) {
      printf("Not listening for any callsign\n");
      return 0;
    }
    printf("Call       Handed to\n");
    for (lp = Axlisten; lp; lp = lp->next)
      printf("%-9s  %s\n", pax25(buf, lp->call), lp->dest);
    return 0;
  }

  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }

  if (argc < 3) {
    if ((lp = axlisten_find(call)))
      printf("%-9s  %s\n", pax25(buf, lp->call), lp->dest);
    else
      printf("Not listening for %s\n", argv[1]);
    return 0;
  }

  if (!strcmp(argv[2], "off")) {
    for (pp = &Axlisten; *pp; pp = &(*pp)->next)
      if (addreq((*pp)->call, call)) {
        lp = *pp;
        *pp = lp->next;
        free(lp->dest);
        free(lp);
        return 0;
      }
    printf("Not listening for %s\n", argv[1]);
    return 1;
  }

  /* One of our own interface callsigns would be answered anyway, and by the
   * login server rather than by this - say so instead of pretending.
   */
  if (ismyax25addr(call)) {
    printf("%s is an interface callsign already\n", argv[1]);
    return 1;
  }

  if (!(lp = axlisten_find(call))) {
    if (!(lp = (struct axlisten *) calloc(1, sizeof(struct axlisten)))) {
      printf("No memory\n");
      return 1;
    }
    addrcp(lp->call, call);
    lp->next = Axlisten;
    Axlisten = lp;
  } else
    free(lp->dest);
  lp->dest = strdup(argv[2]);
  return 0;
}

/*---------------------------------------------------------------------------*/

// dl9sau: compatibilty feature for IP.VC with xnet hosts
// incoming ax25 PID=Text droper
void axserv_recv_upcall_discard(struct axservice *sp, int cnt)
{
  struct mbuf *bp;

  (void) cnt;
  bp = recv_axservice(sp, 0);
  free_p(&bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(struct axservice *sp, int cnt)
{
  struct mbuf *bp;

  (void) cnt;
  bp = recv_axservice(sp, 0);
  login_write((struct login_cb *) sp->user, &bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(struct axservice *sp, int cnt)
{
  struct mbuf *bp;

  (void) cnt;
  if ((bp = login_read((struct login_cb *) sp->user, space_axservice(sp))))
    send_axservice(sp, &bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(struct axservice *sp, enum lapb_state oldstate, enum lapb_state newstate)
{
  (void) oldstate;
  if (newstate == LAPB_DISCONNECTED) {
    login_close((struct login_cb *) sp->user);
    close_axservice(sp);
  }
}

/*---------------------------------------------------------------------------*/

static void axserv_send_login_upcall(void *arg)
{
  axserv_send_upcall((struct axservice *) arg, 0);
}

static void axserv_close_upcall(void *arg)
{
  close_axservice((struct axservice *) arg);
}


/*---------------------------------------------------------------------------*/

/* Handing an incoming call on to whatever is listening for it.
 *
 * The direction is deliberately outward: we dial, one socket per call, and
 * the program on the other end just accepts.  Its accept() is then literally
 * the Unix accept(), one session is one socket, and nothing has to be
 * correlated between a control channel and a data channel.  It also supplies
 * the refusal we otherwise lack - nobody listening is ECONNREFUSED, at once
 * and without a timeout, where the AX.25 side has already had to say UA.
 */

struct axpipe {
  struct axservice *sp;
  int fd;
  struct mbuf *sndq;                    /* waiting for the socket */
  int connecting;                       /* connect() not finished yet */
  /* Plain text is converted: the radio side ends its lines with CR, this
   * side with LF, and a program that gets the wrong one shows a staircase.
   * Any other protocol id is binary and is passed through untouched.
   */
  int ascii;
  int recv_char;                        /* last byte seen, per direction */
  int send_char;
};

/* How much may pile up for a socket that is not taking it.  Far above
 * anything a radio link produces, so the brake never touches the normal
 * case; it exists so that a consumer which has stopped entirely cannot bind
 * memory without end.  Reaching it leaves the frames in the receive queue,
 * where busy() sees them and RNR goes out by itself.
 */
#define AXPIPE_HIGHWATER 32768

static void axpipe_close(struct axpipe *pp);
static void axpipe_readable(void *arg);
static void axpipe_writable(void *arg);
static void axpipe_pump(struct axpipe *pp);

/*---------------------------------------------------------------------------*/

/* AX.25 -> socket.  Take only what we have room for: what stays in the
 * receive queue makes the link busy, and recv_ax25() sends the RR itself once
 * it drains.  Draining it regardless is what makes a stalled consumer grow
 * without bound.
 */

static void axpipe_recv_upcall(struct axservice *sp, int cnt)
{
  struct axpipe *pp = (struct axpipe *) sp->user;
  struct mbuf *bp;
  int room;

  if (!pp) return;
  room = AXPIPE_HIGHWATER - len_p(pp->sndq);
  if (room <= 0) return;                /* leave it where it is */
  if ((bp = recv_axservice(sp, (uint) (cnt < room ? cnt : room)))) {
    if (pp->ascii) convert_eol(&bp, EOL_LF, &pp->recv_char);
    append(&pp->sndq, &bp);
  }
  axpipe_pump(pp);
}

/*---------------------------------------------------------------------------*/

/* socket -> AX.25 */

static void axpipe_send_upcall(struct axservice *sp, int cnt)
{
  struct axpipe *pp = (struct axpipe *) sp->user;

  (void) cnt;

  if (pp && !pp->connecting) on_read(pp->fd, axpipe_readable, pp);
}

/*---------------------------------------------------------------------------*/

static void axpipe_state_upcall(struct axservice *sp, enum lapb_state oldstate, enum lapb_state newstate)
{
  struct axpipe *pp = (struct axpipe *) sp->user;

  (void) oldstate;
  if (newstate == LAPB_DISCONNECTED && pp) {
    sp->user = 0;
    pp->sp = 0;
    axpipe_close(pp);
    close_axservice(sp);
  }
}

/*---------------------------------------------------------------------------*/

static void axpipe_readable(void *arg)
{
  struct axpipe *pp = (struct axpipe *) arg;
  struct mbuf *bp;
  char buf[2048];
  int n;
  int room;

  if (!pp->sp) return;
  room = space_axservice(pp->sp);
  if (room <= 0) {
    off_read(pp->fd);                   /* the link will call us back */
    return;
  }
  if (room > (int) sizeof(buf)) room = sizeof(buf);
  if ((n = read(pp->fd, buf, (size_t) room)) > 0) {
    bp = qdata(buf, (uint) n);
    if (pp->ascii) convert_eol(&bp, EOL_CR, &pp->send_char);
    send_axservice(pp->sp, &bp);
    return;
  }
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
  close_axservice(pp->sp);              /* end of file: let the link go */
  off_read(pp->fd);
}

/*---------------------------------------------------------------------------*/

static void axpipe_writable(void *arg)
{
  struct axpipe *pp = (struct axpipe *) arg;

  if (pp->connecting) {
    int err = 0;
    socklen_t len = sizeof(err);

    off_write(pp->fd);
    if (getsockopt(pp->fd, SOL_SOCKET, SO_ERROR, (void *) &err, &len) || err) {
      if (pp->sp) close_axservice(pp->sp);
      axpipe_close(pp);
      return;
    }
    pp->connecting = 0;
    on_read(pp->fd, axpipe_readable, pp);
  }
  axpipe_pump(pp);
}

/*---------------------------------------------------------------------------*/

static void axpipe_pump(struct axpipe *pp)
{
  char buf[2048];
  int n;
  int want;

  if (pp->connecting) return;
  while (pp->sndq) {
    want = len_p(pp->sndq);
    if (want > (int) sizeof(buf)) want = sizeof(buf);
    {                                   /* copy, do not consume: a short
					 * write must leave the rest behind */
      struct mbuf *tmp = 0;
      dup_p(&tmp, pp->sndq, 0, (uint) want);
      want = (int) pullup(&tmp, buf, (uint) want);
      free_p(&tmp);
    }
    if (want <= 0) break;
    if ((n = write(pp->fd, buf, (size_t) want)) > 0) {
      pullup(&pp->sndq, NULL, (uint) n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      on_write(pp->fd, axpipe_writable, pp);
      return;
    }
    if (pp->sp) close_axservice(pp->sp);
    return;
  }
  off_write(pp->fd);
  /* Room again: pull whatever the link has been holding, which is what lets
   * recv_ax25() clear the busy condition and reopen the window.
   */
  if (pp->sp && pp->sp->rxq)
    axpipe_recv_upcall(pp->sp, len_p(pp->sp->rxq));
}

/*---------------------------------------------------------------------------*/

static void axpipe_close(struct axpipe *pp)
{
  if (pp->fd >= 0) {
    off_read(pp->fd);
    off_write(pp->fd);
    close(pp->fd);
  }
  free_q(&pp->sndq);
  if (pp->sp) pp->sp->user = 0;
  free(pp);
}

/*---------------------------------------------------------------------------*/

/* The one line the consumer gets before the stream: which port the call came
 * in on, and the header in the form every tool outside WAMPES already reads.
 */

static void axpipe_announce(struct axpipe *pp, struct ax25_cb *axp)
{
  char buf[256];
  char call[AXBUF];
  int i;

  sprintf(buf, "*** incoming %s %s",
	  axp->iface ? axp->iface->name : "?", pax25(call, axp->hdr.dest));
  strcat(buf, ">");
  strcat(buf, pax25(call, axp->hdr.source));
  for (i = 0; i < axp->hdr.ndigis; i++) {
    strcat(buf, ",");
    strcat(buf, pax25(call, axp->hdr.digis[i]));
  }
  strcat(buf, "\n");
  {
    struct mbuf *bp = qdata(buf, (uint) strlen(buf));
    append(&pp->sndq, &bp);
  }
}

/*---------------------------------------------------------------------------*/

static struct axservice *axpipe_open(struct ax25_cb *axp, const char *dest)
{
  int addrlen;
  int fd;
  struct axpipe *pp;
  struct axservice *sp;
  struct sockaddr *addr;

  if (!(addr = build_sockaddr(dest, &addrlen))) return NULL;
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) return NULL;
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  if (!(pp = (struct axpipe *) calloc(1, sizeof(struct axpipe)))) {
    close(fd);
    return NULL;
  }
  pp->fd = fd;
  pp->ascii = 1;                        /* pid=text, until an entry says otherwise */

  if (connect(fd, addr, addrlen)) {
    if (errno != EINPROGRESS) {
      axpipe_close(pp);
      return NULL;
    }
    pp->connecting = 1;                 /* the node must not wait here */
  }

  if (!(sp = open_axservice(axp, PID_NO_L3, axpipe_recv_upcall,
			    axpipe_send_upcall, axpipe_state_upcall, pp))) {
    axpipe_close(pp);
    return NULL;
  }
  pp->sp = sp;
  axpipe_announce(pp, axp);

  if (pp->connecting)
    on_write(fd, axpipe_writable, pp);
  else {
    on_read(fd, axpipe_readable, pp);
    axpipe_pump(pp);
  }
  return sp;
}

/*---------------------------------------------------------------------------*/

/* What serves this protocol id on this link?  Asked once, when the first
 * frame for it arrives and nothing is attached yet.  Returning nothing means
 * the node's own protocols get their turn, and after them the frame is
 * discarded - which is what happens to any protocol id nobody wants.
 */

struct axservice *axserv_start(struct ax25_cb *axp, int pid)
{

  char callsign[AXBUF];
  struct axlisten *lp;
  struct axservice *sp;

  if (pid != PID_NO_L3)
    return NULL;                        /* only plain text so far */

  /* On an incoming link build_path() has already turned the header round:
   * hdr.dest is who called us, hdr.source is the address they called.  It is
   * the latter we listen for.
   */
  if ((lp = axlisten_find(axp->hdr.source))) {
    if ((sp = axpipe_open(axp, lp->dest))) return sp;
    /* Nobody there.  The AX.25 side has already had to say UA, so the only
     * way to decline now is to say why and let go.  The caller is told the
     * callsign, not our socket path - where the node keeps its files is
     * nobody's business on the air.
     */
    {
      struct mbuf *bp;
      char buf[120];

      sprintf(buf, "*** %s is not answering\r", pax25(callsign, axp->hdr.source));
      syslog(LOG_ERR, "%s: cannot hand the call to %s: %s",
	     pax25(callsign, axp->hdr.source), lp->dest, strerror(errno));
      bp = qdata(buf, (uint) strlen(buf));
      send_ax25(axp, &bp, PID_NO_L3);
    }
    disc_ax25(axp);
    return NULL;
  }

  if (!Axserver_enabled)
    return NULL;

  if (!(sp = open_axservice(axp, PID_NO_L3, axserv_recv_upcall,
			    axserv_send_upcall, axserv_state_upcall, NULL)))
    return NULL;
  pax25(callsign, axp->hdr.dest);
  sp->user = login_open(callsign, "AX25", axserv_send_login_upcall,
			axserv_close_upcall, sp);
  if (!sp->user) {
    /* dl9sau: don't disconnect.  The client may decide to; this keeps the
     * session open for transports with other PIDs.
     */
    sp->r_upcall = axserv_recv_upcall_discard;
    sp->t_upcall = 0;
  }
  return sp;
}

/*---------------------------------------------------------------------------*/

int ax25start(int argc, char *argv[], void *p)
{
  Axserver_enabled = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

int ax250(int argc, char *argv[], void *p)
{
  Axserver_enabled = 0;
  return 0;
}
