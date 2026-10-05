#ifndef _TCPSOCK_H
#define _TCPSOCK_H

/* AXTCP and KISS-over-TCP have the same skeleton and differ only in framing:
 * KISS is self-synchronising (FEND at the start and at the end), AXTCP is not
 * (two bytes of length, and the CRC is the copy).  Everything else -
 * nonblocking, a buffer per session, reconnection, idle timeout, garbage - is
 * here and shared.
 *
 * THE ONE SENTENCE THAT GOVERNS EVERYTHING: TCP is a byte stream.  A frame
 * that arrives incomplete stays incomplete until the rest arrives, and two
 * frames can sit in a single read().  Anyone who trusts a read() boundary
 * here to be a frame boundary loses frames; anyone without a buffer per
 * session here files half a frame into the wrong one (Thomas).
 */

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* System headers without a fallback to "UNIX": the rest of the tree does the
 * same, and an #ifdef UNIX that is never set is an #ifdef that never includes
 * anything - so the file looks complete and is empty.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* What one read() takes in at a time.  Big enough that an ordinary frame
 * arrives in one go, and not so big that a flood fills memory.
 */
#ifndef TCPSOCK_BUFSIZE
#define TCPSOCK_BUFSIZE		4096
#endif

/* CEILING FOR INCOMING DATA.  We do not know what is sending to us: 0xc0
 * followed by ten megabytes of zeroes is valid KISS noise, and an AXTCP length
 * field may claim 65535.  Without this ceiling memory can be filled, and by
 * anyone who reaches the port.
 *
 * The value is not AX.25's maximum but generous enough for the worst a valid
 * KISS stream can be: every frame end becomes an escape plus end, that is
 * twice as much, plus the two KISS bytes.  2048 * 2 + 512 covers even that.
 */
#ifndef TCPSOCK_MAXFRAME
#define TCPSOCK_MAXFRAME	4608
#endif

/* THE FIRST BYTE DECIDES THE PROTOCOL.  0xc0 is the KISS FEND and therefore a
 * KISS-over-TCP peer; anything else can only be the upper half of an AXTCP
 * length field, because 0xc0 as a length would be 49152 bytes (XRouter
 * against BPQ, see the OARC paper).
 */
#define TCPAD_DETECT	0
#define TCPAD_AXTCP	1
#define TCPAD_KISSTCP	2

#define TCF_LISTEN	(1<<0)	/* own listening socket, no fd */
#define TCF_CLIENT	(1<<1)	/* built by us, SYN sent */
#define TCF_CONNECTING	(1<<2)
#define TCF_CONNECTED	(1<<3)
#define TCF_GARBAGE	(1<<4)	/* still unidentified, waiting for its test */

/* TIMES, all in seconds.
 *
 * IDLE AND RECONNECTION.  A byte stream can be quietly torn down by a firewall
 * without our noticing: the RST is filtered, and on the next frame the socket
 * looks open and writes into the void.  Reconnection therefore belongs to the
 * need to send and not to a ticker - a machine with nothing to send does not
 * connect.
 *
 * THE BUILD ITSELF needs patience.  TCP does a SYN and the kernel repeats it
 * on intervals of its own; anyone who rings again after a second is working
 * against that queue and not against the target.  Measured against a dropping
 * firewall: a good two minutes to the timeout.  So our first retry of our own
 * must not be faster than the kernel's.
 */
#define TCP_IDLE_GUARD		3600	/* idle; after that a send rebuilds
					   the session */
#define TCP_GARBAGE_MAX		300	/* unidentified session: 5 minutes,
					   then gone - that is the port scanner */
#define TCP_BACKOFF_FIRST	240	/* first retry of our own, 4 min */
#define TCP_BACKOFF_MAX		1800	/* after that up to half an hour */

