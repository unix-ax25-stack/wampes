/* AXTCP and KISS-over-TCP share this file.  The whole difference between the
 * two consists of two hooks - rx() and tx() - and the rest is sockets,
 * buffers, maintenance, and the one decision that has to be made up front:
 * TCP is a byte stream, and most bugs in drivers like these are framing bugs
 * (Thomas).
 */

#include "configure.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "cmdparse.h"
#include "iface.h"
#include "ax25.h"
#include "axip.h"
#include "hpux.h"
#include "tcpsock.h"

/*---------------------------------------------------------------------------*/

/* ALL SESSIONS, as a list and not as a tree: there are a handful of them, and
 * they are walked on every tick and every accept().  The ghosts of sockets
 * that are not sockets yet sit in here too - a listening socket is a session
 * that does not have a connection yet - otherwise the tick would have to tell
 * two kinds apart that differ only in whether somebody is already there.
 */
static struct tcpsock *Tcp_socks = NULL;

static struct timer Tcptimer;
static int Tcp_timer_running = 0;

/* THE TICK, and why 30 seconds and not one frame: the tick has three jobs, none
 * of which knows a frame boundary.  It clears rubbish away, it tries the
 * rebuild, and it reports a session that has been silent too long.  A longer
 * deadline than the garbage counter does not make the first any quicker; a
 * shorter one makes the second impossible, because "nothing for five minutes"
 * and "nothing for three seconds" would be the same report.
 */
#define TCPTIMER_INTERVAL	30000	/* ms */

/* HOW OFTEN A RING.  Attempts 1..7 double, after that it stays at half an hour.
 * The build starts at four minutes, because until then the kernel goes its own
 * way - see the explanation in tcpsock.h.
 */
static int
tcp_backoff(
int attempts)
{
  int sec = TCP_BACKOFF_FIRST;

  while (attempts-- > 0 && sec < TCP_BACKOFF_MAX)
    sec *= 2;
  return sec > TCP_BACKOFF_MAX ? TCP_BACKOFF_MAX : sec;
}

/*---------------------------------------------------------------------------*/

static void tcpsock_flush(struct tcpsock *tp);
static void tcpsock_on_write(void *p);
static void tcpsock_on_accept(void *p);
static void tcpsock_reconnect(struct tcpsock *tp);

/* WHEN A CHANNEL COMES UP FOR A DECISION and has not decided yet: the first
 * byte decides, and after that it stays decided.  Anyone who tests again after
 * every frame ends up with a port at which the peer surprises itself (Thomas).
 *
 * THAT IS WHY THE CALL SITS IN THE ONE PATH THAT BRINGS BYTES INTO THE BUFFER,
 * and not in the read() and not in the rx(): there it would be twice, and the
 * second time with half the buffer used up.
 */
static int
tcpsock_detect(
struct tcpsock *tp)
{
  int r;

  if (tp->proto != TCPAD_DETECT || tp->detect == NULL)
    return 0;
  if (tp->len < 1)
    return 0;
  r = tp->detect(tp);
  if (r == 1) {
    /* RECOGNISED, and with that the deadline is gone.  The hook has set the
     * protocol - there is no KISS and no AXTCP written here, because this file
     * does not know what a frame is (Thomas).
     */
    tp->flags &= ~TCF_GARBAGE;
    return 1;
  }
  if (r < 0) {
    /* Neither KISS nor AXTCP: that was not a packet that anybody sends.
     */
    tp->garbage++;
    tcpsock_forget(tp);
    return -1;
  }
  return 0;			/* noch nicht genug Bytes fuer ein Urteil */
}

/*---------------------------------------------------------------------------*/

int
tcpsock_push(
struct tcpsock *tp,
const unsigned char *data,
int len)
{
  int need;

  if (tp->buf == NULL) {
    tp->bufsize = TCPSOCK_BUFSIZE;
    if ((tp->buf = (unsigned char *) malloc((size_t) tp->bufsize)) == NULL)
      return -1;
    tp->len = 0;
  }
  if (tp->len + len > tp->bufsize) {
    need = tp->len + len;
    while (need > tp->bufsize)
      need *= 2;
    if (need > TCPSOCK_MAXFRAME)
      need = TCPSOCK_MAXFRAME;
    if (need < tp->len + len)		/* the limit itself is too small */
      return -1;
    {
      unsigned char *nb = (unsigned char *) realloc(tp->buf, (size_t) need);
      if (nb == NULL)
	return -1;
      tp->buf = nb;
      tp->bufsize = need;
    }
  }
  memcpy(tp->buf + tp->len, data, (size_t) len);
  tp->len += len;
  tp->lastrx = secclock();
  /* BYTES ON THE WIRE, and the same thing the send side counts: there
   * tp->bytesout is the length the tx hook handed back, and that length is
   * the framed output - the AXTCP length field, the KISS FENDs, everything
   * that went into the socket.  Making this one count AX.25 payload instead
   * would have been the tidier-looking half of a symmetry that the other end
   * does not offer, and the two figures would no longer be comparable with
   * each other or with what the partner sees.
   *
   * WHICH PUTS THE KEEPALIVE IN HERE, and that is wanted: a partner that is
   * alive and idle must not leave the port looking dead.  "in:0/12" with three
   * frames' worth of nothing is the picture of such a partner, and "kp in"
   * says what the twelve bytes were.  The frame counter next to it stays at 0,
   * because a keepalive is not a frame.
   */
  tp->bytesin += len;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THIS FRAME SIZE, and it is the one from axip_recv() and not one of our own:
 * 2048 is large enough for every AX.25 frame with everything a digipeater
 * still hangs onto it, and small enough that a buffer push by someone who
 * reached the port does not fill memory.  Two numbers for one thing are two
 * numbers that drift apart (Thomas).
 */
#define MAX_TCP_FRAME		2048

/* A FRAME FROM THE PEER, over the same path as at axip_recv(): the direct
 * partner is taken from the address field, learning goes through
 * axip_learn_transport(), and the loop protection is the same one as there -
 * not one of our own, because one of our own only catches one of the two
 * kinds (Thomas).
 */
void
tcpsock_input(
struct tcpsock *tp,
unsigned char *buf,
int len,
int chan)
{
  struct mbuf *bp;
  uint8 *p;
  uint8 *src;
  int ndigi;

  if (len < 2 * AXALEN || len > MAX_TCP_FRAME) {
    tp->overruns++;
    return;
  }

  /* THE LOOP PROTECTION, and it comes BEFORE the learning.  What we sent
   * ourselves may not create a route: otherwise the copy coming back lays our
   * own address onto our own port - and if the port is a listener, onto
   * ourselves.
   *
   * UI ONLY, and for the same reason as there: connected-mode frames come back
   * at regular intervals and are protocol timing, not an echo.
   */
  if (ax25_frame_is_ui(buf, len) &&
      ax_dup_recent(ax_fingerprint_data(buf, len))) {
    Ax_echoes++;
    return;
  }

  /* THE DIRECT PARTNER, and only that one: the one with the R bit set, because
   * exactly it holds the tunnel.  Anyone who stood further along the path still
   * stands behind the digipeater, and that one is responsible for whoever
   * stands further along still.
   *
   * THE GUARD MATTERS MORE HERE THAN IN AXIP.  The frame here comes out of a TCP
   * stream in which the length statement confirms nothing: a correct length can
   * still be an AX.25 frame with an address field that never ends.  Without the
   * guard the loop runs past the buffer and hands out src from memory (Thomas).
   */
  p = src = buf + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > buf + len) {
      tp->overruns++;
      return;
    }
    p += AXALEN;
    if (p[6] & REPEATED)
      src = p;
    else
      break;
  }

  /* LEARNED OR DISCARDED - and this is the only place where that is decided.
   * The question is a double one: does THIS callsign belong to THIS session,
   * and may anything be learned here at all?
   */
  if (!axip_learn_transport(src, tp, chan, tp->proto, tp->ifp)) {
    axip_dropped();
    return;
  }
  axip_heard(src);
  /* THE COUNTER, AND IT COUNTS FRAMES HANDED OVER.  A frame that learning
   * discarded is not one - otherwise the number would be a statement about the
   * traffic and not about the port, and the two look the same when either of
   * them is broken (Thomas).
   */
  tp->framesin++;

  if ((bp = qdata(buf, (uint) len)) == NULL)
    return;
  net_route(tp->ifp, &bp);
}

