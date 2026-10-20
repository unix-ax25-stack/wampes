/* tcpsock.c
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

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
#include "slip.h"		/* only FR_END: the first byte of KISS */
#include "tcpsock.h"
#include "trace.h"
#include "sockaddr_util.h"	/* tcpsock_show_verbose(): the bound address */
#include "buildsaddr.h"		/* build_hostport(): the "host:port" rule */

/*---------------------------------------------------------------------------*/

/* ALL SESSIONS, as a list and not as a tree: there are a handful of them, and
 * they are walked on every tick and every accept().  The ghosts of sockets
 * that are not sockets yet sit in here too - a listening socket is a session
 * that does not have a connection yet - otherwise the tick would have to tell
 * two kinds apart that differ only in whether somebody is already there.
 */
struct tcpsock *Tcp_socks = NULL;

static struct timer Tcptimer;
static int Tcp_timer_running = 0;

/* THE TICK, and why 30 seconds and not one frame: the tick has three jobs, none
 * of which knows a frame boundary.  It clears rubbish away, it tries the
 * rebuild, and it reports a session that has been silent too long.  A longer
 * deadline than the garbage counter does not make the first any quicker; a
 * shorter one makes the second impossible, because "nothing for five minutes"
 * and "nothing for three seconds" would be the same report.
 */
/* ZEHN SEKUNDEN, und nicht mehr dreissig: der Tick prueft ueberall nur
 * Fristen (Reconnect-Termine, Keepalive, Garbage), nie Zaehler - er darf
 * also haeufiger schlagen, ohne dass sich sonst etwas aendert, ausser dass
 * eine Frist hoechstens 10 statt 30 s zu spaet kommt.  Genau das ist der
 * Unterschied zwischen "sofort neu verbunden" und "der Nutzer schaut auf
 * die Uhr" nach einem Serverneustart (Thomas).
 */
#define TCPTIMER_INTERVAL	10000	/* ms */

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
static void tcpsock_on_read(void *p);
static void tcpsock_on_accept(void *p);
static void tcpsock_close_listens(struct tcpsock *tp);
static void tcpsock_on_accept(void *p);
static void tcpsock_reconnect(struct tcpsock *tp);
static void tcpsock_client_lost(struct tcpsock *tp);
static void tcpsock_build_failed(struct tcpsock *tp);

/* THE NAME OF A PORT'S POLICY, once, for the two lists that print it.  "auto"
 * and not "axtcp" for the port that takes both - that one is NOT an AXTCP port
 * and calling it one is exactly the lie Punkt 2 is here to remove (Thomas).
 */
static const char *
tcpsock_portname(
int portproto)
{
  if (portproto == TCPAD_KISSTCP)
    return "kisstcp";
  if (portproto == TCPAD_AUTO)
    return "auto";
  return "axtcp";
}

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

/* THE HOOKS OF A PORT THAT TAKES BOTH ENCODINGS.  The sysop wrote no dialect
 * ("type=auto"), so the port carries a set that dispatches on tp->proto - the
 * value its detector read out of the first byte.
 *
 * THE ONE-BYTE RULE IS THE SAME ONE THAT STANDS IN axtcp.c AND kisstcp.c, and
 * it is written once more here because this is the one reading that must NOT
 * answer -1: a pinned port refuses the dialect it was not opened for, an auto
 * port has no other dialect to refuse.  0xc0 is the KISS FEND, and as an AXTCP
 * length it would be 49152, over AXTCP_MAXLEN - so it cannot be anything else;
 * everything else opens a length field and is AXTCP unless a CRC says no,
 * which is axtcp_rx()'s business and not this one's.
 *
 * UNTIL THE FIRST BYTE A SESSION SENDS NOTHING.  That is not an omission but
 * the decision: on a listener the peer speaks first, and a client on an "auto"
 * port cannot exist at all - it would have to frame its first packet with a
 * dialect it does not know yet, which is a deadlock (TODO.txt, ENTSCHIEDEN G).
 * tx and keepalive therefore only ever answer for a proto that is set; the
 * defensive branches below exist so that a route pointing at an unidentified
 * session drops the frame instead of running into a NULL.
 */
static int
tcpsock_dual_detect(
struct tcpsock *tp)
{
  if (tp->len < 1)
    return 0;			/* nothing there yet: keep waiting */
  tp->proto = (tp->buf[0] == FR_END) ? TCPAD_KISSTCP : TCPAD_AXTCP;
  return 1;
}

static int
tcpsock_dual_rx(
struct tcpsock *tp)
{
  if (tp->proto == TCPAD_KISSTCP)
    return kisstcp_rx(tp);
  if (tp->proto == TCPAD_AXTCP)
    return axtcp_rx(tp);
  return 0;			/* unreachable: detection runs before rx */
}

static int
tcpsock_dual_tx(
struct tcpsock *tp,
unsigned char *out,
int max,
struct mbuf **bpp)
{
  if (tp->proto == TCPAD_KISSTCP)
    return kisstcp_tx(tp, out, max, bpp);
  if (tp->proto == TCPAD_AXTCP)
    return axtcp_tx(tp, out, max, bpp);
  /* UNIDENTIFIED, so there is no dialect to write the frame in and no frame
   * goes out.  The caller (tcpsock_send()) frees the mbuf on a negative
   * return, exactly as it does for a frame that did not fit.
   */
  return -1;
}

static int
tcpsock_dual_keepalive(
struct tcpsock *tp,
unsigned char *out,
int max)
{
  if (tp->proto == TCPAD_KISSTCP)
    return kisstcp_keepalive(tp, out, max);
  if (tp->proto == TCPAD_AXTCP)
    return axtcp_keepalive(tp, out, max);
  return -1;			/* nothing to say before the peer said who he is */
}

void
tcpsock_hooks_auto(
struct tcpsock *tp)
{
  tp->rx = tcpsock_dual_rx;
  tp->tx = tcpsock_dual_tx;
  tp->detect = tcpsock_dual_detect;
  tp->keepalive = tcpsock_dual_keepalive;
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
  axip_heard(src, tp->ifp);
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

  /* THE SEND SIDE, which was missing here while axip has traced its own since
   * the beginning (axip.c:729).  Every port traced what came IN through the
   * kernel input path (iface.c:397) and nothing said what went OUT on the two
   * TCP carriers, so a link that is not the one the sysop believes it is looks
   * exactly like a link nobody speaks on - and we are then debugging the wrong
   * end of a cable.
   *
   * HERE and not in axtcp_tx()/kisstcp_tx(), because this is the one place
   * both carriers pass through, and what arrives here is the bare AX.25 frame:
   * the two-byte length prefix and the FEND framing are not what the sysop
   * wants to read, and the CRC is still on the frame as ax25 handed it over.
   *
   * dump() works on its own copy and leaves the frame alone - axip has always
   * done it this way and the frame has to go out afterwards.
   */
  if (tp->ifp != NULL) {
    dump(tp->ifp, IF_TRACE_OUT, bp);
    if (tp->ifp->trace & IF_TRACE_RAW)
      raw_dump(tp->ifp, -1, bp);
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

/* THE FIRST FRAME OF A SESSION WE BUILT.  If the port carries a shared code,
 * the partner decides who we are by the destination of the first frame it
 * receives from us - so the code has to ride in exactly that frame, and the
 * frame has to be the first thing that goes out, ahead of the keepalive.
 *
 * THE MOMENT IS HERE, NOT AT THE ATTACH.  The user cannot know that the
 * connection to the partner has been rebuilt and that a new code frame is
 * due, so net has to watch the build itself.  on_write() answering the
 * SO_ERROR question with success is precisely "the peer is listening again",
 * and it is the one moment in the session where the first frame is
 * guaranteed to be read.
 *
 * THE FRAME IS the axip keepalive frame (axip.c:1058), an empty UI of
 * sixteen bytes - destination the code, source our own call.  It must stay
 * distinguishable from that keepalive, which addresses nobody and must not
 * count as an answer: dest == src and a clear extension bit are the
 * keepalive's mark, the code is six different first bytes.
 */
static void
tcpsock_auth_send(
struct tcpsock *tp)
{
  struct iface *ifp = tp->ifp;
  struct mbuf *bp;
  uint8 buf[2 * AXALEN + 2];
  int l = 2 * AXALEN + 2;

  if (ifp == NULL || ifp->sharedkey[0] == '\0')
    return;
  /* The word was accepted at the attach; this setcall() is the encode, in
   * the same spelling the partner compares with.
   */
  if (setcall(buf, ifp->sharedkey))
    return;
  memcpy(buf + AXALEN, ifp->hwaddr, AXALEN);
  buf[AXALEN + 6] |= E;
  buf[2 * AXALEN] = 0x03;	/* UI */
  buf[2 * AXALEN + 1] = 0xf0;	/* no layer 3 */

  if ((bp = qdata(buf, (uint) l)) == NULL)
    return;
  /* tcpsock_send(), not a direct write: the frame has to be the first thing on
   * the wire, ahead of every frame and every keepalive that follows, and the one
   * queue is what keeps that order.
   */
  tcpsock_send(tp, bp);
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
	/* THE FIRST REFUSAL HAD ONLY THE SILENT PATH.  tcpsock_connect()
	 * reports the synchronous failures and nothing after that, and the
	 * refusal the kernel almost always delivers as ECONNREFUSED arrives
	 * asynchronously as SO_ERROR - which made a client whose peer is
	 * switched off look exactly like one that was never ringing at all
	 * (Thomas).
	 */
	if (tp->peer != NULL)
	  printf("Cannot connect to %s:%d - will retry\n", tp->peer, tp->port);
	tcpsock_gone(tp);
	tcpsock_build_failed(tp);
      } else {
	tcpsock_forget(tp);
      }
      return;
    }
    tp->flags |= TCF_CONNECTED | TCF_WASUP;
    /* AND THE SAME DEADLINE AS AT THE ACCEPT (TODO.txt, ENTSCHIEDEN I).  A
     * client whose peer says nothing after the handshake is as useless as an
     * accepted session whose peer only probed, and the same five minutes end it.
     * By tcpsock_client_lost() and not by tcpsock_forget(): a client port
     * belongs to nobody else and rings again, an accepted session does not.  Only
     * when the peer has not spoken yet - a session that identified itself is kept
     * alive by the keepalive and must not be torn down for silence.
     */
    if (tp->proto == TCPAD_DETECT) {
      tp->flags |= TCF_GARBAGE;
      tp->garbage_since = secclock();
    }
    tp->early = 0;		/* der Burst ist bezahlt oder nie gefallen;
				 * ab hier zaehlt nur noch die Leiter */
    tp->attempts = 0;
    tp->nextrecon = 0;
    on_read(tp->fd, tcpsock_on_read, tp);
    /* THE LOCAL ADDRESS OF THE SESSION, once the build has stood: on the way
     * into a connection getsockname() on a connecting socket is not the answer,
     * and a "bound" line that names an address the session does not use is
     * worse than none (Thomas).
     */
    {
      socklen_t sl = sizeof(tp->laddr);

      if (getsockname(tp->fd, (struct sockaddr *) &tp->laddr, &sl) < 0)
	memset(&tp->laddr, 0, sizeof(tp->laddr));
    }
    /* THE SHARED-KEY FRAME, AND ONLY THAT.  A session we built is the one place
     * where something goes out before the peer has said a byte - the code is how
     * the partner decides who we are, and it has to be the first frame
     * (tcpsock_auth_send()).  The keepalive that used to stand beside it is gone
     * (TODO.txt, ENTSCHIEDEN G): a greeting is a guess at the peer's dialect, and
     * a peer that waits for us is exactly the peer whose protocol we do not know.
     * A client never pins "auto" ("type=auto" with "client" is refused at the
     * attach), so the frame here is never framed in the wrong dialect - and a
     * client with no shared key sends nothing at all until the peer speaks.
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
    tcpsock_auth_send(tp);
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
  if (tp->flags & TCF_CLIENT)
    tcpsock_client_lost(tp);
  else
    tcpsock_forget(tp);
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
  /* AND THE DEADLINE GOES WITH THE CONNECTION.  TCF_GARBAGE means "this live
   * session has not identified itself yet"; with the fd gone there is no
   * session left to identify, and a flag that stayed would have the tick tear
   * the next attempt down the moment it stands (TODO.txt, ENTSCHIEDEN I).
   */
  tp->flags &= ~(TCF_CONNECTED | TCF_CONNECTING | TCF_GARBAGE);
  tp->len = 0;			/* half a frame of a dead connection
				 * belongs to no new one */
}

