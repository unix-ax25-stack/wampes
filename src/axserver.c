/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "iface.h"
#include "hpux.h"
#include "buildsaddr.h"
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
void axserv_recv_upcall_discard(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  bp = recv_ax25(axp, 0);
  free_p(&bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  bp = recv_ax25(axp, 0);
  login_write((struct login_cb *) axp->user, &bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  if ((bp = login_read((struct login_cb *) axp->user, space_ax25(axp))))
    send_ax25(axp, &bp, PID_NO_L3);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(struct ax25_cb *axp, enum lapb_state oldstate, enum lapb_state newstate)
{
  if (newstate == LAPB_DISCONNECTED) {
    login_close((struct login_cb *) axp->user);
    del_ax25(axp);
  }
}

/*---------------------------------------------------------------------------*/

static void axserv_send_login_upcall(void *arg)
{
  axserv_send_upcall((struct ax25_cb *) arg, 0);
}

static void axserv_close_upcall(void *arg)
{
  disc_ax25((struct ax25_cb *) arg);
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
  struct ax25_cb *axp;
  int fd;
  struct mbuf *sndq;                    /* waiting for the socket */
  int connecting;                       /* connect() not finished yet */
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

static void axpipe_recv_upcall(struct ax25_cb *axp, int cnt)
{
  struct axpipe *pp = (struct axpipe *) axp->user;
  struct mbuf *bp;
  int room;

  if (!pp) return;
  room = AXPIPE_HIGHWATER - len_p(pp->sndq);
  if (room <= 0) return;                /* leave it where it is */
  if ((bp = recv_ax25(axp, (uint) (cnt < room ? cnt : room))))
    append(&pp->sndq, &bp);
  axpipe_pump(pp);
}

/*---------------------------------------------------------------------------*/

/* socket -> AX.25 */

static void axpipe_send_upcall(struct ax25_cb *axp, int cnt)
{
  struct axpipe *pp = (struct axpipe *) axp->user;

  if (pp && !pp->connecting) on_read(pp->fd, axpipe_readable, pp);
}

/*---------------------------------------------------------------------------*/

static void axpipe_state_upcall(struct ax25_cb *axp, enum lapb_state oldstate, enum lapb_state newstate)
{
  struct axpipe *pp = (struct axpipe *) axp->user;

  if (newstate == LAPB_DISCONNECTED && pp) {
    axp->user = 0;
    pp->axp = 0;
    axpipe_close(pp);
    del_ax25(axp);
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

  if (!pp->axp) return;
  room = space_ax25(pp->axp);
  if (room <= 0) {
    off_read(pp->fd);                   /* the link will call us back */
    return;
  }
  if (room > (int) sizeof(buf)) room = sizeof(buf);
  if ((n = read(pp->fd, buf, (size_t) room)) > 0) {
    bp = qdata(buf, (uint) n);
    send_ax25(pp->axp, &bp, PID_NO_L3);
    return;
  }
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
  disc_ax25(pp->axp);                   /* end of file: let the link go */
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
      if (pp->axp) disc_ax25(pp->axp);
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
    if (pp->axp) disc_ax25(pp->axp);
    return;
  }
  off_write(pp->fd);
  /* Room again: pull whatever the link has been holding, which is what lets
   * recv_ax25() clear the busy condition and reopen the window.
   */
  if (pp->axp && pp->axp->rxq)
    axpipe_recv_upcall(pp->axp, len_p(pp->axp->rxq));
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
  if (pp->axp) pp->axp->user = 0;
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

static int axpipe_open(struct ax25_cb *axp, const char *dest)
{
  int addrlen;
  int fd;
  struct axpipe *pp;
  struct sockaddr *addr;

  if (!(addr = build_sockaddr(dest, &addrlen))) return -1;
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) return -1;
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  if (!(pp = (struct axpipe *) calloc(1, sizeof(struct axpipe)))) {
    close(fd);
    return -1;
  }
  pp->fd = fd;
  pp->axp = axp;

  if (connect(fd, addr, addrlen)) {
    if (errno != EINPROGRESS) {
      axpipe_close(pp);
      return -1;
    }
    pp->connecting = 1;                 /* the node must not wait here */
  }

  axp->user = (char *) pp;
  axp->r_upcall = axpipe_recv_upcall;
  axp->t_upcall = axpipe_send_upcall;
  axp->s_upcall = axpipe_state_upcall;
  axpipe_announce(pp, axp);

  if (pp->connecting)
    on_write(fd, axpipe_writable, pp);
  else {
    on_read(fd, axpipe_readable, pp);
    axpipe_pump(pp);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

void axserv_open(struct ax25_cb *axp, int cnt)
{
  char callsign[AXBUF];
  struct axlisten *lp;

  /* A callsign we were told to listen for is not a login.  Until the handing
   * on is built, say so and let go rather than dropping the caller into a
   * shell they never asked for.
   */
  /* On an incoming link build_path() has already turned the header round:
   * hdr.dest is who called us, hdr.source is the address they called.  It is
   * the latter we listen for.
   */
  if ((lp = axlisten_find(axp->hdr.source))) {
    if (!axpipe_open(axp, lp->dest)) return;
    /* Nobody there.  The AX.25 side has already had to say UA, so the only
     * way to decline now is to say why and let go.
     */
    {
      struct mbuf *bp;
      char buf[120];

      sprintf(buf, "*** %s is not answering\r", lp->dest);
      bp = qdata(buf, (uint) strlen(buf));
      send_ax25(axp, &bp, PID_NO_L3);
    }
    axp->r_upcall = axserv_recv_upcall_discard;
    disc_ax25(axp);
    return;
  }

  if (Axserver_enabled) {
    pax25(callsign, axp->hdr.dest);
    axp->user = (char *) login_open(callsign, "AX25", axserv_send_login_upcall, axserv_close_upcall, axp);
  }
  if (axp->user) {
    free_q(&axp->rxq);
    axp->r_upcall = axserv_recv_upcall;
    axp->t_upcall = axserv_send_upcall;
    axp->s_upcall = axserv_state_upcall;
  } else
#ifdef	notdef
    disc_ax25(axp);
#else
    // dl9sau: don't disconnect. the client may deceide to do it. this
    // keeps the session open for transports with other PIDs
    axp->r_upcall = axserv_recv_upcall_discard;
#endif
    
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