/*---------------------------------------------------------------------------*/

/* A FRAME OUT.  The mbuf goes into the queue and is emptied from there; if the
 * socket cannot, it stays put and the write ticker takes it as soon as there is
 * room again.  Losing it instead means the loss only shows up when somebody is
 * waiting for an answer (Thomas).
 */
int
tcpsock_send(
struct tcpsock *tp,
struct mbuf *bp)
{
  unsigned char out[TCPSOCK_MAXFRAME];
  int l;
  int need;

  if (tp == NULL || tp->tx == NULL) {
    free_p(&bp);
    return -1;
  }
  if (tp->flags & TCF_LISTEN) {
    /* A LISTENER HAS NO PEER to send anything to.  That is not an error worth
     * reporting: the sysop ordered it that way.
     */
    free_p(&bp);
    return 0;
  }
  if (!(tp->flags & TCF_CONNECTED)) {
    /* THE REBUILD BELONGS TO THE NEED TO SEND.  A machine that has nothing to send
     * does not ring - and when there is something to send, the build is exactly
     * as right as the moment it is needed.
     *
     * AND THIS FRAME GOES OUT THEN TOO, when the build succeeds at once: it is
     * the occasion, and dropping it because the channel has just fallen means
     * that the first message after an outage is always the one that is missing
     * (Thomas).
     */
    if (!(tp->flags & TCF_CLIENT)) {
      free_p(&bp);
      return 0;
    }
    if (!tp->nextrecon || secclock() >= tp->nextrecon)
      tcpsock_reconnect(tp);
    if (!(tp->flags & TCF_CONNECTED)) {
      free_p(&bp);
      return 0;
    }
  }

  /* A CHANNEL THAT HAS BEEN SILENT TOO LONG MAY HAVE BEEN TAKEN AWAY BY A
   * FIREWALL WITHOUT OUR NOTICING: the RST is filtered, and the next frame
   * goes into the void.  Without this check one writes to a dead channel and
   * believes it was sent.
   */
  if (tp->lasttx && secclock() - tp->lasttx > TCP_IDLE_GUARD) {
    tcpsock_gone(tp);
    if (tp->flags & TCF_CLIENT) {
      tp->nextrecon = 0;
      tcpsock_reconnect(tp);
    }
    free_p(&bp);
    return 0;
  }

  /* THE LOOP PROTECTION BEFORE THE SEND.  This is the other half of
   * ax_dup_recent(): what one sends oneself goes into the receiver's memory,
   * and that has to happen here - otherwise only the far side remembers and
   * not one's own node.
   *
   * BEFORE THE FRAMING, because the fingerprint computes the AX.25 frame and not
   * the KISS wrapper: what comes back is bare, and only the bare frame compares
   * with the bare frame.
   */
  ax_dup_remember(ax_fingerprint(bp));

  l = tp->tx(tp, out, (int) sizeof(out), &bp);
  if (l <= 0 || bp != NULL) {
    /* A FRAME THAT DOES NOT FIT INTO THE STREAM is neither chopped nor cut
     * short: it is discarded and reported, because half a frame is worse than
     * none.
     */
    free_p(&bp);
    return -1;
  }
  tp->framesout++;
  tp->bytesout += l;
  tp->lasttx = secclock();