/* DIE SITZUNG EINES CLIENT-PORTS IST WEG, und wie lange gewartet wird, bis
 * neu gebaut wird, entscheidet sich hier.
 *
 * EINE SITZUNG, DIE STAND (TCF_WASUP) UND LANGER ALS TCP_EARLY_AGE: der
 * Peer war bis vor Sekunden erreichbar - der Server neu gestartet, die
 * Leitung kurz weg, beides kehrt ueblicherweise in einer Minute zurueck.
 * Also darf der erste Neubau sofort kommen (naechster Tick, hoechstens
 * 10 s, oder beim naechsten sendenden Frame gleich), und die naechsten
 * TCP_EARLY_TRIES Versuche folgen im Abstand von TCP_EARLY_GAP - das ist
 * das Fenster, in das ein Neustart des Gegners haelt.  Erst danach gilt
 * die uebliche Leiter wieder.
 *
 * DIE SICHERUNG DAUERHAFT GEBAUT: wer annehmen und sofort wieder
 * zumachen kann (alter Server, Proxy, Konfigurationsfehler), bekommt
 * genau diesen Burst und sonst nichts - seine Sitzungen stehen ja nie
 * TCP_EARLY_AGE lang, und ohne Alter kein Budget.  So kostet jeder echte
 * Ausfall eine Handvoll SYNs und nicht eine pro Tick (Thomas).
 *
 * ALLES ANDERE wie bisher: ein Port, der noch nie gestanden hat, oder
 * eine zu kurz stehende Sitzung warten erst die volle Wartezeit, denn
 * ohne beobachtbare Sitzung geht die Rechnung von einem ausgeschalteten
 * Gegner aus und vom Kernel, der auf eigene Faust SYN wiederholt.
 */
static void
tcpsock_client_lost(
struct tcpsock *tp)
{
  tcpsock_gone(tp);
  if ((tp->flags & TCF_WASUP) &&
      secclock() - tp->lastbuild >= TCP_EARLY_AGE) {
    tp->early = TCP_EARLY_TRIES;
    tp->attempts = 0;
    tp->nextrecon = secclock();
  } else {
    tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
    tp->attempts++;
  }
}

/* DER BAU IST FEHLGESCHLAGEN - die entscheidete Stelle fuer die naechste
 * Wartezeit, an der beiden Wege zusammenlaufen: das asynchrone
 * ECONNREFUSED aus on_write() und das Scheitern in tcpsock_reconnect().
 *
 * Das Early-Budget geht vor.  Es wird hier bezahlt und nicht dort, wo der
 * Verlust fiel, weil zwischen den beiden die Dauer des Syn-Self-Timeouts
 * liegen kann - die Sekunden des Budgets gehoeren ans Ende des
 * Fehlversuchs und nicht an den Anfang des Ausfalls.
 */
static void
tcpsock_build_failed(
struct tcpsock *tp)
{
  if (tp->early > 0) {
    tp->early--;
    tp->nextrecon = secclock() + TCP_EARLY_GAP;
    tp->attempts = 0;
  } else {
    tp->nextrecon = secclock() + tcp_backoff(tp->attempts);
    tp->attempts++;
  }
}

/* HAT DER PORT NOCH EINE SITZUNG?  tcpsock_forget() fragt das, wenn eine
 * EINGEHENDE Sitzung endet: Erst wenn die letzte weg ist, sind auch die
 * Adress-Routen des Ports ohne Weg und werden vergessen (axip_forget_iface()).
 * Eine gerade sterbende Sitzung zaehlt nicht mehr: tcpsock_gone() hat ihren
 * TCF_CONNECTED vorher schon geloescht.
 */
static int
tcpsock_iface_has_session(
struct iface *ifp)
{
  struct tcpsock *tp;

  if (ifp == NULL)
    return 0;
  for (tp = Tcp_socks; tp != NULL; tp = tp->next)
    if (tp->ifp == ifp && tp->fd >= 0 && (tp->flags & TCF_CONNECTED))
      return 1;
  return 0;
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
  tcpsock_close_listens(tp);
  tcpsock_bind_free(tp->binds);
  /* THE ROUTE FIRST, and that is the reason for this call here and not one in
   * axip: the axip table does not know which of its rows hang on a TCP session,
   * and cannot know it - it knows channels, not sockets.
   */
  axip_forget_transport(tp);
  /* EINGEHENDE SITZUNG WEG, UND DAMIT ALLE.  Eine akzeptierte Sitzung ist die
   * eines Besuchers, der wiederkommt, wenn er will - der Port haelt nichts fuer
   * ihn.  Ist es die letzte gewesen, gehoeren auch die Adress-Routen des Ports
   * weg, die keine eigene Sitzung haben (axip_forget_iface()): eine Route
   * "on <port>" waere sonst eine Zeile, deren Weg in Wirklichkeit niemand
   * haelt.  Bei einer CLIENT-Sitzung gilt das nicht: den baut der Port selbst
   * wieder auf und behaelt seine Routen dafuer (Thomas).
   */
  if (!(tp->flags & TCF_CLIENT) && !tcpsock_iface_has_session(tp->ifp))
    axip_forget_iface(tp->ifp);

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
  tp->listens = NULL;
  tp->nlistens = 0;
  tp->fd = -1;
  /* A NEW NEIGHBOUR DOES NOT YET KNOW WHICH PROTOCOL IT SPEAKS.
   * TCPAD_DETECT is not the exception here but the normal case, and it is the
   * reason the state exists at all: the first byte decides, and until then the
   * session is exactly this - rubbish with a deadline.
   *
   * portproto is the OTHER half and stays unchanged: it says what the PORT
   * speaks, and that hangs on the command, not on the first byte.
   *
   * THE DEADLINE IS NOT SET HERE.  It belongs to a connection that is LIVE and
   * has not identified itself, and this struct may be a listening port (no fd,
   * and none to come) or a client that has not rung yet - the clock would run
   * from the attach and not from the session.  It is set where the connection
   * comes up: at the accept (tcpsock_on_accept()) and when our own SYN has
   * stood (tcpsock_on_write()).  See TCP_GARBAGE_MAX in tcpsock.h.
   */
  tp->proto = TCPAD_DETECT;
  tp->portproto = portproto;
  tp->flags = flags;
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

/* WHETHER THIS PORT IS ONE OF THE TWO TCP CARRIERS AT ALL.  For ifconfig
 * verbose, which walks every port in the node: a serial port, a loopback and a
 * tun must not be asked about sockets and listening ports they do not have.
 */
int
tcpsock_isport(
struct iface *ifp)
{
  return tcpsock_first(ifp) != NULL;
}

/* THE PORT'S ONLY SESSION, or NULL when it has zero or several.  For a
 * written route that names the port instead of a session (axip_sysop_route_on(),
 * tcpsock_raw()): with one session hanging on the port, "the port" and "the
 * neighbour" are the same thing - what "axip route add <call>:<port>" says is
 * then no guess.  With two, the route says nothing about WHICH one, and the
 * answer this does not give is worth more than the one it makes up (Thomas).
 *
 * ONLY WHAT IS CONNECTED COUNTS.  The port's own listening socket sits in the
 * same list with the same tp->ifp and holds no peer (tcpsock_stat() prints
 * the two kinds as "listen" and "connected").  Counted, it turned one session
 * into two and the written route never wrote - measured: ax25sndcnt 1,
 * rawsndcnt 0, nothing on the wire; and with no session at all it made the
 * listener look like the one session, so the frame went to a socket nobody
 * answers on and rawsndcnt counted it as sent all the same.
 */
static struct tcpsock *
tcpsock_single_session(
struct iface *ifp)
{
  struct tcpsock *tp;
  struct tcpsock *one = NULL;
  int n = 0;