/* THE KEEPALIVE, in seconds, and the default for a fresh port.
 *
 * WHY IT IS NEEDED, and it is not theory.  A NAT box drops its mapping after
 * a while of silence without telling either side: the RST that would say so is
 * filtered, and afterwards BOTH ends believe the connection is up.  Then
 * nothing arrives although the sender sent it, and - this is the half that
 * hurts - the sender does not know it either, because it did not write into a
 * dead socket, it wrote into a socket that stopped being a path.  A session
 * that has identified itself has no other deadline at all (TCP_GARBAGE_MAX is
 * only for unidentified ones), so such a pair stays quiet and wrong for ever.
 *
 * SO BOTH SIDES SEND SOMETHING WHEN NOTHING IS HAPPENING.  One packet on a
 * quiet line is not traffic: the NAT keeps its mapping alive, and if it was
 * gone in the meantime the packet either gets through - which rebuilds
 * nothing and at least proves the path - or an RST comes back and the session
 * is rebuilt.  Either way the two ends learn the same thing, which is the
 * whole point.
 *
 * WHAT IS SENT is the one byte sequence that means "nothing to say" in the
 * protocol at hand, and each protocol writes its own: KISS has FEND FEND for
 * it, AXTCP has a frame of zero length with its FCS.  What must NOT be sent is
 * an empty TCP write - a write of zero bytes says nothing at all to the kernel,
 * is not a segment, and would not even reach the NAT.
 *
 * SECONDS, not minutes, and against the precedent of "axip dns-interval" which
 * is in minutes.  A name is not enough here: the shortest NAT timeout anyone
 * should try to survive is well under a minute, and a parameter in minutes can
 * only say "once a minute" or "once two minutes" - there is nothing between
 * zero and sixty, and the fifty seconds is exactly the case that matters.
 *
 * FIVE MINUTES by default, and it is changed per port with "attach axtcp ...
 * keepalive <seconds>"; 0 turns it off completely, the initial packet
 * included.  Five minutes is under every NAT timeout in common use and still
 * only twelve packets an hour.
 */
#define TCP_KEEPALIVE_DEFAULT	300

/* THE RETRY IS NOT BROKEN, IT GROWS: 4, 8, 16, 32, then half an hour, and
 * then it stays half an hour.  After the first build the number counts the
 * attempts since then.
 */
#define TCP_BACKOFF_STEP(n)	((n) < 8 ? (TCP_BACKOFF_FIRST << (n)) : TCP_BACKOFF_MAX)

struct tcpsock {
  struct tcpsock *next;
  struct iface *ifp;		/* who the session belongs to */
  int flags;
  int proto;			/* TCPAD_*: what this SESSION has settled on */
  int portproto;		/* TCPAD_*: what the PORT speaks - on a
				 * listener this is what the sysop ordered, and
				 * proto stays TCPAD_DETECT until the first byte.
				 * Without these two, "attach axtcp stat" cannot
				 * leave out the KISS ports, and the protocol
				 * switch on detection would erase the kind of
				 * port (Thomas). */
  int family;			/* AF_INET or AF_INET6 */

  /* ONE LISTENING SOCKET PER ADDRESS, and not one for all of them: 0.0.0.0,
   * ::1 and a single global address need different sockets, because you
   * cannot bind to ::1 by binding to ::.  The list below is a list for that
   * reason.
   *
   * NO TIMER MAY TOUCH THIS.  The loss of a client closes the session; the
   * loss of a listening socket closes the port, and that is an error in the
   * command, not a timeout (Thomas).
   */
  int listenfd;			/* >= 0 only with TCF_LISTEN */
  int fd;			/* >= 0 from accept() or connect() */
  char *peer;			/* host, for the client and for the display */
  int port;			/* target port, resp. the listening port */
  struct sockaddr_storage laddr;	/* the address that was bound */

  /* THE STREAM.  raw is the unmodified TCP content, len how much of it is
   * already there.  For AXTCP that is needed until length field AND announced
   * byte count are complete; for KISS, until the closing FEND is there.
   */
  unsigned char *buf;
  int len;
  int bufsize;

  /* WHAT STILL HAS TO GO OUT.  An AX.25 socket may be unable to write because
   * the window is full, and the frame is not lost: it waits here, and the
   * write ticker takes it as soon as there is room again.  Without this every
   * full send buffer would be a silent loss.
   */
  unsigned char *wbuf;
  int wlen;
  int wbufsize;

  /* COUNTERS for "axtcp stat" and "kisstcp stat" */
  long framesin;
  long framesout;
  long bytesin;
  long bytesout;
  long resets;
  long crcerr;
  long overruns;
  long garbage;		/* rejected unidentified sessions */
  long idlesin;		/* keepalive packets RECEIVED - see
			 * TCP_KEEPALIVE_DEFAULT */
  long idlesout;	/* keepalive packets SENT, and they are not in
			 * bytesout: that one counts AX.25 frames, and this
			 * is none (Thomas).  With more than one partner on
			 * the port the two are added together and say
			 * nothing about either - which is why the sysop who
			 * wants to read them keeps one partner per port */