  need = tp->wlen + l;
  if (need > tp->wbufsize) {
    int sz = tp->wbufsize ? tp->wbufsize : TCPSOCK_BUFSIZE;
    unsigned char *nb;
    while (sz < need)
      sz *= 2;
    if ((nb = (unsigned char *) realloc(tp->wbuf, (size_t) sz)) == NULL) {
      free_p(&bp);
      return -1;
    }
    tp->wbuf = nb;
    tp->wbufsize = sz;
  }
  memcpy(tp->wbuf + tp->wlen, out, (size_t) l);
  tp->wlen += l;
  tcpsock_flush(tp);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE PACKET, queued like any other output - but NOT through the tx
 * hook, because there is no frame here that could be framed: the protocol's
 * keepalive hook writes the bytes that mean "nothing to say" and they go into
 * the same queue, in the same order, behind anything that is already waiting
 * (so the packet never overtakes a real frame) and out of it by the same flush.
 *
 * AND IT DOES NOT SET lasttx ITSELF.  tcpsock_flush() sets it when the bytes
 * are really written, which is the truth; a packet that sat in a full send
 * buffer has not kept anything alive yet.
 */
static void
tcpsock_keepalive_send(
struct tcpsock *tp)
{
  unsigned char out[8];
  int l;
  int need;

  if (tp->keepalive == NULL || tp->ifp == NULL || tp->ifp->keepalive <= 0)
    return;
  if (!(tp->flags & TCF_CONNECTED))
    return;
  if ((l = tp->keepalive(tp, out, (int) sizeof(out))) <= 0)
    return;

  need = tp->wlen + l;
  if (need > tp->wbufsize) {
    int sz = tp->wbufsize ? tp->wbufsize : TCPSOCK_BUFSIZE;
    unsigned char *nb;
    while (sz < need)
      sz *= 2;
    if ((nb = (unsigned char *) realloc(tp->wbuf, (size_t) sz)) == NULL)
      return;
    tp->wbuf = nb;
    tp->wbufsize = sz;
  }
  memcpy(tp->wbuf + tp->wlen, out, (size_t) l);
  tp->wlen += l;
  tp->idlesout++;
  tcpsock_flush(tp);
}

/*---------------------------------------------------------------------------*/

/* THE REAL ENTRY POINT FOR select().  The hook takes void * because hpux.c
 * provides it that way and not because it is pretty; the session comes back as
 * a pointer.  The call to tcpsock_flush() is one line, and without it the queue
 * would be shut exactly when it is needed (Thomas).
 */
static void
tcpsock_on_write(
void *p)
{
  struct tcpsock *tp = (struct tcpsock *) p;
  int err = 0;
  socklen_t elen = sizeof(err);

  /* THE BUILD IS NOT FINISHED YET.  on_write() fires on the first write
   * permission of a nonblocking socket - and that means "connected or failed",
   * not "connected".  SO_ERROR answers the question, and it is asked exactly
   * once: after that it is a quite ordinary write session.
   *
   * WITHOUT THIS QUESTION the session stays in the state "being built" for ever.
   * It then sends nothing (the send path demands TCF_CONNECTED), the tick does
   * not ring it again either (it sees a build in progress), and both look from
   * outside like a working connection that is quiet - the worst form of "hung"
   * (Thomas).
   */
  if (tp->flags & TCF_CONNECTING) {
    if (getsockopt(tp->fd, SOL_SOCKET, SO_ERROR, &err, &elen) < 0)
      err = errno;
    tp->flags &= ~TCF_CONNECTING;
    if (err != 0) {
      tp->resets++;
      if (tp->flags & TCF_CLIENT) {
	tcpsock_gone(tp);
	tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
	tp->attempts++;
      } else {
	tcpsock_forget(tp);
      }
      return;
    }
    tp->flags |= TCF_CONNECTED;
    tp->attempts = 0;
    tp->nextrecon = 0;
    /* THE KEEPALIVE GOES OUT AT ONCE, and that is the whole reason it is on by
     * default.  A partner that has just answered our SYN has told us nothing
     * about itself, so it cannot know whether we speak AXTCP or KISS - and the
     * keepalive packet is what says it: a KISS peer sees FEND and settles on
     * KISS, an AXTCP peer sees a valid frame and settles on AXTCP.  It also
     * ends the wait for a port scanner early, because the scanner does not
     * answer and a real partner does.
     *
     * HERE AND NOT AT THE END OF THE FUNCTION: on_write() is registered again
     * every time a write could not be completed, so a send at the end would go
     * out with every partial write and the port would talk to itself in bursts
     * whenever the window was narrow.
     *
     * IT DOES NOT CLEAR TCF_GARBAGE.  That flag is cleared by having RECEIVED
     * something recognisable (see tcpsock_detect()), and it has to stay that
     * way: our own packet going out proves that we speak, not that they do.  A
     * port scanner that keeps quiet must still run into TCP_GARBAGE_MAX.
     */
    tcpsock_keepalive_send(tp);
  }
  tcpsock_flush(tp);
}

/* THE SEND TICK.  on_write() is registered HERE and not in the send(): the
 * normal path goes through tcpsock_send(), and there the fd is never
 * registered for on_write() - anyone who registers only in the failure case
 * has exactly in the failure case a queue that is never emptied again (Thomas).
 */
static void
tcpsock_flush(
struct tcpsock *tp)
{
  int l;

  if (tp->fd < 0 || tp->wlen <= 0)
    return;
  if (!(tp->flags & TCF_CONNECTED))
    return;
  l = write(tp->fd, tp->wbuf, (size_t) tp->wlen);
  if (l > 0) {
    if (l < tp->wlen)
      memmove(tp->wbuf, tp->wbuf + l, (size_t) (tp->wlen - l));
    tp->wlen -= l;
    tp->lasttx = secclock();
    if (tp->wlen > 0)
      on_write(tp->fd, tcpsock_on_write, tp);	/* the rest waits */
    else
      off_write(tp->fd);
    return;
  }
  if (l < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR))
    return;			/* window full: the ticker comes again */
  /* AN RST FROM THE PEER OR A CHANNEL THAT HAS BEEN TORN DOWN.  Both mean the
   * same thing: the session is over, and for a client that means "build it
   * again", not "forget it".
   */
  if (l < 0)
    tp->resets++;
  if (tp->flags & TCF_CLIENT) {
    tcpsock_gone(tp);
    tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
    tp->attempts++;
  } else {
    tcpsock_forget(tp);
  }
}

/*---------------------------------------------------------------------------*/