  for (tp = Tcp_socks; tp != NULL; tp = tp->next)
    if (tp->ifp == ifp && (tp->flags & TCF_CONNECTED)) {
      one = tp;
      n++;
    }
  return n == 1 ? one : NULL;
}

/*---------------------------------------------------------------------------*/

/* THE "bind=" LIST, split on the commas and looked up once.
 *
 * Written in the order it stands in the configuration and kept in that order,
 * because "ifconfig verbose" prints the sockets that way and a sysop comparing
 * two lines has to be comparing them in the order he wrote them.
 *
 * ONE ADDRESS PER SOCKET, because one socket cannot answer on 0.0.0.0 and on
 * ::1 at the same time, and "bind=0.0.0.0,[::]" is two addresses somebody wrote
 * on purpose.  AT MOST ONE PER FAMILY, and the second one is refused rather than
 * bound: which of two IPv4 sockets a session would leave by is a question with
 * no answer in the configuration, and an answer the code makes up is one the
 * sysop did not write.
 *
 * Returns NULL for no list, and NULL with a message for a list that cannot be
 * taken.  The word is quoted in every message - "cannot bind 127.0.0.2" is
 * something to act on, "cannot bind" is not.
 */
struct tcpsock_bindlist *
tcpsock_bind_list(
const char *list)
{
  struct tcpsock_bindlist *bl;
  char *copy;
  char *entry;
  char *comma;
  struct tcpsock_bind *b;
  struct tcpsock_bind *tail;

  if (list == NULL || *list == '\0')
    return NULL;
  if (!(bl = (struct tcpsock_bindlist *) malloc(sizeof(struct tcpsock_bindlist)))) {
    printf("out of memory\n");
    return NULL;
  }
  memset(bl, 0, sizeof(*bl));
  if (!(copy = strdup(list))) {
    printf("out of memory\n");
    free(bl);
    return NULL;
  }

  for (entry = copy; entry; entry = comma) {
    struct sockaddr_storage sa;
    socklen_t sl = 0;

    if ((comma = strchr(entry, ',')) != NULL)
      *comma++ = '\0';
    if (!*entry) {
      printf("\"bind=%s\" has an empty entry\n", list);
      goto Fail;
    }
    if (sockaddr_from_bindword(entry, 0, &sa, &sl))
      goto Fail;
    for (b = bl->head; b; b = b->next)
      if (b->family == (int) sa.ss_family) {
	printf("\"%s\": this port already has an address of this family "
	       "(%s).  One\nsocket per family - \"bind=%s\" is what names the "
	       "other one.\n", entry,
	       sa.ss_family == AF_INET6 ? "IPv6" : "IPv4",
	       sa.ss_family == AF_INET6 ? "[::]" : "0.0.0.0");
	goto Fail;
      }
    if (!(b = (struct tcpsock_bind *) malloc(sizeof(struct tcpsock_bind)))) {
      printf("out of memory\n");
      goto Fail;
    }
    memset(b, 0, sizeof(*b));
    b->family = (int) sa.ss_family;
    b->addr = sa;
    snprintf(b->word, sizeof(b->word), "%s", entry);
    /* AT THE END, so the list reads the way it was written.  A single-linked
     * list that everybody prepends to comes out backwards, and "bind=0.0.0.0,
     * [::]" would be displayed as [::] and then 0.0.0.0 - the reverse of the
     * configuration, on every screen, for ever.
     */
    if (bl->head == NULL)
      bl->head = b;
    else {
      for (tail = bl->head; tail->next; tail = tail->next)
	;
      tail->next = b;
    }
    bl->n++;
  }
  free(copy);
  return bl;

Fail:
  free(copy);
  tcpsock_bind_free(bl);
  return NULL;
}

/*---------------------------------------------------------------------------*/

void
tcpsock_bind_free(
struct tcpsock_bindlist *bl)
{
  struct tcpsock_bind *b;
  struct tcpsock_bind *bn;

  if (bl == NULL)
    return;
  for (b = bl->head; b; b = bn) {
    bn = b->next;
    free(b);
  }
  free(bl);
}

/*---------------------------------------------------------------------------*/

struct tcpsock_bind *
tcpsock_bind_nth(
struct tcpsock_bindlist *bl,
int n)
{
  struct tcpsock_bind *b;

  if (bl == NULL || n < 0)
    return NULL;
  for (b = bl->head; b; b = b->next, n--)
    if (n == 0)
      return b;
  return NULL;
}

/*---------------------------------------------------------------------------*/

struct tcpsock_bind *
tcpsock_bind_family(
struct tcpsock_bindlist *bl,
int family)
{
  struct tcpsock_bind *b;

  if (bl == NULL)
    return NULL;
  for (b = bl->head; b; b = b->next)
    if (b->family == family)
      return b;
  return NULL;
}

/*---------------------------------------------------------------------------*/

/* ONE LISTENING SOCKET, on one address.
 *
 * b says which address; NULL means the default of this family, and the family
 * is a parameter because without a "bind=" there are two defaults and not one.
 *
 * THE PORT GOES INTO THE ADDRESS HERE, and not at the bind() call, because the
 * address is what "ifconfig verbose" prints - and it should print what the
 * socket really got rather than what the parse tree believed.
 */
static int
tcpsock_listen_one(
struct tcpsock *tp,
struct tcpsock_bind *b,
int family,
int port)
{
  struct tcpsock_listen *ln;
  struct sockaddr_storage ss;
  socklen_t sl;
  int on = 1;

  memset(&ss, 0, sizeof(ss));
  if (b != NULL) {
    ss = b->addr;
    family = b->family;
    sl = sockaddr_len((struct sockaddr *) &ss);
  } else {
    /* THE DEFAULT OF A FAMILY: every address of it, which is the way in which
     * one accepts something without knowing it.  It is a real 0.0.0.0 and not a
     * placeholder for "no address" (Thomas).
     */
    if (family == AF_INET6) {
      struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *) &ss;
      sin6->sin6_family = AF_INET6;
      sin6->sin6_addr = in6addr_any;
      sl = sizeof(*sin6);
    } else {
      struct sockaddr_in *sin = (struct sockaddr_in *) &ss;
      sin->sin_family = AF_INET;
      sin->sin_addr.s_addr = INADDR_ANY;
      sl = sizeof(*sin);
    }
  }
  if (sl == 0 || !sockaddr_set_port((struct sockaddr *) &ss, port)) {
    printf("\"bind=%s\" has no port for the number %d\n",
	   b ? b->word : "0.0.0.0", port);
    return -1;
  }

  if (!(ln = (struct tcpsock_listen *) malloc(sizeof(struct tcpsock_listen)))) {
    printf("out of memory\n");
    return -1;
  }
  memset(ln, 0, sizeof(*ln));
  ln->family = family;
  ln->addr = ss;