  time_t lastrx;
  time_t lasttx;
  time_t garbage_since;	/* when the unidentified session arrived */
  time_t nextrecon;	/* when we ring again, 0 = now */
  int attempts;		/* failed build attempts since then */

  /* THE TWO PROTOCOL HOOKS, and nothing more: tcpsock.c knows how to read,
   * write, wait on and close a socket; it does not know what a frame is.
   * That is the whole difference between axtcp and kisstcp.
   *
   * rx is called as soon as new bytes stand in buf, and must consume as much
   * as is there in terms of complete frames.  tx builds the bytes that go
   * into the stream from one mbuf.
   */
  int (*rx)(struct tcpsock *tp);
  int (*tx)(struct tcpsock *tp, unsigned char *out, int max, struct mbuf **bpp);

  /* AND ON THE FIRST BYTE: axtcp_detect() or kisstcp_detect().  The listener
   * carries both and calls both until one says "that is mine".  Whoever leaves
   * the decision to the sysop cannot accept an XRouter on port 8001 - and
   * whoever does not carry it cannot accept both on one port (Thomas).
   */
  int (*detect)(struct tcpsock *tp);

  /* AND THE KEEPALIVE, which is the third thing a protocol knows and neither of
   * the two above: the bytes that mean "nothing to say" in it.
   * axtcp_keepalive() writes a frame of zero length plus its FCS, four bytes;
   * kisstcp_keepalive() writes FEND FEND, two bytes.  Returns the number of
   * bytes, or -1 if they do not fit.  It gets no mbuf, because there is no
   * frame here - that is the whole point.
   */
  int (*keepalive)(struct tcpsock *tp, unsigned char *out, int max);
};

/* ONE BYTE INTO THE INPUT BUFFER, with the ceiling.  Returns -1 if the frame
 * has burst the ceiling - the caller then closes the session, because a
 * stream longer than the ceiling does not get shorter with several frames
 * either (Thomas).
 */
int tcpsock_push(struct tcpsock *tp, const unsigned char *data, int len);

/* A COMPLETE FRAME WITHOUT CRC - exactly as axip_recv() hands it on.  Both
 * protocols call this here, and do not of their own accord take the route via
 * the address field: the route, the learning and the loop protection are the
 * same ones, and written three times it means twice right and once not.
 */
void tcpsock_input(struct tcpsock *tp, unsigned char *buf, int len, int chan);

/* A FRAME OUT.  Takes the mbuf, calls the tx hook and puts it on the queue
 * when the socket cannot.
 */
int tcpsock_send(struct tcpsock *tp, struct mbuf *bp);

/* A HOOK COMES IN, and the buffer with it.  portproto is the KIND OF PORT and
 * is put in here; the session itself always starts as TCPAD_DETECT, because it
 * only learns what it is at the first byte. */
struct tcpsock *tcpsock_new(struct iface *ifp, int flags, int portproto);
int tcpsock_init(void);
int tcpsock_set_nonblock(int fd);
int tcpsock_listen(struct tcpsock *tp, const char *addr, int port);
int tcpsock_connect(struct tcpsock *tp, const char *host, int port);
void tcpsock_forget(struct tcpsock *tp);
void tcpsock_gone(struct tcpsock *tp);
struct tcpsock *tcpsock_first(struct iface *ifp);
int tcpsock_has_session(struct iface *ifp);

/* THE COUNTERS.  ifp == NULL means "all ports", proto == 0 "all protocols" -
 * which is what "attach axtcp stat" without a label says: the ports of this
 * protocol, without the KISS ports beside them. */
int tcpsock_stat(struct iface *ifp, int proto, int argc, char *argv[], void *p);

/* Takes "keepalive <seconds>" off an attach line and closes the gap behind it,
 * so that the positional parsing of the rest of the line does not have to know
 * that the option exists.  0 = the line is usable, 1 = it is not.
 */
int tcpsock_take_keepalive(int *argcp, char **argv, int *keepal);

/* THE WAY OUT, and both protocols hang it on ifp->raw.  The framing stays with
 * the tx hook: AXTCP counts the CRC into its length field, KISS sends none. */
int tcpsock_raw(struct iface *ifp, struct mbuf **bpp);

#endif /* _TCPSOCK_H */