/* THE CONNECTION IS GONE.  For a client that means nothing more than: fd to
 * zero, connected cleared, and the tick builds it again.  The session itself
 * stays, with its buffer and its address - the attach command remains valid,
 * and a learned route stays valid until a build has failed (Thomas).
 *
 * But for an accepted session "gone" means gone: the peer has decided, and
 * whoever wants to see it again has to ring again.
 */
void
tcpsock_gone(
struct tcpsock *tp)
{
  if (tp == NULL)
    return;
  if (tp->fd >= 0) {
    off_read(tp->fd);
    off_write(tp->fd);
    close(tp->fd);
    tp->fd = -1;
  }
  tp->flags &= ~(TCF_CONNECTED | TCF_CONNECTING);
  tp->len = 0;			/* half a frame of a dead connection
				 * belongs to no new one */
}

/* THE WHOLE SESSION GONE.  For a client this is the last step - a failed build
 * is not repairable by waiting, and the list should not collect entries over
 * the night that nobody reads any more.
 */
void
tcpsock_forget(
struct tcpsock *tp)
{
  struct tcpsock *prev;
  struct tcpsock *next;

  if (tp == NULL)
    return;
  tcpsock_gone(tp);
  if (tp->listenfd >= 0) {
    close(tp->listenfd);
    tp->listenfd = -1;
  }
  /* THE ROUTE FIRST, and that is the reason for this call here and not one in
   * axip: the axip table does not know which of its rows hang on a TCP session,
   * and cannot know it - it knows channels, not sockets.
   */
  axip_forget_transport(tp);

  for (prev = NULL, next = Tcp_socks; next != NULL; next = next->next) {
    if (next == tp) {
      if (prev)
	prev->next = next->next;
      else
	Tcp_socks = next->next;
      break;
    }
    prev = next;
  }
  if (tp->peer)
    free(tp->peer);
  if (tp->buf)
    free(tp->buf);
  if (tp->wbuf)
    free(tp->wbuf);
  free(tp);
}

/*---------------------------------------------------------------------------*/