  if ((ln->fd = socket(family, SOCK_STREAM, 0)) < 0) {
    printf("cannot create socket: %s\n", strerror(errno));
    free(ln);
    return -1;
  }
  setsockopt(ln->fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  if (family == AF_INET6) {
    /* ONLY THIS INTERFACE, and therefore this flag: :: would otherwise also take
     * IPv4, and the second address is rejected with "address already in use"
     * without anybody seeing the reason.  It is also what lets an axudp port and
     * an axtcp port sit side by side on one number.
     */
    int v6only = 1;
    setsockopt(ln->fd, IPPROTO_IPV6, IPV6_V6ONLY, &v6only, sizeof(v6only));
  }
  if (bind(ln->fd, (struct sockaddr *) &ss, sl) < 0) {
    char abuf[SOCKADDR_STRLEN];

    printf("cannot bind %s port %d: %s\n",
	   sockaddr_to_string((struct sockaddr *) &ss, abuf, sizeof(abuf)),
	   port, strerror(errno));
    close(ln->fd);
    free(ln);
    return -1;
  }
  if (listen(ln->fd, 5) < 0) {
    printf("cannot listen on port %d: %s\n", port, strerror(errno));
    close(ln->fd);
    free(ln);
    return -1;
  }
  tcpsock_set_nonblock(ln->fd);
  /* AT THE END, for the reason tcpsock_bind_list() keeps its order: the sockets
   * are printed in the order the configuration lists them, and a list that
   * everybody prepends to comes out backwards (Thomas).
   */
  {
    struct tcpsock_listen *t;

    if (tp->listens == NULL)
      tp->listens = ln;
    else {
      for (t = tp->listens; t->next; t = t->next)
	;
      t->next = ln;
    }
  }
  tp->nlistens++;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* A LISTENER: one socket per address of the "bind=" list, and the two defaults
 * when there is no list at all.
 *
 * BOTH FAMILIES BY DEFAULT, and the IPv6 one second: 0.0.0.0 and [::] are the
 * way in which one accepts something without knowing it, and a node on the air
 * has partners of both kinds.  "bind=0.0.0.0" says IPv4 and nothing else, which
 * is what a sysop writes when he means it.
 */
int
tcpsock_listen(
struct tcpsock *tp,
struct tcpsock_bindlist *bl,
int port)
{
  struct tcpsock_listen *ln;

  if (tp == NULL)
    return -1;
  /* THE LISTENING PORT IS ITS OWN NUMBER.  A port that listens and dials has
   * two of them, and the one the partner is called on says nothing about the one
   * that is answered on (Thomas).
   */
  tp->lport = port;

  if (bl == NULL) {
    if (tcpsock_listen_one(tp, NULL, AF_INET, port))
      return -1;
#if HAS_AF_INET6
    if (tcpsock_listen_one(tp, NULL, AF_INET6, port)) {
      tcpsock_close_listens(tp);
      return -1;
    }
#endif
  } else {
    int i;

    for (i = 0; i < bl->n; i++) {
      if (tcpsock_listen_one(tp, tcpsock_bind_nth(bl, i),
			     tcpsock_bind_nth(bl, i)->family, port)) {
	tcpsock_close_listens(tp);
	return -1;
      }
    }
  }
  tp->flags |= TCF_LISTEN;
  /* THE ONLY WAY TO AN ACCEPTED SESSION.  Not in a ticker of its own: a ticker of
   * its own polls every port every few seconds, including the many on which
   * nothing comes, and on ten ports on a Raspberry Pi that is the difference
   * between a radio and a heater (Thomas).
   *
   * The argument is the LISTENING SOCKET and not the port: there is more than one
   * of them, only the socket says which one is readable, and WHICH one it was
   * says the family of everything that comes out of it.
   */
  for (ln = tp->listens; ln; ln = ln->next)
    on_read(ln->fd, tcpsock_on_accept, ln);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* A RING.  The build goes into a state called TCF_CONNECTING and not into
 * "finished": a SYN for which nobody is at home comes back after a while with an
 * RST or not at all, and until then the session is connected but not reachable.
 * on_write() tells us when the kernel has the answer - through select(), not
 * through a wait time and not through threads (Thomas).
 */
/* THE SHARED CODE OF A TCP PORT, and the outgoing half is BUILT.  The
 * reasoning in both directions - and why a code that travels in an address
 * field is a filter and not a credential - is written out once in
 * axip_raw(); this is the second half of it.
 *
 * WHAT IS BUILT:
 *
 *   Outgoing, client mode.  One empty UI frame as soon as the TCP connect
 *   comes up - source ifp->hwaddr, destination the configured code.  This is
 *   the one moment where the peer is guaranteed to read something: the first
 *   frame it receives from us is what it decides who we are by.  After a
 *   rebuilt TCP link that packet must be there again although the user saw
 *   nothing, which is why the send watches the build and not the attach
 *   (tcpsock_auth_send()).  A keepalive says nothing and cannot be used for
 *   it - the AXTCP keepalive is a zero-length frame and the KISS one is FEND
 *   FEND, and neither carries an address field at all.  The keepalive is not
 *   the first frame; it is everything after it.
 *
 * WHAT IS NOT BUILT:
 *
 *   Incoming.  Learn and accept once a frame addressed to the code arrives -
 *   the same rule as on axip/axudp, and for the same reason: a server that
 *   demanded it would be inventing a rule xrouter does not have, and
 *   xrouter's clients are the only ones that exist.  One half of a filter is
 *   not a filter, so a port that only SENDS the code protects nobody yet;
 *   building the check later changes nothing here.
 *
 * CONFIGURATION: on attach ("shared-key <code>"), for the reason given in
 * axip_raw(): a code means nothing on a port kind that has no address field
 * to ride in, and the attach is where the kind is being named.
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
  int said = 0;			/* has this attempt already said why? */

  if (tp == NULL || host == NULL)
    return -1;

  /* WHEN WE RANG LAST, and every attempt says so here - the sync failure as
   * well as the one that goes through on_write().  tcpsock_client_lost() asks
   * this to decide between an early rebuild and the patient ladder (Thomas).
   */
  tp->lastbuild = secclock();

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
    printf("Cannot resolve \"%s\" - will retry\n", host);
    return -1;
  }
  /* THE FIRST ONE THAT GOES - and not the first one that can be bound.  A
   * machine with both address families and an IPv6 route into the void
   * otherwise takes the IPv6 attempt and hangs there, although IPv4 would have
   * answered at once (XRouter against BPQ).
   */
  for (ai = res; ai != NULL; ai = ai->ai_next) {
    struct tcpsock_bind *b;

    if (tp->family && ai->ai_family != tp->family)
      continue;
    if ((fd = socket(ai->ai_family, SOCK_STREAM, 0)) < 0)
      continue;
    tcpsock_set_nonblock(fd);
    /* THE SOURCE ADDRESS THE SYSOP WROTE, and it is a bind() before the
     * connect() - there is no other way to leave a machine by one of its own
     * addresses.  WHICH entry applies depends on the peer: a client has one
     * socket and therefore one source address, and the peer decides which
     * family that is.  So a node with two addresses that calls a partner over
     * IPv6 leaves by the IPv6 one named in "bind=", and a name that resolves
     * to either family picks the matching entry - neither of which is
     * possible if the address is decided once, before the name is looked up.
     *
     * NO ENTRY FOR THIS FAMILY IS NOT AN ERROR HERE, and that is worth a word:
     * the peer may come back on the other family after the next outage, and a
     * session that rings again must be able to.  An entry that cannot be bound
     * at all IS said, here and now, and not by the caller: the caller only
     * knows that the session did not come up, and "will retry" on its own
     * would read as a peer that is not there yet (Thomas).
     */
    if ((b = tcpsock_bind_family(tp->binds, ai->ai_family)) != NULL) {
      struct sockaddr_storage src = b->addr;
      socklen_t sl = sockaddr_len((struct sockaddr *) &src);

      if (sl == 0 || bind(fd, (struct sockaddr *) &src, sl) < 0) {
	printf("Cannot leave by %s to reach %s: %s - will retry\n", b->word,
	       host, strerror(errno));
	said = 1;
	close(fd);
	fd = -1;
	continue;
      }
    }
    tp->family = ai->ai_family;
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
  /* THE LOCAL ADDRESS OF THE SESSION, and only now is it known: getsockname()
   * answers the address the kernel picked, which without a "bind=" is not the
   * address the sysop would have named and with one is the address he did.  It
   * is asked after the connect() and not before, because before the connect a
   * socket that is not connected yet has the address it was bound to, which on
   * a host with several addresses is 0.0.0.0 - a line that says "bound 0.0.0.0"
   * on a working session is a line nobody can learn anything from.
   */
  if (fd >= 0 && tp->fd >= 0) {
    socklen_t sl = sizeof(tp->laddr);

    if (getsockname(tp->fd, (struct sockaddr *) &tp->laddr, &sl) < 0)
      memset(&tp->laddr, 0, sizeof(tp->laddr));
  }
  /* ONE SENTENCE FOR THE ATTEMPT, from whoever knows most about it.  The
   * caller adds nothing of its own: "Cannot connect" after a line that already
   * said "Cannot leave by 127.0.0.2" is the same mistake twice, and the second
   * one is the one that hides the first (Thomas).
   */
  if (rc && !said)
    printf("Cannot connect to %s:%d - will retry\n", host, port);
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
     * is switched off should not put a SYN on the wire every two minutes -
     * es sei denn, es laeuft gerade das Early-Budget eines verlorenen
     * Stehenden (tcpsock_build_failed()).
     */
    tcpsock_build_failed(tp);
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
      if (tp->flags & TCF_CLIENT)
	tcpsock_client_lost(tp);
      else
	tcpsock_forget(tp);
      return;
    }
    if (errno == EINTR)
      continue;
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      break;
    tp->resets++;
    if (tp->flags & TCF_CLIENT)
      tcpsock_client_lost(tp);
    else
      tcpsock_forget(tp);
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

/* ALL THE LISTENING SOCKETS OF A PORT GONE, and their entries with them.
 *
 * A PARTIAL LIST IS NOT A LISTENER.  "bind=0.0.0.0,192.168.1.1" on a node that
 * has no 192.168.1.1 would otherwise leave a port that listens on 0.0.0.0 and
 * says nothing about the second address that failed - and a sysop who wrote two
 * addresses is entitled to know that he got neither.
 */
static void
tcpsock_close_listens(
struct tcpsock *tp)
{
  struct tcpsock_listen *ln;
  struct tcpsock_listen *lnn;

  for (ln = tp->listens; ln; ln = lnn) {
    lnn = ln->next;
    close(ln->fd);
    free(ln);
  }
  tp->listens = NULL;
  tp->nlistens = 0;
  tp->flags &= ~TCF_LISTEN;
}

/*---------------------------------------------------------------------------*/

/* A NEW ACCEPTED ONE.  It inherits the hooks of the listener - not the
 * identity: the session is new, with its own buffer and its own counters, and
 * it has nothing to do with the listening socket except for the port number
 * (Thomas).
 *
 * IT ALSO INHERITS THE ADDRESS AND THE FAMILY OF THE ONE LISTENING SOCKET IT
 * CAME IN ON, and that is not a detail: the port may listen on both families at
 * once, and a session that said "IPv4" for a partner that arrived on ::1 is
 * wrong in the one line "ifconfig verbose" has to get right (Thomas).
 */
static void
tcpsock_on_accept(
void *p)
{
  struct tcpsock_listen *ln = (struct tcpsock_listen *) p;
  struct tcpsock_listen *l;
  struct tcpsock *lp;
  struct tcpsock *tp;
  struct sockaddr_storage ss;
  socklen_t sl;
  int fd;

  /* Back to the port: the socket knows the address, the port knows the hooks.
   *
   * AND THE SOCKET IS LOOKED FOR ANYWHERE IN THE LIST, not only at its head:
   * "bind=127.0.0.1,[::1]" is ONE port with TWO listening sockets, and an
   * accept on the second socket would otherwise find no port and be dropped in
   * silence - the accept() had happened, the socket had data, and nobody read
   * it (Thomas).
   */
  for (lp = Tcp_socks; lp; lp = lp->next) {
    for (l = lp->listens; l != NULL; l = l->next)
      if (l == ln)
	break;
    if (l != NULL)
      break;
  }
  if (lp == NULL)
    return;			/* the port is gone; do not accept into it */

  sl = sizeof(ss);
  if ((fd = accept(ln->fd, (struct sockaddr *) &ss, &sl)) < 0)
    return;
  tcpsock_set_nonblock(fd);
  if ((tp = (struct tcpsock *) calloc(1, sizeof(struct tcpsock))) == NULL) {
    close(fd);
    return;
  }
  tp->ifp = lp->ifp;
  tp->fd = fd;
  tp->family = ln->family;
  tp->laddr = ln->addr;
  /* AND THE PEER, from the accept() and not from a lookup: it is the one address
   * of this session that was never written anywhere, and "ifconfig <iface> stats"
   * has to name a session before its line is worth anything.
   */
  tp->raddr = ss;
  tp->port = lp->lport;
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
   * for your generosity.  IT DOES NOT ASK WHAT THE PORT IS: an accepted session
   * that has said nothing is unfinished whether the port is pinned or takes both
   * dialects, and the five minutes end it either way (TODO.txt, ENTSCHIEDEN I).
   */
  tp->flags |= TCF_GARBAGE;
  tp->garbage_since = secclock();
  tp->lastrx = tp->lasttx = secclock();
  tp->next = Tcp_socks;
  Tcp_socks = tp;
  on_read(fd, tcpsock_on_read, tp);
  on_write(fd, tcpsock_on_write, tp);
  /* AND NOT ONE BYTE GOES OUT.  An accepted session says nothing until the peer
   * has said its first byte (TODO.txt, ENTSCHIEDEN G).  A greeting here would be
   * a guess at the peer's dialect, and the guess is destructive: c0 c0 is a
   * length of 0xC0C0 for a peer that reads AXTCP - over AXTCP_MAXLEN, and
   * axtcp_rx() throws the connection away - and 00 02 00 00 only helps a peer
   * that already knows to wait for a FEND.  The peer's first byte is what
   * decides the dialect, and until then there is nothing to frame.
   *
   * An auto port could not even frame a greeting (the dispatcher has no dialect
   * yet), and because the deadline above runs out on a peer that never says
   * anything, the first keepalive of a real session also falls in the dialect
   * the peer's first byte named - see the keepalive block in the tick.
   */
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
	if (tp->flags & TCF_CLIENT)
	  tcpsock_client_lost(tp);
	else
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
     * AND IT WAITS FOR THE PEER'S FIRST BYTE (TODO.txt, ENTSCHIEDEN G): tp->proto
     * is TCPAD_DETECT until the peer has said which dialect it speaks, so the
     * FIRST keepalive goes out in the dialect that was RECOGNISED and not in a
     * guessed one.  On an accepted session the rubbish test above already says
     * this (the deadline is set at the accept and only a byte from the peer
     * clears it); the test here is what makes it true of a CLIENT too, where no
     * such deadline stands.
     *
     * AFTER THE RUBBISH TEST, on purpose: a session that has not identified
     * itself gets nothing from here.  Feeding a scanner every five minutes would
     * be keeping exactly the connection alive that the GARBAGE window exists to
     * end.
     */
    if ((tp->flags & TCF_CONNECTED) && tp->keepalive != NULL &&
	tp->proto != TCPAD_DETECT &&
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

  if ((tp = (struct tcpsock *) axip_transport_route(dest, ifp)) == NULL) {
    /* A WRITTEN ROUTE ON ANOTHER PORT SENDS NOTHING THROUGH THIS ONE.  The
     * sysop wrote "this callsign goes via port X", and this port's client
     * session must not carry it on as if the command had named this port -
     * which is exactly how the SABM of "c kisstcp2:db0fhn-13" left via
     * axtcp.  No session, no silent switch: -1 (Thomas).
     */
    if (axip_sysop_route_foreign(dest, ifp))
      return -1;
    tp = tcpsock_client_session(ifp);
  }
  /* A WRITTEN ROUTE FOR THIS PORT, and no other session to ask: this is the
   * listener case (a client port already sent by the line above), and here
   * "the port" resolves to a session only while exactly ONE session hangs on
   * it - the neighbour of a single peer is precisely whom the sysop named in
   * "axip route add <call>:<port>".  Two sessions mean two possible answers
   * to a question the route does not ask, and a frame pushed onto the wrong
   * neighbour's session is not an answer anybody wanted; so it stays
   * unwritten (tcpsock_single_session(), Thomas).
   */
  if (tp == NULL &&
      axip_sysop_route_on(dest, ifp))
    tp = tcpsock_single_session(ifp);
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

/* ifconfig <iface> verbose, for the TCP carrier.
 *
 * "attach axtcp" or "attach kisstcp", and then nothing about the socket: which
 * address it is bound to is printed nowhere else.  The bound address is the one
 * thing that decides whether a TCP link can work at all - a port bound to
 * 127.0.0.1 is unreachable from the network side, and a link that is bound to
 * the wrong family accepts nothing and explains nothing.  The sysop sees a port
 * that exists and a link that does not.
 *
 * ONE PORT, ONE LINE HERE - one line PER LISTENING SOCKET, because a port with
 * two of them has two addresses and a reader has to know that the second
 * "bound" is not a contradiction of the first but the other socket.  The
 * sessions are the other half of the picture and they are one word away:
 * "ifconfig <iface> stats" holds one block per session, with what each of them
 * has identified itself as, whose connection it is, and what it has sent.  They
 * come and go while a link is being watched, and a section that changes shape is
 * not a thing to read (Thomas).
 */
void
tcpsock_show_verbose(
struct iface *ifp)
{
  struct tcpsock *tp;
  struct tcpsock_listen *ln;
  char abuf[SOCKADDR_STRLEN];

  /* THE PORT, AND NOT THE NEWEST ENTRY.  tcpsock_first() is the most recently
   * built one, and an accepted session is pushed in front of the listener it
   * belongs to - so as soon as a visitor arrived, the visitor was read as the
   * port (it carries no TCF_LISTEN, so the line called it a "client") and the
   * lines above, which say where the port is bound, disappeared along with the
   * listener they belong to.  A port was invisible exactly while somebody was
   * using it.
   *
   * WHAT THE PORT IS is decided by what an entry HOLDS and not by its place in
   * the list: the listening sockets, or the dialing role.  An accepted session
   * holds neither - it came in on a socket somebody else owns (Thomas).
   */
  for (tp = Tcp_socks; tp != NULL; tp = tp->next)
    if (tp->ifp == ifp && (tp->listens != NULL || (tp->flags & TCF_CLIENT)))
      break;
  if (tp == NULL)
    return;
  if (tp->listens) {
    /* A LISTENER HAS NO ONE FAMILY, and it says so per socket: the family is
     * the property of the address that was bound, and "axtcp listener, IPv4"
     * above two sockets of which one is IPv6 is a line that is wrong in half
     * its parts (Thomas).
     */
    for (ln = tp->listens; ln; ln = ln->next) {
      printf("           %s listener, %s port %d\n",
	     tcpsock_portname(tp->portproto),
	     ln->family == AF_INET6 ? "IPv6" : "IPv4", tp->lport);
      printf("           bound %s\n",
	     sockaddr_to_string((struct sockaddr *) &ln->addr, abuf,
				sizeof(abuf)));
    }
  } else {
    printf("           %s %s, %s port %d\n",
	   tcpsock_portname(tp->portproto),
	   (tp->flags & TCF_LISTEN) ? "listener" : "client",
	   (tp->family == AF_INET6) ? "IPv6" : "IPv4",
	   (tp->flags & TCF_LISTEN) ? tp->lport : tp->port);
    /* tp->laddr, and not the config word: the config says what was asked for,
     * laddr says what the socket got.  On a host without the address that was
     * asked for, the attach failed and is long gone; this line is what is left.
     *
     * AND WITH NO ADDRESS AT ALL IT SAYS SO, rather than printing the family
     * zero that an unused sockaddr carries: a client that has not come up yet
     * has no local address, and "bound <af 0>" is not a thing anybody can act
     * on (Thomas).
     */
    if (tp->laddr.ss_family == 0)
      printf("           bound (no session yet)\n");
    else
      printf("           bound %s\n",
	     sockaddr_to_string((struct sockaddr *) &tp->laddr, abuf,
				 sizeof(abuf)));
  }
  /* BOTH ROLES ARE ONE PORT, so both numbers are said: the pair of lines above
   * the "dials" line are the sockets, and a sysop who wrote "listen 8000 client
   * host:8001" has to be able to see that the 8001 next to "dials" is not also
   * the port it answers on (Thomas).
   */
  if ((tp->flags & TCF_CLIENT) && tp->peer != NULL)
    printf("           dials %s port %d\n", tp->peer, tp->port);
}

/*---------------------------------------------------------------------------*/

/* WHAT A PORT NUMBER MEANS when it is a well-known one: 8001 is the
 * KISS-over-TCP port (direwolf, xrouter) and 9393 the AXTCP one.  -1 for every
 * other number, because "unknown" is something a client has to be TOLD about
 * and not something to guess at.
 */
static int
tcpsock_porttype(
int port)
{
  if (port == KISSTCP_DEFAULT_PORT)
    return TCPAD_KISSTCP;
  if (port == AXTCP_DEFAULT_PORT)
    return TCPAD_AXTCP;
  return -1;
}

/* "type=kiss|axtcp|auto", taken off the line before anything reads a position -
 * the same shape as "bind=", "keepalive" and "shared-key", and for the same
 * reason: a word that may only stand in one particular place is one more rule
 * to think about while copying a line out.  It is ONE word with a "=" and not a
 * word with a value, so the gap closes by one.
 *
 * *typep is left at -1 when the word is absent, and the caller decides what
 * that means (see tcpsock_attach()).
 */
static int
tcpsock_take_type(
int *argcp,
char **argv,
int *typep)
{
  int argc = *argcp;
  int i;

  /* FROM ONE, and not from two as the other two helpers: the word may stand in
   * the very place the label would, as in "attach axtcp type=kiss listen
   * 9393", and a word left there would be taken for a name (Thomas).
   */
  for (i = 1; i < argc; i++) {
    if (strncmp(argv[i], "type=", 5) != 0)
      continue;
    if (!strcmp(argv[i] + 5, "auto"))
      *typep = TCPAD_AUTO;
    else if (!strcmp(argv[i] + 5, "axtcp"))
      *typep = TCPAD_AXTCP;
    else if (!strcmp(argv[i] + 5, "kiss"))
      *typep = TCPAD_KISSTCP;
    else {
      printf("\"%s\" is not a dialect: it is type=kiss, type=axtcp or"
	     " type=auto\n", argv[i]);
      return 1;
    }
    memmove(argv + i, argv + i + 1, (size_t) (argc - i - 1) * sizeof(char *));
    *argcp = argc - 1;
    return 0;
  }
  return 0;
}

/* THE USAGE LINE, once: the two attach commands differ in the name they print
 * and in nothing else, and a second copy of this text is a second copy that
 * rots.
 */
static void
tcpsock_attach_usage(
const char *cmd)
{
  printf("Usage: attach %s [<label> [listen [<port>]]]"
	 " [client <host>[:<port>]] [type=kiss|axtcp|auto]"
	 " [bind=<addr>[,<addr>]...] [keepalive <seconds>]"
	 " [shared-key <code>]\n", cmd);
}

/* A LINE THAT DOES NOT DO, before the interface exists: the pieces that were
 * already taken are given back, and the answer is the one an attach returns.
 */
static int
tcpsock_attach_abort(
char *host,
struct tcpsock_bindlist *binds)
{
  if (host)
    free(host);
  tcpsock_bind_free(binds);
  return 1;
}

/* THE ONE ATTACH BEHIND "attach axtcp" AND "attach kisstcp".
 *
 * "attach kisstcp" IS "attach axtcp type=kiss" - not by a copied body but by
 * this call: cmd is the name the sysop wrote (it stands in the usage lines and
 * is the default interface name), and cmdtype is what that name means.
 * "attach axtcp" means NO dialect in particular (TCPAD_AUTO, the superset),
 * the alias means KISS (TCPAD_KISSTCP).  The framing itself is not here; each
 * file keeps its own four hooks and one set of them is hung on below (see
 * tcpsock_hooks_auto()).
 *
 * THE VALUES.  argv[0] is the word after "attach", argv[1] the label - exactly
 * as in axip_attach(), and that is not a subtlety here: an attach that reads
 * its arguments one place too early takes the word "listen" for the label and
 * then fails on the port specification it never read (Thomas).
 *
 * bind= may stand anywhere and is filtered out, as in axip; type=, keepalive
 * and shared-key are the same shape and come out before anything reads a
 * position.
 *
 * WHAT COMES OUT IS ONE POLICY AND TWO PORTS, found in this order (Thomas,
 * 2026-10-19):
 *
 *   1. A WRITTEN "type=" WINS, whatever else stands on the line.
 *   2. Otherwise the COMMAND decides for a port that only LISTENS: "auto" for
 *      "attach axtcp" - that is its reason to exist - and "kiss" for the
 *      alias.  A written port number does NOT pin a listener: 8001 and 9393
 *      are conventions and not laws, and a listener that takes both is what
 *      the sysop asked for by not writing type=.
 *   3. A CLIENT CANNOT BE "auto": it speaks first and would have to frame its
 *      first packet with a dialect it does not know yet, which is a deadlock
 *      (TODO.txt, ENTSCHIEDEN G).  Its dialect comes from its DESTINATION PORT
 *      - 8001 is a KISS and 9393 an AXTCP partner; without a written one the
 *      command's own number stands (9393 for "attach axtcp", 8001 for the
 *      alias).  A written port that is neither of the two is an error, and so
 *      is a written "type=auto" together with a client.
 *
 * THE DEFAULT PORT FOLLOWS THE DIALECT: KISS 8001, AXTCP and auto 9393.  So
 * "attach kisstcp" and "attach axtcp type=kiss" are the same line, and a port
 * that takes both answers on the AXTCP number, where XRouter expects it.
 */
int
tcpsock_attach(
int argc,
char *argv[],
void *p,
const char *cmd,
int cmdtype)
{
  struct iface *ifp;
  struct tcpsock *tp;
  char ifname[64];
  const char *bindword = NULL;
  char *av[12];
  char *host = NULL;
  char hostbuf[1024];
  char servbuf[32];
  struct tcpsock_bindlist *binds = NULL;
  int family = AF_UNSPEC;
  long tmp;
  /* TWO PORT NUMBERS, and not one.  "listen 8000 client host:8001" is one
   * interface that answers on 8000 and calls on 8001, and a single variable
   * would make whichever word came last the answer to both questions - so
   * "listen 20102 client 127.0.0.1:20103" listened on 20103 and said nothing
   * (Thomas).  Both start at 0, which says "not written": what they default to
   * follows the dialect and is worked out below.
   */
  int lport = 0;		/* the port this port ANSWERS on, 0 = not written */
  int dport = 0;		/* the port this port CALLS on, 0 = not written */
  int policy = cmdtype;		/* what the sessions of this port may speak */
  int wtype = -1;		/* the "type=" word, -1 when it was not written */
  int client = 0;
  int listen = 0;
  int ac = 0;
  int i;
  int keepalive = -1;		/* -1 = not written, so the default applies */
  char key[AXALEN];		/* the shared code, if one was written */

  (void) p;
  /* "attach axtcp" IS NOT ITS OWN DIALECT.  Its name says the family, and the
   * family's default is to take both; only the alias pins one.  What a client
   * of THIS command speaks when it names no other follows the same rule, and is
   * worked out where the ports are - the target port 8001 or 9393 says it, and
   * a stranger's port is an error rather than a guess.
   */

  /* "bind=" AND "keepalive" ARE THE TWO WORDS THAT MAY STAND ANYWHERE, and
   * both are taken out here, before anything reads a position.  A word that may
   * only be written in one particular place is an extra rule to think about
   * while copying a line out.
   *
   * "bind=" IS A LIST, and the list is parsed ONCE and then belongs to the port,
   * so that "listen" can make a socket per entry and "client" can leave by the
   * entry of the peer's family out of the same words.
   */
  for (i = 0; i < argc; i++) {
    if (!strncmp(argv[i], "bind=", 5)) {
      if (bindword != NULL) {
	printf("\"bind=\" is written twice - it takes a list, so one word: "
	       "\"bind=%s,%s\"\n", bindword, argv[i] + 5);
	return 1;
      }
      bindword = argv[i] + 5;
      continue;
    }
    if (ac >= (int) (sizeof(av) / sizeof(av[0]))) {
      tcpsock_attach_usage(cmd);
      printf("... and too many words before it.\n");
      return 1;
    }
    av[ac++] = argv[i];
  }
  argc = ac;
  argv = av;
  /* THE KEEPALIVE COMES OUT HERE, next to "bind=" and before anything reads a
   * position: from here on the line is positional, and a word in a position it
   * knows nothing about is an error the sysop would not understand.
   */
  if (tcpsock_take_keepalive(&argc, argv, &keepalive))
    return 1;
  /* AND THE SHARED CODE NEXT TO IT, for the same reason: a word that may only
   * stand in one particular place is an extra rule to think about while
   * copying a line out.
   */
  if (tcpsock_take_sharedkey(&argc, argv, key))
    return 1;
  /* AND THE DIALECT, and it has to come out BEFORE the label is read: written
   * in the label's place - "attach axtcp type=kiss listen 9393" - it would
   * otherwise be taken for a name.
   */
  if (tcpsock_take_type(&argc, argv, &wtype))
    return 1;
  if (wtype >= 0)
    policy = wtype;

  /* "stat" IS NOT A LABEL, and it is not this command's to answer any more.  A
   * status word inside a line that BUILDS something is the one place a sysop
   * should not have to look for a status (Thomas), and what it answers is now
   * "ifconfig <iface> stats", beside "verbose" where the other read-only word
   * has always stood.
   *
   * A PREFIX OF THE WORD IS THE WORD, the same as for every subcommand on the
   * command line - and that is what closes the trap this reserved word is here
   * for in the first place: "attach axtcp st" used to build a port named "st"
   * and say nothing at all.  Nobody who writes "sta" means a name.
   */
  if (argc >= 2 && strncmp("stat", argv[1], strlen(argv[1])) == 0) {
    printf("Statistics do not belong to \"attach\" - use \"ifconfig %s stats\".\n",
	   argc >= 3 ? argv[2] : "<iface>");
    return 1;
  }

  /* "listen" AND "client" ARE NOT LABELS, and a first word that is one of
   * them is the classic mistype: the name comes first, the option second, so
   * "attach axtcp listen 999" builds a port NAMED "listen" - and the next
   * attempt says "Interface listen already exists", a message about a port
   * nobody meant to make (the "stat" case above has the same shape, Thomas).
   */
  if (argc >= 2 &&
      (!strcmp(argv[1], "listen") || !strcmp(argv[1], "client"))) {
    printf("\"%s\" is taken as the interface NAME here - the name stands\n"
	   "first and the option after it: \"attach %s <name> %s ...\"\n",
	   argv[1], cmd, argv[1]);
    return 1;
  }
  if (argc >= 2) {
    strncpy(ifname, argv[1], sizeof(ifname) - 1);
    ifname[sizeof(ifname) - 1] = 0;
  } else {
    strncpy(ifname, cmd, sizeof(ifname) - 1);
    ifname[sizeof(ifname) - 1] = 0;
  }
  if (if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return 1;
  }

  /* THE LIST IS TAKEN APART HERE, before the interface exists: a list that
   * cannot be read is a mistake in the command line, and building the interface
   * first and then refusing it would leave a name that is already half made.
   */
  if ((binds = tcpsock_bind_list(bindword)) == NULL && bindword != NULL)
    return 1;

  /* "listen" AND "client" ARE TWO WORDS, NOT TWO MODES, and both may stand:
   * a node that answers an XRouter AND holds a session to a partner is both of
   * those at once, and it is one interface - one name for "ifconfig", one
   * "keepalive", one thing to trace.  Making them modes meant writing the
   * second word decided everything and the first was ignored, so such a node
   * had to become two interfaces over one port number, and two interfaces
   * cannot hold one port number between them.
   *
   * Either word may come first, and the line is walked word by word until
   * neither is there any more - the same shape as the "bind=" filtering above.
   *
   * NEITHER WORD AT ALL IS A LISTENER, which is the more frequent form and the
   * one where you need not decide on a word.
   */
  listen = 0;
  for (i = 2; i < argc; ) {
    if (!strcmp(argv[i], "listen")) {
      if (listen++)
	goto Usage;
      i++;
      if (i < argc && argv[i][0] != '\0') {
	if (cmd_getnum(argv[i], &tmp) || tmp <= 0 || tmp > 65535) {
	  printf("Port \"%s\" is not a port number\n", argv[i]);
	  return tcpsock_attach_abort(host, binds);
	}
	lport = (int) tmp;
	i++;
      }
    } else if (!strcmp(argv[i], "client")) {
      if (client++)
	goto Usage;
      if (++i >= argc) {
	printf("\"client\" wants a host, as in \"client db0sao.ampr.org:8000\"\n");
	tcpsock_attach_usage(cmd);
	return tcpsock_attach_abort(host, binds);
      }
      /* THE PORT STANDS IN THE HOST, and that is right here: "client" has
       * exactly one target, and a second word for it would be a syllable
       * without content.
       *
       * build_hostport() is what splits it, so an IPv6 literal can be written
       * the way build_sockaddr() wants it - "[::1]:8000" - and the brackets are
       * gone by the time the name reaches getaddrinfo(), which would not take
       * them.  A bare "::1:8000" it refuses instead of guessing: there is no
       * telling the port from the address, and the wrong guess is the one that
       * costs the afternoon.
       */
      if (build_hostport(argv[i], hostbuf, sizeof(hostbuf), servbuf,
		 sizeof(servbuf), &family)) {
	printf("\"%s\" is not a host or a host:port - an IPv6 literal goes in "
	       "brackets, as in [::1]:8000\n", argv[i]);
	return tcpsock_attach_abort(host, binds);
      }
      if (*servbuf) {
	if (cmd_getnum(servbuf, &tmp) || tmp <= 0 || tmp > 65535) {
	  printf("Port \"%s\" is not a port number\n", servbuf);
	  return tcpsock_attach_abort(host, binds);
	}
	dport = (int) tmp;
      }
      if ((host = strdup(hostbuf)) == NULL)
	return tcpsock_attach_abort(host, binds);
      client = 1;
      i++;
    } else {
    Usage:
      /* A WORD TOO MANY IS REFUSED, not left unread.  The line is walked by
       * position, and a word nobody read is a word the sysop believes was
       * honoured: "attach axtcp al listen 19966 19967" bound 19966 and said
       * nothing at all about 19967, which is not a shorter answer than this
       * one.
       */
      tcpsock_attach_usage(cmd);
      if (i < argc)
	printf("... and \"%s\" is one word too many.\n", argv[i]);
      return tcpsock_attach_abort(host, binds);
    }
  }
  /* NEITHER WORD IS A LISTENER, on the default port: that is the more frequent
   * form and the one where you need not decide on a word.
   */
  if (!client)
    listen = 1;

  /* THE DIALECT, AND THE ONE CASE THAT STILL NEEDS ONE.  Everything else is
   * already decided; what is left is a client on a command whose own default
   * is "auto", and a client cannot be "auto" (see the head of this function).
   */
  if (policy == TCPAD_AUTO && client) {
    int wanted;

    if (wtype >= 0) {
      /* WRITTEN, and so an order and not a gap: the sysop asked for both
       * dialects for the sessions of this port, and the client cannot have
       * them.
       */
      printf("A port that takes both dialects cannot hold a client: the client\n"
	     "speaks first and would have to write its first packet in a dialect\n"
	     "it does not know yet.  Write \"type=kiss\" or \"type=axtcp\".\n");
      return tcpsock_attach_abort(host, binds);
    }
    /* NOT WRITTEN, so the DESTINATION PORT decides - that is what the numbers
     * mean and the sysop wrote one.  Without one the command's own number
     * stands, and its dialect is the command's.
     */
    wanted = tcpsock_porttype(dport ? dport : AXTCP_DEFAULT_PORT);
    if (wanted < 0) {
      printf("Port %d says nothing about the dialect, and a client cannot be\n"
	     "\"auto\": it speaks first.  Write \"type=kiss\" or \"type=axtcp\",\n"
	     "or call port %d (kiss) or %d (axtcp)\n",
	     dport, KISSTCP_DEFAULT_PORT, AXTCP_DEFAULT_PORT);
      return tcpsock_attach_abort(host, binds);
    }
    policy = wanted;
  }
  /* THE PORTS NOW THAT THE DIALECT IS KNOWN: what was written stays, and what
   * was not follows it - 8001 for KISS, 9393 for AXTCP and for "auto".
   */
  if (!lport)
    lport = policy == TCPAD_KISSTCP ? KISSTCP_DEFAULT_PORT : AXTCP_DEFAULT_PORT;
  if (!dport)
    dport = policy == TCPAD_KISSTCP ? KISSTCP_DEFAULT_PORT : AXTCP_DEFAULT_PORT;

  if ((ifp = (struct iface *) calloc(1, sizeof(struct iface))) == NULL) {
    if (host)
      free(host);
    tcpsock_bind_free(binds);
    return 1;
  }
  if ((ifp->name = strdup(ifname)) == NULL) {
    free(ifp);
    if (host)
      free(host);
    tcpsock_bind_free(binds);
    return 1;
  }
  ifp->addr = Ip_addr;
  ifp->mtu = 1500;		/* an AX.25 frame fits comfortably */
  ifp->trace = 0;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  /* OUR OWN HARDWARE ADDRESS.  ax_answers_to() compares every incoming
   * destination field with iface->hwaddr, and without that field the first
   * frame that is delivered to us crashes.  The port is ours, so our own
   * callsign goes here (Thomas).
   */
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  /* THE IFTYPE, AND WITHOUT IT THE NODE CRASHES.  After every incoming
   * packet network() fetches ifp->iftype->rcvf; a NULL there is a crash on
   * the first good frame.  "AX25UI" is the same route as on a serial line:
   * the frame arrives without a KISS cap and ax_recv() is the receiver both
   * protocols need.
   */
  setencap(ifp, "AX25UI");
  /* THE WAY OUT, and it is a hook as in axip - the framing stays with the tx
   * hook, because AXTCP must count the CRC into its length field and KISS does
   * not send one along.
   */
  ifp->raw = tcpsock_raw;
  /* THE WIRE CHECKSUM.  crccontrol is NOT the AX.25 frame check sequence:
   * that one is not in the KISS picture at all.  Nothing in the HDLC/LAPB
   * stack computes it (only axip and axtcp append one, both encapsulations
   * with no TNC in the path), nothing takes it off, and ntohax25()
   * (ax25hdr.c:102) consumes the addresses and nothing else.  crccontrol
   * names the KISS dialect's checksum - SMACK, FlexNet, or none.
   *
   * A PORT THAT CAN SPEAK KISS STARTS IN "auto".  CRC_AUTO says "learn which
   * one from the partner, per session, and answer in kind"
   * (kisstcp_crcmode(), and the top of kisstcp.c).  It used to be "off" here,
   * on the argument that a TCP partner sends no checksum - and that argument
   * was wrong for the partner we actually met: DG8NGN speaks FlexNet (type
   * byte 0x20, a FlexNet checksum behind it), and "off" had us refuse every
   * one of his frames as a foreign channel.  Learning costs nothing and is
   * right in both cases: a partner who sends no checksum is learned as none
   * and is answered with none.  A sysop who knows his partner may pin the
   * dialect with "ifconfig <iface> crc off|smack|flexnet".
   *
   * A PINNED AXTCP PORT STAYS AT CRC_CCITT, as it always has: AXTCP carries
   * its own checksum in its length area (axtcp_rx() checks it) and crccontrol
   * is only ever the answer to "which KISS dialect", and there is no KISS
   * session on such a port.
   */
  if (policy == TCPAD_AXTCP)
    ifp->crccontrol = CRC_CCITT;
  else
    ifp->crccontrol = CRC_AUTO;
  ifp->crcfixed = 0;
  /* THE KEEPALIVE, and the default is on: see TCP_KEEPALIVE_DEFAULT for why a
   * line that is quiet needs a packet on it.  The written value wins, and 0 is
   * a written value - "keepalive 0" means off and not "use the default".
   */
  ifp->keepalive = keepalive < 0 ? TCP_KEEPALIVE_DEFAULT : keepalive;
  if (key[0] != '\0')
    memcpy(ifp->sharedkey, key, sizeof(ifp->sharedkey));

  /* ONE PORT WITH BOTH ROLES, and the hooks are one set of four - the same
   * bytes either way.  Only the flags say which half of it is wanted, and the
   * session list is what "attach ... stat" walks.
   *
   * "bind=" BELONGS TO THE PORT AND NOT TO A ROLE, and that is what makes both
   * words work with one list: a listener makes a socket per entry, and the
   * session the client holds leaves by the entry of the peer's family.  A
   * listener and a client on one interface that want different local addresses
   * would need two interfaces - and two interfaces cannot hold one port number
   * between them, which is why they are one interface (Thomas).
   */
  if ((tp = tcpsock_new(ifp, (client ? TCF_CLIENT : 0) | (listen ? TCF_LISTEN : 0),
			policy)) == NULL) {
    if (host)
      free(host);
    tcpsock_bind_free(binds);
    if_detach(ifp);
    return 1;
  }
  /* THE HOOKS OF THE POLICY, and this is the one place where the two files
   * meet: a pinned port gets the four functions of its dialect and nothing
   * else - it refuses the other dialect on the first byte, and that refusal is
   * the security a sysop ordered - and a port that takes both gets the
   * dispatcher (tcpsock_hooks_auto()), which grudges no byte and lets the CRC
   * sort out a partner who guessed wrong.
   */
  if (policy == TCPAD_KISSTCP) {
    tp->rx = kisstcp_rx;
    tp->tx = kisstcp_tx;
    tp->detect = kisstcp_detect;
    tp->keepalive = kisstcp_keepalive;
    ifp->ioctl = kisstcp_ioctl;
  } else if (policy == TCPAD_AXTCP) {
    tp->rx = axtcp_rx;
    tp->tx = axtcp_tx;
    tp->detect = axtcp_detect;
    tp->keepalive = axtcp_keepalive;
  } else {
    tcpsock_hooks_auto(tp);
  }
  /* THE LIST IS THE PORT'S, and not the call's: the rebuild needs it, and a
   * session that rings again has to leave by the address it was told to leave
   * by.  tcpsock_forget() takes it away with the port.
   */
  tp->binds = binds;

  if (listen) {
    if (tcpsock_listen(tp, binds, lport)) {
      /* tcpsock_listen() has said which address and why.  A second sentence
       * here would only repeat the port number at somebody who has just been
       * told the address.
       */
      tcpsock_forget(tp);
      if (host)
	free(host);
      if_detach(ifp);
      return 1;
    }
  }
  if (client) {
    /* WHAT THE BRACKETS SAID, remembered before the first attempt and not
     * only for it: AF_INET6 tells tcpsock_connect() to stop after the v6
     * answers, and a later retry would otherwise be free to walk into an A
     * record for a name that was written with "[...]" on purpose.  Zero means
     * "either family", which is what an unbracketed spelling asks for.
     *
     * AND WITH A LISTENER ALONGSIDE, THE BRACKETS ARE THE PORT'S TOO: they say
     * which family the peer is, and the "bind=" entry of that family is the
     * address the session leaves by.  So a bracketed peer plus "bind=[::]" is
     * a port that listens on [::] and calls out of [::], which is what the two
     * words together say.
     */
    tp->family = family;
    /* A RING THAT FAILS IS NO REASON TO LOSE THE PORT.  tcpsock_connect() says
     * why, once, and the attach carries on; the ticker tries again after the
     * deadline - otherwise after every outage of the peer one would have to
     * type the command once more (Thomas).
     */
    tcpsock_connect(tp, host, dport);
    free(host);
  }
  /* THE TICK, and it has to be started HERE, at the attach.  There is no
   * other place for it: the init call that was meant to do it is gone,
   * because nothing called it.  The consequence of a missing start would
   * be invisible at the attach - the port listens and answers - and only
   * show later: nothing in tcpsock_timer_task() runs, so a client never
   * reconnects, a connection that is not identified never hits its five
   * minutes, and the keepalive never ticks.
   *
   * CALLED FROM BOTH MODULES it is harmless: the first call starts the tick
   * and every further one sees it running (see tcpsock_init()).
   */
  tcpsock_init();
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* WHAT A SESSION'S CHECKSUM IS, in the words "ifconfig <iface> crc" takes.  The
 * port's own setting is the other word on the block, "crc-port:" - and the two
 * differ exactly where the port learns (CRC_AUTO), which is the point of
 * printing both: "crc-port:auto crc:smack" is a port that has learned, and
 * "crc-port:auto crc:off" is one that has not heard a checksummed frame yet
 * (kisstcp.c explains why starting with none is right and not an omission).
 *
 * THE SAME ANSWER kisstcp_crcmode() gives on the sending side, and for the same
 * reason - that one is static in kisstcp.c and this file may not call it, so the
 * question is answered here and has to stay the same (Thomas).
 *
 * AN AXTCP SESSION ALWAYS SAYS "ccitt" and never reads the setting: the CRC-16
 * of the AX.25 frame is part of the payload there and is checked by axtcp_rx(),
 * so a port pinned to "crc off" still checks it.
 *
 * AN UNIDENTIFIED SESSION SAYS "-", and not "off": "off" is an answer, and there
 * is none yet - the partner has not sent a byte that would decide one.
 */
static const char *
tcpsock_session_crc(
struct tcpsock *tp)
{
  if (tp->proto == TCPAD_AXTCP)
    return "ccitt";
  if (tp->proto == TCPAD_KISSTCP) {
    if (tp->ifp != NULL && tp->ifp->crcfixed)
      return crcword(tp->ifp->crccontrol);
    return crcword(tp->crcmode);
  }
  return "-";
}

/*---------------------------------------------------------------------------*/

/* ONE LIST, AND NOT ONE PER DIALECT.  "attach axtcp stat" and "attach kisstcp
 * stat" used to be two lists that could not see each other, which is two
 * answers to the question "what is this port doing" - and a port that takes
 * both dialects had to be listed in one of them by an exception.  After Punkt
 * 2b there is one transport, so there is one list, and WHICH dialect a port
 * pins is a column of it (tcpsock_portname()) rather than a reason to split the
 * table.  It is called from "ifconfig [<iface>] stats" (Thomas).
 */
int
tcpsock_stat(
struct iface *ifp)
{
  struct tcpsock *tp;
  char abuf[SOCKADDR_STRLEN];
  char who[SOCKADDR_STRLEN + 8];
  int now;

  now = secclock();
  for (tp = Tcp_socks; tp; tp = tp->next) {
    if (ifp != NULL && tp->ifp != ifp)
      continue;
    /* "listens:%d" and not one fd, because there is more than one: a port with
     * "bind=0.0.0.0,[::]" has two listening sockets, and a column that can only
     * show one of them is a column that has to lie on half the ports.  Which
     * sockets they are, and what they are bound to, is "ifconfig <iface>
     * verbose"'s line and stays there - this list is one block per session
     * (Thomas).
     *
     * WHO IS ON THIS ENTRY, in the column where the dialed host has always
     * stood: "peer:" is the host a dialed session rings, "from:" the address an
     * accepted session came in on.  One entry has one of them and never both -
     * and an accepted session used to be answered with a bare "-", which is the
     * one thing a list of sessions cannot do without when a port has more than
     * one of them (Thomas).
     */
    if (tp->raddr.ss_family != 0) {
      char rbuf[SOCKADDR_STRLEN];
      sockaddr_to_string((struct sockaddr *) &tp->raddr, rbuf, sizeof(rbuf));
      int rp = sockaddr_port((struct sockaddr *) &tp->raddr);
      if (rp > 0) {
	if (strchr(rbuf, '[')) {
	  if (strstr(rbuf, "]:"))
	    snprintf(who, sizeof(who), "from:%s", rbuf);
	  else
	    snprintf(who, sizeof(who), "from:%s:%d", rbuf, rp);
	} else if (strchr(rbuf, ':')) {
	  /* IPv6 without brackets */
	  if (strchr(rbuf, ':') == strrchr(rbuf, ':'))
	    snprintf(who, sizeof(who), "from:%s:%d", rbuf, rp);
	  else
	    snprintf(who, sizeof(who), "from:%s", rbuf);
	} else {
	  /* IPv4 */
	  snprintf(who, sizeof(who), "from:%s:%d", rbuf, rp);
	}
      } else
	snprintf(who, sizeof(who), "from:%s", rbuf);
    } else if (tp->peer != NULL)
      snprintf(who, sizeof(who), "peer:%s", tp->peer);
    else if (tp->flags & TCF_LISTEN)
      snprintf(who, sizeof(who), "peer:*");
    else
      snprintf(who, sizeof(who), "peer:-");
    /* "crc-port:" IS THE PORT'S SETTING and stands on the port's line, next to
     * the port number it belongs to; what this session has learned is the second
     * word, on the session's line (Thomas).
     */
    if ((tp->flags & TCF_LISTEN) && (tp->flags & TCF_CLIENT))
      printf("  %-12s fd:%d listens:%d listen:%d %s port:%d crc-port:%s\n",
	     tp->ifp ? tp->ifp->name : "-", tp->fd, tp->nlistens, tp->lport,
	     who, tp->port,
	     tp->ifp != NULL ? crcword(tp->ifp->crccontrol) : "-");
    else {
      int lp = -1;
      if (tp->laddr.ss_family != 0)
	lp = sockaddr_port((struct sockaddr *)&tp->laddr);
      else if ((tp->flags & TCF_CLIENT) && tp->lport > 0)
	lp = tp->lport;
      printf("  %-12s fd:%d listens:%d %s", tp->ifp ? tp->ifp->name : "-", tp->fd, tp->nlistens, who);
      if ((tp->flags & TCF_CLIENT) && lp > 0)
	printf(" lport:%d", lp);
      if (tp->flags & TCF_CLIENT)
	printf(" dport:%d", tp->port);
      else if (tp->flags & TCF_LISTEN)
	printf(" port:%d", tp->lport > 0 ? tp->lport : tp->port);
      else
	printf(" port:%d", tp->port);
      printf(" crc-port:%s\n", tp->ifp != NULL ? crcword(tp->ifp->crccontrol) : "-");
    }
/* "in" AND "out" ARE FRAMES AND BYTES ON THE WIRE, and the two halves do not
     * count the same thing: the first number is AX.25 frames handed over, the
     * second is everything that went through the socket - the AXTCP length
     * field, the KISS FENDs, and the keepalive.  So a partner that is alive and
     * idle shows "in:0/12" and not "in:0/0", which is the point: the port must
     * not look dead because it is quiet.  "kp in" below says what those bytes
     * were.
     */
    printf("    type:%s session:%s crc:%s flags:%s%s%s%s in:%ld/%ld out:%ld/%ld\n",
	   tcpsock_portname(tp->portproto),
	   tp->proto == TCPAD_DETECT ? "unidentified" :
	   tp->proto == TCPAD_KISSTCP ? "kisstcp" : "axtcp",
	   tcpsock_session_crc(tp),
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
      printf("    keepalive:%ds kp in:%ld kp out:%ld\n", tp->ifp->keepalive,
	     tp->idlesin, tp->idlesout);
    if (tp->ifp != NULL && tp->ifp->sharedkey[0] != '\0')
      printf("    shared-key:%s\n", tp->ifp->sharedkey);
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

/* THE SHARED CODE OFF AN "attach" LINE, and the twin of
 * tcpsock_take_keepalive(): the line is positional, so an option word at the
 * end is a word nobody in the chain knows what to do with.  Same word taken
 * out, same gap closed, for the same reason.
 *
 * A CODE IS CALLSIGN-SHAPED, because that is the address field it travels in.
 * The range is deliberately narrower than setcall()'s: a plain word of up to
 * six characters, no SSID and no repeats mark - so the code and the frame
 * it encodes into can never disagree about what was written, and a message
 * can quote the word as configured.
 *
 * Returns 0 when the line is usable - with or without the option - and 1
 * when it is not, having said why.
 */
int
tcpsock_take_sharedkey(
int *argcp,
char **argv,
char *key /* at least AXALEN bytes */)
{
  int argc = *argcp;
  int i;
  const char *p;

  key[0] = '\0';
  for (i = 2; i + 1 < argc; i++) {
    if (stricmp(argv[i], "shared-key") != 0)
      continue;
    p = argv[i + 1];
    if (*p == '\0' || strlen(p) > ALEN ||
	strchr(p, '-') != NULL || strchr(p, '*') != NULL) {
      printf("\"%s\" is not a shared code: it has to be a plain word"
	     " of at most %d characters\n", p, ALEN);
      return 1;
    }
    strcpy(key, p);
    memmove(argv + i, argv + i + 2, (size_t) (argc - i - 1) * sizeof(char *));
    *argcp = argc - 2;
    return 0;
  }
  return 0;
}