int
tcpsock_set_nonblock(
int fd)
{
  int fl = fcntl(fd, F_GETFL, 0);

  if (fl == -1)
    return -1;
  return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/*---------------------------------------------------------------------------*/

struct tcpsock *
tcpsock_new(
struct iface *ifp,
int flags,
int portproto)
{
  struct tcpsock *tp;

  if ((tp = (struct tcpsock *) calloc(1, sizeof(struct tcpsock))) == NULL)
    return NULL;
  tp->ifp = ifp;
  tp->listenfd = -1;
  tp->fd = -1;
  /* A NEW NEIGHBOUR DOES NOT YET KNOW WHICH PROTOCOL IT SPEAKS.
   * TCPAD_DETECT is not the exception here but the normal case, and it is the
   * reason the state exists at all: the first byte decides, and until then the
   * session is exactly this - rubbish with a deadline.
   *
   * portproto is the OTHER half and stays unchanged: it says what the PORT
   * speaks, and that hangs on the command, not on the first byte.
   */
  tp->proto = TCPAD_DETECT;
  tp->portproto = portproto;
  tp->flags = flags;
  if (portproto == TCPAD_DETECT) {
    tp->flags |= TCF_GARBAGE;
    tp->garbage_since = secclock();
  }
  tp->lastrx = tp->lasttx = secclock();
  tp->next = Tcp_socks;
  Tcp_socks = tp;
  return tp;
}

struct tcpsock *
tcpsock_first(
struct iface *ifp)
{
  struct tcpsock *tp;

  for (tp = Tcp_socks; tp; tp = tp->next)
    if (tp->ifp == ifp)
      return tp;
  return NULL;
}

/* WHETHER ANYTHING HANGS ON THIS PORT AT ALL.  The interface's raw hook needs
 * that in order to decide whether it has to ask at all: a port with only a
 * client on it sends over this route, a port without a session does not.
 */
int
tcpsock_has_session(
struct iface *ifp)
{
  return tcpsock_first(ifp) != NULL;
}

/*---------------------------------------------------------------------------*/

/* A LISTENER.  The address is NULL for "all", and that is a real 0.0.0.0 and not
 * a placeholder for "no address": 0.0.0.0 is the way in which one accepts
 * something without knowing it, and :: is the same way for IPv6 (Thomas).
 */
int
tcpsock_listen(
struct tcpsock *tp,
const char *addr,
int port)
{
  struct sockaddr_storage ss;
  socklen_t sl;
  int fd;
  int on = 1;

  if (tp == NULL)
    return -1;
  if (tp->family == 0)
    tp->family = AF_INET;
  memset(&ss, 0, sizeof(ss));
  if (addr != NULL && *addr) {
    struct addrinfo hints, *res = NULL;
    char portbuf[16];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = tp->family;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portbuf, sizeof(portbuf), "%d", port);
    if (getaddrinfo(addr, portbuf, &hints, &res) != 0 || res == NULL) {
      printf("Cannot resolve \"%s\"\n", addr);
      return -1;
    }
    memcpy(&ss, res->ai_addr, res->ai_addrlen);
    sl = res->ai_addrlen;
    freeaddrinfo(res);
  } else {
    if (tp->family == AF_INET6) {
      struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &ss;
      sin6->sin6_family = AF_INET6;
      sin6->sin6_addr = in6addr_any;
      sin6->sin6_port = htons((unsigned short) port);
      sl = sizeof(*sin6);
    } else {
      struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
      sin->sin_family = AF_INET;
      sin->sin_addr.s_addr = INADDR_ANY;
      sin->sin_port = htons((unsigned short) port);
      sl = sizeof(*sin);
    }
  }
  /* REMEMBER THE ADDRESS THAT WAS BOUND.  Without it one cannot say after an
   * accept() which of several ports was meant, once several of them hang on one
   * interface.
   */
  tp->laddr = ss;

  if ((fd = socket(tp->family, SOCK_STREAM, 0)) < 0)
    return -1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (tp->family == AF_INET6) {
    /* ONLY THIS INTERFACE, and therefore this flag: :: would otherwise also take
     * IPv4, and the second address is rejected with "address already in use"
     * without anybody seeing the reason.
     */
    int v6only = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
  }
  if (bind(fd, (struct sockaddr *) &ss, sl) < 0) {
    printf("Cannot bind port %d: %s\n", port, strerror(errno));
    close(fd);
    return -1;
  }
  if (listen(fd, 5) < 0) {
    close(fd);
    return -1;
  }
  tcpsock_set_nonblock(fd);
  tp->listenfd = fd;
  tp->flags |= TCF_LISTEN;
  tp->port = port;
  /* THE ONLY WAY TO AN ACCEPTED SESSION.  Not in a ticker of its own: a ticker of
   * its own polls every port every few seconds, including the many on which
   * nothing comes, and on ten ports on a Raspberry Pi that is the difference
   * between a radio and a heater (Thomas).
   */
  on_read(fd, tcpsock_on_accept, tp);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* A RING.  The build goes into a state called TCF_CONNECTING and not into
 * "finished": a SYN for which nobody is at home comes back after a while with an
 * RST or not at all, and until then the session is connected but not reachable.
 * on_write() tells us when the kernel has the answer - through select(), not
 * through a wait time and not through threads (Thomas).
 */
/* NOT BUILT: the optional shared code, for the two ports that ride on this
 * layer - axtcp and kisstcp.  The reasoning, both directions, and why a code
 * that travels in an address field is a filter and not a credential, are
 * written out once in axip_raw(); this is the second half of it.
 *
 * WHAT WOULD BE BUILT HERE:
 *
 *   Outgoing, client mode.  One empty UI frame as soon as the TCP connect
 *   comes up - source ifp->hwaddr, destination the configured code.  This is
 *   the one moment where the peer is guaranteed to read something: the first
 *   frame it receives from us is what it decides who we are by.  A keepalive
 *   says nothing and cannot be used for it - the AXTCP keepalive is a
 *   zero-length frame and the KISS one is FEND FEND, and neither carries an
 *   address field at all.  The keepalive is not the first frame; it is
 *   everything after it.
 *
 *   Incoming.  Learn and accept once a frame addressed to the code arrives -
 *   the same rule as on axip/axudp, and for the same reason: a server that
 *   demanded it would be inventing a rule xrouter does not have, and
 *   xrouter's clients are the only ones that exist.
 *
 * CONFIGURATION: on attach, for the reason given in axip_raw().  OPEN: the
 * exact wording of the attach argument.
 */
int
tcpsock_connect(
struct tcpsock *tp,
const char *host,
int port)
{
  struct addrinfo hints, *res = NULL, *ai;
  char portbuf[16];
  int fd = -1;
  int rc = -1;

  if (tp == NULL || host == NULL)
    return -1;

  /* THE TARGET GOES IN BEFORE THE RING, not after.  A build that fails on the
   * first attempt - no route to the machine, port closed, SYN discarded - must
   * be able to repeat the same build as every later one, and the tick only
   * finds the target if it is already there.  Otherwise the port dies after the
   * first failure exactly as quietly as "Cannot connect" on the screen promised
   * it would not (Thomas).
   */
  tp->port = port;
  if (tp->peer)
    free(tp->peer);
  tp->peer = strdup(host);
  if (tp->peer == NULL)
    return -1;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = tp->family ? tp->family : AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(portbuf, sizeof(portbuf), "%d", port);
  if (getaddrinfo(host, portbuf, &hints, &res) != 0 || res == NULL) {
    printf("Cannot resolve \"%s\"\n", host);
    return -1;
  }
  /* THE FIRST ONE THAT GOES - and not the first one that can be bound.  A
   * machine with both address families and an IPv6 route into the void
   * otherwise takes the IPv6 attempt and hangs there, although IPv4 would have
   * answered at once (XRouter against BPQ).
   */
  for (ai = res; ai != NULL; ai = ai->ai_next) {
    if (tp->family && ai->ai_family != tp->family)
      continue;
    if ((fd = socket(ai->ai_family, SOCK_STREAM, 0)) < 0)
      continue;
    tp->family = ai->ai_family;
    tp->laddr = *(struct sockaddr_storage *) ai->ai_addr;
    tcpsock_set_nonblock(fd);
    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
      break;			/* connected at once - the loopback case */
    if (errno == EINPROGRESS || errno == EALREADY || errno == EWOULDBLOCK) {
      tp->fd = fd;
      tp->flags |= TCF_CONNECTING;
      on_write(fd, tcpsock_on_write, tp);
      freeaddrinfo(res);
      return 0;
    }
    close(fd);
    fd = -1;
  }
  freeaddrinfo(res);

  if (fd >= 0) {
    tp->fd = fd;
    tp->flags |= TCF_CONNECTED | TCF_CONNECTING;
    /* THE FIRST CLEAN-UP ALWAYS GOT AS FAR AS HERE: the buffer, the counters,
     * the tick.  A build that is treated differently the second time than the
     * first is a build that hangs on the third (Thomas).
     */
    tp->attempts = 0;
    tp->nextrecon = 0;
    on_write(fd, tcpsock_on_write, tp);
    rc = 0;
  }
  return rc;
}

/*---------------------------------------------------------------------------*/

/* THE REBUILD.  A new socket, not the old one revived: after an RST the
 * connection is in a state that cannot be seen from outside, and a fresh fd is
 * the only one whose state one knows.
 *
 * THE PRODUCTIVE CASE IS THE EXPENSIVE ONE: the learned routes stay, because
 * tcpsock_forget() takes them only at the end of a session and not on every
 * RST.  Otherwise one would have to learn all routes again after every outage
 * of the peer, and with a peer that has a hundred partners that means a hundred
 * entries by hand.
 */
static void
tcpsock_reconnect(
struct tcpsock *tp)
{
  char *peer;
  int port;

  if (tp->peer == NULL) {
    tcpsock_forget(tp);
    return;
  }
  peer = strdup(tp->peer);
  port = tp->port;
  tcpsock_gone(tp);
  if (peer == NULL) {
    tcpsock_forget(tp);
    return;
  }
  if (tcpsock_connect(tp, peer, port)) {
    /* THE CLEAN-UP DID NOT WORK, and the session STAYS: it is the reason why
     * another attempt is made at all, and "rang once and then forgotten" means
     * that the port is dead after the first outage of the peer until the sysop
     * types the attach once more (Thomas).
     *
     * The next attempt waits out the backoff time, and that grows: a radio that
     * is switched off should not put a SYN on the wire every two minutes.
     */
    tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
    tp->attempts++;
  }
  free(peer);
}

/*---------------------------------------------------------------------------*/

/* THE ENTRANCE.  read() in one pull for as long as something is there, and the
 * way through the buffer: the reader here knows nothing about frames, it only
 * knows that bytes have arrived.
 *
 * ONE read() IS NOT A FRAME - and the most frequent mistake in drivers like
 * these is to take it for one.  Half a read() is not half a frame, it is only
 * half a frame (Thomas).
 */
static void
tcpsock_on_read(
void *p)
{
  struct tcpsock *tp = (struct tcpsock *) p;
  unsigned char in[TCPSOCK_BUFSIZE];
  int l;
  int off = 0;

  while (1) {
    l = (int) read(tp->fd, in, (size_t) sizeof(in));
    if (l > 0) {
      if (tcpsock_push(tp, in, l) < 0) {
	/* MORE THAN THE CEILING IN ONE STREAM: that is not a frame but somebody
	 * who wants to fill memory.  Disconnecting is the only answer one can
	 * give.
	 */
	tp->overruns++;
	tcpsock_forget(tp);
	return;
      }
      continue;
    }
    if (l == 0) {
      /* THE PEER HAS ORDERED AN END.  The conversation ends here.
       */
      tp->resets++;
      if (tp->flags & TCF_CLIENT) {
	tcpsock_gone(tp);
	tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
	tp->attempts++;
      } else {
	tcpsock_forget(tp);
      }
      return;
    }
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    tp->resets++;
    if (tp->flags & TCF_CLIENT) {
      tcpsock_gone(tp);
      tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
      tp->attempts++;
    } else {
      tcpsock_forget(tp);
    }
    return;
  }

  if (tp->len <= 0)
    return;
  /* AND NOW THE DECISION.  It comes here and not in the rx(), because the
   * protocol hook is needed from the first byte and not from the first
   * complete frame.
   */
  if (tcpsock_detect(tp) < 0)
    return;
  if (tp->rx != NULL)
    tp->rx(tp);
}

/*---------------------------------------------------------------------------*/

/* A NEW ACCEPTED ONE.  It inherits the hooks of the listener - not the
 * identity: the session is new, with its own buffer and its own counters, and
 * it has nothing to do with the listening socket except for the port number
 * (Thomas).
 */
static void
tcpsock_on_accept(
void *p)
{
  struct tcpsock *lp = (struct tcpsock *) p;
  struct tcpsock *tp;
  struct sockaddr_storage ss;
  socklen_t sl;
  int fd;

  sl = sizeof(ss);
  if ((fd = accept(lp->listenfd, (struct sockaddr *) &ss, &sl)) < 0)
    return;
  tcpsock_set_nonblock(fd);
  if ((tp = (struct tcpsock *) calloc(1, sizeof(struct tcpsock))) == NULL) {
    close(fd);
    return;
  }
  tp->ifp = lp->ifp;
  tp->listenfd = -1;
  tp->fd = fd;
  tp->family = lp->family;
  tp->laddr = lp->laddr;
  tp->port = lp->port;
  tp->flags = TCF_CONNECTED;
  /* THE KIND OF PORT COMES ALONG, and stays the one the sysop ordered.
   * proto here is the state of the session and means "still unidentified" - when
   * the detect hook reports, it writes into proto, and a port that knew what it
   * was before would no longer know it afterwards (Thomas).
   */
  tp->proto = TCPAD_DETECT;
  tp->portproto = lp->portproto;
  /* THE HOOKS, and among them the detect.  Without it the accepted session does
   * not know what it is, and its bytes are read as AXTCP whether they are KISS
   * or not - the bug that reports everything as broken on port 8001 (Thomas).
   */
  tp->rx = lp->rx;
  tp->tx = lp->tx;
  tp->detect = lp->detect;
  tp->keepalive = lp->keepalive;
  /* THE DEADLINE IS ALREADY SET AT THE ACCEPT, and not at the first byte: the
   * scanner is already there in the accept(), and a port scanner does not wait
   * for your generosity.
   */
  tp->flags |= TCF_GARBAGE;
  tp->garbage_since = secclock();
  tp->lastrx = tp->lasttx = secclock();
  tp->next = Tcp_socks;
  Tcp_socks = tp;
  on_read(fd, tcpsock_on_read, tp);
  on_write(fd, tcpsock_on_write, tp);
  /* THE KEEPALIVE GOES OUT HERE, and not in on_write(): on_write() only sends it
   * when it was the one that finished a connect, and an accepted session has no
   * connect to finish - it starts out TCF_CONNECTED (Thomas).
   *
   * It goes out before the peer has said a single byte, and that is the point:
   * it is what tells a KISS peer that we are KISS, and it is what a port
   * scanner does not answer.  It does not touch TCF_GARBAGE - only a byte FROM
   * the peer proves that there is one (see tcpsock_on_read()/tcpsock_detect()).
   */
  tcpsock_keepalive_send(tp);
}

/*---------------------------------------------------------------------------*/

/* THE TICK.  Three jobs, none of which has anything to do with a frame
 * boundary.
 *
 * THE REBUILD OF A CLIENT: the loss of a connection can happen at any time, and
 * a sysop who is waiting on port 9393 should not have to make it depend on a
 * movement of the hand.  There is a tick for that.
 *
 * BUT ONLY WHEN THERE IS SOMETHING TO SEND.  Otherwise the node rings a peer
 * that does not answer every four minutes all day long while the radio is
 * switched off, and in that peer's log that looks like an attack (Thomas).
 */
static void
tcpsock_timer_task(
void *arg)
{
  struct tcpsock *tp;
  struct tcpsock *next;
  int now = secclock();

  (void) arg;
  for (tp = Tcp_socks; tp != NULL; tp = next) {
    next = tp->next;

    if (tp->flags & TCF_LISTEN)
      continue;

    /* THE RUBBISH.  A connection that has not yet identified itself is either a
     * slow partner or a scanner.  Five minutes are a good limit: a real peer
     * says its first byte in that period, because the first byte is what it
     * establishes in the first place - and a scanner has had a thousand of them
     * in five minutes.
     */
    if (tp->flags & TCF_GARBAGE) {
      if (now - tp->garbage_since >= TCP_GARBAGE_MAX) {
	tp->garbage++;
	tcpsock_forget(tp);
      }
      continue;
    }

    /* THE KEEPALIVE, and it stands HERE and not further down because two of the
     * tests below are about clients only.  An accepted session is not a client -
     * that is the whole difference between a listener and a client port - and it
     * is exactly the session that goes quiet, because on a listener nothing
     * goes out unless somebody on the far side first sends us a frame.
     *
     * AFTER THE RUBBISH TEST, on purpose: a session that has not identified
     * itself gets nothing from here.  It has already had one packet out of the
     * accept(); it is being watched, and five minutes later it is gone.  Feeding
     * a scanner every five minutes would be keeping exactly the connection
     * alive that the GARBAGE window exists to end.
     */
    if ((tp->flags & TCF_CONNECTED) && tp->keepalive != NULL &&
	tp->ifp != NULL && tp->ifp->keepalive > 0 &&
	now - tp->lasttx >= tp->ifp->keepalive)
      tcpsock_keepalive_send(tp);

    if (!(tp->flags & TCF_CLIENT))
      continue;

    /* A SYN IS RUNNING: the fd is there, it is only not yet settled whether the
     * peer answers.  The fd has its own tick and its own deadline; a second
     * build here would leave the first fd lying around.
     */
    if (tp->flags & TCF_CONNECTING)
      continue;

    if (!(tp->flags & TCF_CONNECTED)) {
      /* THE REBUILD IS ATTEMPTED, and in the end the tick is what actually does it.
       * The first deadline is waited out here so that four attach commands do
       * not produce the same flood of SYNs at the same moment; after that it
       * rings anew on every tick for as long as nobody answers.
       *
       * WHOEVER HAS NEVER RUNG COUNTS AS WELL: the difference between "never
       * tried" and "tried once and failed" is only the number here, and without
       * it a port whose first build failed stands on this line for ever and
       * pretends it never tried (Thomas).
       */
      if (tp->nextrecon == 0)
	tp->nextrecon = now + TCP_BACKOFF_FIRST;
      if (now >= tp->nextrecon) {
	tcpsock_reconnect(tp);
	tp->attempts++;
      }
      continue;
    }
  }
  start_timer(&Tcptimer);
}

/*---------------------------------------------------------------------------*/

/* THE BEGINNING.  One tick for everything, because the jobs are the same and a
 * second tick costs a second clock that nobody reconciles.
 *
 * CALLED SEVERAL TIMES it is harmless: the first call starts the tick, and every
 * further one sees it running and does nothing.  That matters, because "attach
 * axtcp" and "attach kisstcp" are both allowed to come here.
 */
int
tcpsock_init(
void)
{
  if (Tcp_timer_running)
    return 0;
  Tcptimer.func = tcpsock_timer_task;
  Tcptimer.arg = NULL;
  Tcptimer.duration = TCPTIMER_INTERVAL;
  Tcptimer.state = TIMER_STOP;
  start_timer(&Tcptimer);
  Tcp_timer_running = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE FAR SIDE OF A FRAME, and it is not guessed.  In the address field there
 * is a chain, and the next neighbour is the first one without the R bit set -
 * not the last and not the first: the last is the digipeater that transmitted
 * most recently, and that is not the one this frame is going to (Thomas).
 */
static uint8 *
tcpsock_dest(
unsigned char *buf,
int l)
{
  uint8 *dest;
  uint8 *p;
  int ndigi;

  dest = buf;
  p = buf + AXALEN;
  for (ndigi = 0; !(p[6] & E); ndigi++) {
    if (ndigi >= MAXDIGIS || p + 2 * AXALEN > buf + l)
      return NULL;
    p += AXALEN;
    if (!(p[6] & REPEATED)) {
      dest = p;
      break;
    }
  }
  return dest;
}

/* THE SESSION OF A CLIENT PORT, and there is exactly one.  "attach axtcp client
 * host" is a point-to-point line: there is one target, and it is in the command.
 * Anyone who demands a route for it as well has taken a line away from the sysop
 * that decides nothing - and that he has to write again after the next outage of
 * the peer, because by then it points into the void (Thomas).
 *
 * TWO SUCH CLIENTS ON ONE PORT DO NOT GO.  They would share their frames, and
 * that would be a misconfiguration that is hard to find - so the first one is
 * taken here and the second one is not asked.
 */
static struct tcpsock *
tcpsock_client_session(
struct iface *ifp)
{
  struct tcpsock *tp;

  for (tp = Tcp_socks; tp != NULL; tp = tp->next)
    if (tp->ifp == ifp && (tp->flags & TCF_CLIENT))
      return tp;
  return NULL;
}

/* THE WAY OUT, and it is a hook as at axip_raw() - but it has no framing of its
 * own.  That belongs to the tx hook of the protocol, because AXTCP has to count
 * the CRC INTO its length field and KISS does not send one along; one common
 * place would do one of the two twice and the other not at all.
 *
 * THE ROUTE DECIDES, and not the session: on a listener port only what the sysop
 * wrote or what the traffic has learned goes out.  A listener that guessed would
 * push a partner's traffic onto that of its neighbours just because it reported
 * in first (Thomas).
 */
int
tcpsock_raw(
struct iface *ifp,
struct mbuf **bpp)
{
  unsigned char buf[MAX_TCP_FRAME];
  uint8 *dest;
  uint8 (*mpp)[AXALEN];
  struct tcpsock *tp;
  struct mbuf *bp;
  int l;

  if ((l = pullup(bpp, buf, sizeof(buf))) <= 0 || *bpp) {
    free_p(bpp);
    return -1;
  }
  if (l < 2 * AXALEN)
    return -1;
  if ((dest = tcpsock_dest(buf, l)) == NULL)
    return -1;

  /* MULTICAST HAS NO RECEIVER ON TCP.  A round of speech goes over the air to
   * everybody who hears it; over TCP there is exactly one, and he did not ask
   * to speak for all of them.  The sysop writes the receivers into a route, as
   * with axudp as well (Thomas).
   */
  for (mpp = Ax25multi; (*mpp)[0]; mpp++)
    if (addreq(dest, *mpp))
      return -1;

  if ((tp = (struct tcpsock *) axip_transport_route(dest, ifp)) == NULL)
    tp = tcpsock_client_session(ifp);
  if (tp == NULL)
    return -1;			/* nobody knows where to */

  /* THE FRAME GOES OUT AS ITS OWN MBUF, and not out of buf: the tx hook appends
   * the CRC, and that on a borrowed buffer would be a write past the end.
   */
  if ((bp = qdata(buf, (uint) l)) == NULL)
    return -1;
  tcpsock_send(tp, bp);
  ifp->rawsndcnt++;
  return l;
}

/*---------------------------------------------------------------------------*/

/* "axtcp stat" and "kisstcp stat" share this function. */
int
tcpsock_stat(
struct iface *ifp,
int proto,
int argc,
char *argv[],
void *p)
{
  struct tcpsock *tp;
  int now;

  (void) argc;
  (void) argv;
  (void) p;
  now = secclock();
  for (tp = Tcp_socks; tp; tp = tp->next) {
    if (ifp != NULL && tp->ifp != ifp)
      continue;
    /* BY PORTPROTO, and not by proto: proto is the state of the individual session
     * and stands at TCPAD_DETECT on every fresh listener.  Anyone who filters by
     * that loses exactly the lines one wants to see.
     */
    if (proto != 0 && tp->portproto != proto)
      continue;
    printf("  %-12s fd:%d listenfd:%d peer:%s port:%d\n",
	   tp->ifp ? tp->ifp->name : "-",
	   tp->fd, tp->listenfd, tp->peer ? tp->peer : "-", tp->port);
/* "in" AND "out" ARE FRAMES AND BYTES ON THE WIRE, and the two halves do not
     * count the same thing: the first number is AX.25 frames handed over, the
     * second is everything that went through the socket - the AXTCP length
     * field, the KISS FENDs, and the keepalive.  So a partner that is alive and
     * idle shows "in:0/12" and not "in:0/0", which is the point: the port must
     * not look dead because it is quiet.  "kp in" below says what those bytes
     * were.
     */
    printf("    port:%s session:%s flags:%s%s%s%s in:%ld/%ld out:%ld/%ld\n",
	   tp->portproto == TCPAD_KISSTCP ? "kisstcp" : "axtcp",
	   tp->proto == TCPAD_DETECT ? "unidentified" :
	   tp->proto == TCPAD_KISSTCP ? "kisstcp" : "axtcp",
	   (tp->flags & TCF_LISTEN) ? "listen " : "",
	   (tp->flags & TCF_CLIENT) ? "client " : "",
	   (tp->flags & TCF_CONNECTED) ? "connected " : "",
	   (tp->flags & TCF_CONNECTING) ? "connecting " : "",
	   tp->framesin, tp->bytesin, tp->framesout, tp->bytesout);
    printf("    crcerr:%ld overrun:%ld reset:%ld garbage:%ld lastrx:%ld lasttx:%ld now:%ld\n",
	   tp->crcerr, tp->overruns, tp->resets, tp->garbage,
	   (long) tp->lastrx, (long) tp->lasttx, (long) now);
    /* THE KEEPALIVE IN ITS OWN LINE, and only where it can say anything: on a
     * port with the option turned off the two numbers would be zero for ever
     * and would only look like a measurement.  "kp in/out" and the interval
     * stand together, so a port on which nobody speaks keepalive is told
     * apart from one where the partner cannot do it.
     */
    if (tp->ifp != NULL && tp->ifp->keepalive > 0)
      printf("    keepalive:%d kp in:%ld kp out:%ld\n", tp->ifp->keepalive,
	     tp->idlesin, tp->idlesout);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE OPTION OUT OF AN "attach" LINE.
 *
 * WHY IT IS PULLED OUT RATHER THAN PARSED WHERE IT STANDS: the attach line is
 * positional - "attach axtcp <label> listen [<port>]" and "attach axtcp <label>
 * client <host>[:<port>]" - so an option word at the end is simply a word that
 * no step of that chain knows what to do with.  Removing the pair first and
 * closing the gap leaves both of the steps above exactly as they were, and a
 * new option in two years does not mean touching them.
 *
 * THE RANGE, and why it has an upper end at all: a keepalive of a day is not a
 * keepalive, it is a connection that has been broken for a day - the NAT box
 * dropped its mapping hours ago and we are politely writing into it.  Zero is
 * the other end and it means off, including the packet at the accept.
 *
 * Returns 0 when the line is usable - with or without the option - and 1 when
 * it is not, having said why.
 */
int
tcpsock_take_keepalive(
int *argcp,
char **argv,
int *keepal)
{
  int argc = *argcp;
  int i;
  long tmp;

  for (i = 2; i + 1 < argc; i++) {
    if (stricmp(argv[i], "keepalive") != 0)
      continue;
    if (cmd_getnum(argv[i + 1], &tmp) || tmp < 0 || tmp > 86400) {
      printf("\"%s\" is not a keepalive interval: seconds, 0 (off) to 86400\n",
	     argv[i + 1]);
      return 1;
    }
    *keepal = (int) tmp;
    /* THE GAP CLOSED UP, including the terminating NULL, so that whoever reads
     * the shortened line sees exactly the line that was written.
     */
    memmove(argv + i, argv + i + 2, (size_t) (argc - i - 1) * sizeof(char *));
    *argcp = argc - 2;
    return 0;
  }
  return 0;
}
