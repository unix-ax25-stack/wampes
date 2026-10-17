/* @(#) $Id: remote_net.c,v 1.35 2006/02/12 17:49:57 dl9sau Exp $ */

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <syslog.h>
#include <unistd.h>

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "netuser.h"
#include "ax25.h"
#include "lapb.h"
#include "timer.h"
#include "pidfilter.h"
#include "transport.h"
#include "hpux.h"
#include "buildsaddr.h"
#include "rundir.h"
#include "main.h"
#include "cmdparse.h"

extern char Prompt[];
extern struct cmds Cmds[];

struct controlblock {
  int fd;                               /* Socket descriptor */
  char buffer[1024];                    /* Input buffer */
  int bufcnt;                           /* Number of bytes in buffer */
  struct transport_cb *tp;              /* Transport handle */
  int binary;                           /* Transfer is binary (no EOL conv) */
  int restricted;                       /* From the service socket: no
                                         * "command", no "console" */
  int silent;                           /* No status lines at all: success is
                                         * silence, failure is end of file */
  char target[80];                      /* What we are connecting to, for the
                                         * one status line */
  int lastcr;                           /* Previous byte was a CR */
  int crlf;                             /* This client ends its lines CRLF */
  int handover;                         /* Answer a connect with a descriptor
					 * instead of becoming the pipe */
  int dgram;                            /* Every further line is a frame */
  int dgram_pid;
  struct iface *dgram_iface;            /* 0: every AX.25 port */
  int dgram_hdrlen;                     /* header and colon, in front of it */
  int dgram_need;                       /* counted frame: bytes still to come */
  int dgram_paylen;                     /* its length; 0: the payload ends at
                                         * the NUL, as a line does */
  int dgram_fixed;                      /* The header was given once on the
                                         * command line; every line after it
                                         * is payload and nothing else */
  struct ax25 dgram_hdr;
  /* The raw monitor, on its own listener.  A monitor connection only ever
   * RECEIVES: after "monitor ..." it is a sink for frames, and what it sends
   * afterwards is discarded.  See axmon_frame().
   */
  int monitor;                          /* This connection is a monitor */
  int mon_dirs;                         /* IF_TRACE_IN, plus IF_TRACE_OUT */
  struct portlist mon_ports;            /* iface= list; empty: every port */
  struct controlblock *mon_next;        /* the list of monitors */
  /* One frame in flight when the socket had no room for the whole of it.
   * Frames that arrive while this is full are dropped - a monitor must never
   * make the node wait, and a stream that is handed half a frame never lines
   * up again.  See axmon_write().
   */
  unsigned char *mon_buf;
  int mon_len;                          /* bytes of it in all */
  int mon_off;                          /* bytes of it written */
};

struct cmdtable {
  const char *name;                     /* Command name (lower case) */
  int (*fnc)(struct controlblock *cp);  /* Command function */
};

static int fkbd = -1;

/* Every connection that asked to watch the raw frame stream.  Read by
 * axmon_frame(); the rest of the node never looks at it.  See the monitor
 * block below delete_controlblock().
 */
static struct controlblock *Monitors;

/* What we listen on.  Three kinds, and the difference is what may be said:
 *
 * The command channel carries "command" and "console" and is therefore the
 * node's own command line - whoever reaches it can reconfigure everything.
 * It lives in .sockets, mode 0700.
 *
 * The service socket carries connect and datagram and nothing that changes
 * the node.  It lives in the public sockets directory, and there the DIRECTORY
 * is what decides who may reach it - 0750, group of the sysop's choosing.
 *
 * The monitor socket carries "monitor" and nothing else, and it is a sink
 * afterwards: the node writes raw frames to it and never reads a frame back.
 * It lives in sockets/ beside the service socket, so it is reached by the
 * same group - but it is a socket of its own because the two answer different
 * questions.  The service socket decides who may CONNECT; the monitor decides
 * who may WATCH, and whoever may do one of those is not thereby allowed the
 * other.  See the monitor command below.
 *
 * Each entry keeps its own descriptor.  The accept handler is given the
 * entry, not a global - with one shared variable the first connection would
 * hang the node in accept() on the wrong socket.
 */
struct listener {
  char *name;                           /* in build_sockaddr() notation */
  int restricted;                       /* 0 command channel, 1 service
					 * socket, 2 monitor socket - which
					 * commands it takes and, for the two
					 * in sockets/, that the directory
					 * around it is the gate */
  int fd;
};

/* The paths are in the node's directory, which "-d" chooses and TCPDIR is the
 * default for - see Node_dir in main.c.  They cannot be written here as
 * literals: at this point the node's directory is not known yet, and a
 * listener's name is also the socket's path that the axsock command reports
 * on.  listener_paths() fills them in, once, before the first is used.
 */
static char Netcmd_path[512];
static char Axsock_path[512];
static char Axsock_name[512];
static char Monitor_path[512];
static char Monitor_name[512];
static struct listener Listeners[] = {
  { NULL, 0, -1 },
  { NULL, 1, -1 },
  { NULL, 2, -1 },
  { NULL, 0, -1 }
};

static void
listener_paths(void)
{
  char path[512];

  if (Netcmd_path[0] != '\0') return;
  nodedir_path(path, sizeof(path), ".sockets/netcmd");
  snprintf(Netcmd_path, sizeof(Netcmd_path), "unix:%s", path);
  nodedir_path(Axsock_path, sizeof(Axsock_path), "sockets/ax25");
  snprintf(Axsock_name, sizeof(Axsock_name), "unix:%s", Axsock_path);
  nodedir_path(Monitor_path, sizeof(Monitor_path), "sockets/monitor");
  snprintf(Monitor_name, sizeof(Monitor_name), "unix:%s", Monitor_path);
  Listeners[0].name = Netcmd_path;
  Listeners[1].name = Axsock_name;
  Listeners[2].name = Monitor_name;
}

/* The loopback listeners are not among them: they stay closed unless net.rc
 * says so, because a TCP port carries no rights of its own.  Switching it on
 * is the statement that every local account may use the transmitter.
 *
 * THE NAME: "tcp-listen" is the sysop's word for the remote-control gateway,
 * and every identifier below - TCP_AX25_GW_* - says the same.  "axtcp" is
 * taken: that is the carrier that encapsulates AX.25 frames over TCP, a
 * different thing on a different layer (axtcp.c).
 */
#define TCP_AX25_GW_PORT_DEFAULT 8213

static struct listener Tcpax25gw[] = {
  { 0, 1, -1 },                         /* 127.0.0.1 */
  { 0, 1, -1 },                         /* ::1       */
  { 0, 0, -1 }
};

static char Tcpax25gw_addr[2][32];
static int Tcpax25gw_port;              /* 0 while nothing is listening */

/*---------------------------------------------------------------------------*/

static void command_receive(void *arg);
static int tcpax25gw_on(int port);
static void tcpax25gw_off(void);
static void complain(const char *fmt, ...);

static char *getarg(char *line, int all)
{

  char *arg;
  int quote;
  static char *p;

  if (line) p = line;
  while (isspace(*p & 0xff)) p++;
  if (all) return p;
  quote = 0;
  if (*p == '"' || *p == '\'') quote = *p++;
  arg = p;
  if (quote) {
    if (!(p = strchr(p, quote))) p = "";
  } else
    while (*p && !isspace(*p & 0xff)) {
      *p = Xtolower(*p);
      p++;
    }
  if (*p) *p++ = 0;
  return arg;
}

/*---------------------------------------------------------------------------*/

static int command_switcher(struct controlblock *cp, const char *name, const struct cmdtable *tableptr)
{
  int namelen;

  namelen = strlen(name);
  for (; ; ) {
    if (!tableptr->name) return -1;
    if (!strncmp(tableptr->name, name, namelen)) return (*tableptr->fnc)(cp);
    tableptr++;
  }
}

/*---------------------------------------------------------------------------*/

/* SAY WHY, EVERY TIME.  A client that goes takes with it everything it holds
 * - every "listen" entry, and through those the logins that were running
 * fine - so "a client is gone" is the one thing that is never enough to
 * know.  This is the trail for the report that a user is thrown off when
 * somebody else signs on (TODO): the next occurrence names its own cause
 * instead of leaving us to read the code and guess.
 *
 * SYSLOG ALWAYS, CONSOLE ONLY WHEN WE DID IT.  A node under systemd has no
 * console at all - printf lands on the /dev/null that hpux.c put there - so
 * syslog is the channel that is always there and journalctl is where one
 * looks.  The console is different: whoever sits on cnet is usually a
 * SCRIPT, and a line that appears in its output because some other client
 * closed lands in the middle of what it is parsing.  Ordinary closes
 * therefore stay in the log; only <loud>, which is remote_net_drop_client()
 * and so a decision of ours, also goes to the console, where it belongs -
 * that one is rare, and the sysop wants to see it happen.
 */

static void delete_controlblock(struct controlblock *cp, int loud,
				const char *reason)
{
  char line[256];

  snprintf(line, sizeof(line), "client fd %d%s%s closed: %s", cp->fd,
	   *cp->target ? " for " : "", cp->target,
	   reason ? reason : "no reason given");
  if (loud)
    complain("%s", line);
  else
    syslog(LOG_NOTICE, "%s", line);
  /* Whatever this client was listening for falls free with it. */
  axlisten_client_release(cp->fd);
  if (cp->monitor) {
    struct controlblock **pp;

    for (pp = &Monitors; *pp != NULL; pp = &(*pp)->mon_next)
      if (*pp == cp) {
	*pp = cp->mon_next;
	break;
      }
    free(cp->mon_buf);
    portlist_free(&cp->mon_ports);
  }
  off_read(cp->fd);
  off_write(cp->fd);
  close(cp->fd);
  free(cp);
}

/*---------------------------------------------------------------------------*/

/* Close a client, named by its descriptor.  The listener bookkeeping in
 * axserver.c needs this: when a local service takes a callsign over, the
 * forwarding entry goes, and the client holding it has to be told.  On a
 * binary stream there is no way to say it in words - a text line would
 * corrupt the very stream we are protecting - so the close IS the message.
 *
 * The control block is not in a list of its own; the I/O layer has it as the
 * argument it will pass to command_receive(), so ask there.  Checking the
 * function as well makes sure we are looking at one of ours.
 */

/* One received datagram to a client: the TNC2 header, a byte count instead
 * of the payload, the client's own line ending, and then exactly that many
 * raw bytes with nothing after them.
 *
 *     [37]DL9SAU>APRS,WIDE1-1*:<37 bytes, whatever is in them>
 *
 * The count goes in front, at position 0, so that the first byte of a line
 * already says which of the two forms this is - no payload can be mistaken
 * for a length that way, not even a beacon whose text is a number.  The
 * header ends at the first colon and the bytes follow immediately; there is
 * no line ending between them and none behind them, because either would be
 * a byte nobody counted.
 *
 * Counted rather than delimited because a UI payload may hold CR and NL.
 * With a plain line format, "DL9SAU>APRS:test\nDL9SAU-2>APRS:foo" would
 * reach the client as TWO frames, the second under a source callsign the
 * sender chose - anybody on the air could forge frames that way.
 *
 * Whole frames or none.  A half written one would put the stream out of step
 * for good, and there is no way back: with an 8-bit clean payload any byte
 * sequence may look like a header.  Losing a whole datagram is what
 * datagrams do.
 */

/* All of it or a failure that names itself.  A short write is not an error
 * to write(2) and leaves errno untouched - here it is one, because it ends
 * the frame half sent.
 */

static int write_all(int fd, const void *buf, unsigned len)
{
  int n = write(fd, buf, len);

  if (n < 0) return -1;
  if ((unsigned) n != len) {
    errno = EIO;
    return -1;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int remote_net_send_frame(int fd, const char *hdr, struct mbuf *bp)
{
  char line[128];
  int crlf = 0;
  int len;
  int n;
  struct controlblock *cp;
  struct mbuf *p;

  /* EVERY WAY OUT OF HERE LEAVES A TRUTHFUL errno, because the caller puts
   * it in the line that says why the client was dropped.  write() sets it
   * on a real failure; the other two exits have to set it themselves, or
   * the log would name whatever errno happened to be holding from before.
   */
  if (fd < 0) {
    errno = EBADF;
    return -1;
  }
  /* Answer in the terminator this client uses - it said so with its first
   * line.  Only the header carries one; behind the payload there is nothing.
   */
  if ((cp = (struct controlblock *) on_read_arg(fd)) && cp->fd == fd)
    crlf = cp->crlf;
  len = (int) len_p(bp);
  (void) crlf;                          /* nothing is terminated here */
  n = snprintf(line, sizeof(line), "[%d]%s:", len, hdr);
  if (n <= 0 || n >= (int) sizeof(line)) {
    errno = EMSGSIZE;
    return -1;
  }

  if (write_all(fd, line, (unsigned) n)) return -1;
  for (p = bp; p; p = p->next)
    if (p->cnt && write_all(fd, p->data, p->cnt)) return -1;
  return 0;
}

/*---------------------------------------------------------------------------*/

void remote_net_drop_client(int fd, const char *reason)
{
  struct controlblock *cp;

  if (fd < 0 || !(cp = (struct controlblock *) on_read_arg(fd))) return;
  if (cp->fd != fd) return;             /* not a client control block */
  delete_controlblock(cp, 1, reason);
}

/*---------------------------------------------------------------------------*/

/* THE RAW MONITOR.
 *
 * What a kernel AX.25 port gives an AF_PACKET socket, the node gives these:
 * the frames themselves.  Every monitor connection is on the list below, and
 * the places that already count a frame for an interface (ax_recv() on the
 * way in, and each frame-producing path on the way out) call axmon_frame().
 * There is no per-frame allocation and no work at all while the list is
 * empty - a node nobody watches pays one null check.
 *
 * The FORM is the same for every monitor:
 *
 *	[4-byte big-endian length][interface name][NUL][AX.25 frame]
 *
 * The length, because a frame holds NUL bytes and the end of it would
 * otherwise be nowhere; the name, because a node has several interfaces and
 * a kernel monitor gives its name too.  What is IN the frame is the whole
 * AX.25 frame - addresses, control field, PID and information - exactly what
 * arrived or left, so listen(1) and mheard(8) decode the original with no
 * help from anybody.  There is no direction bit and no frame invented here:
 * "output" is a separate order, not a flag on a frame (see the handshake).
 *
 * A FRAME IS WRITTEN WHOLE OR NOT AT ALL.  A stream keeps no write
 * boundaries, so half a frame leaves the reader looking for the length of
 * the next one in the middle of this one's payload, and no later byte can put
 * that right.  The whole frame is tried at once, and what the socket would not
 * take is staged for on_write() - one frame, and while it is staged the
 * frames that arrive are DROPPED.  That is the difference from a login: a
 * monitor holds nothing (no session, no login), so losing frames is what it
 * does when it stops reading, and it is never a reason to close it.  The node
 * never waits for a monitor, and a monitor never makes the node wait.
 */

/* The record: four bytes of length, the interface name and its NUL, then the
 * frame.  The frame is one byte shorter than a reader's payload buffer, which
 * carries a KISS data byte in front of it (see libax25/wampes.c): a frame of
 * AXMON_FRAME_MAX bytes would make a payload one byte too long for a reader
 * that asked for AXMON_FRAME_MAX, and a frame that long is not an AX.25 frame
 * anyway.  The bound is here so that reader never has to drop one. */
#define AXMON_NAME_MAX   64
#define AXMON_FRAME_MAX  1500
#define AXMON_REC_MAX    (4 + AXMON_NAME_MAX + 1 + AXMON_FRAME_MAX)

static void axmon_writable(void *arg);

/* Give up on a monitor whose socket failed.  A slow reader is not this - a
 * full socket is handled by staging - but a closed or reset one has no
 * reader left at all, and saying why is better than a silent leak.
 */
static void axmon_lost(struct controlblock *cp)
{
  char reason[128];

  snprintf(reason, sizeof(reason), "monitor not written: %s",
	   strerror(errno));
  remote_net_drop_client(cp->fd, reason);
}

static void axmon_write(struct controlblock *cp, const unsigned char *rec,
			int len)
{
  int n;

  /* A frame is still on its way out: this one is lost.  Dropping it, and not
   * queueing it, is what keeps a monitor that stopped reading from growing
   * anything on the node.
   */
  if (cp->mon_buf != NULL) return;

  n = write(cp->fd, rec, (unsigned) len);
  if (n == len) return;
  if (n < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
      n = 0;                            /* nothing written: stage it all */
    else {
      axmon_lost(cp);
      return;
    }
  }
  /* A short write, or none at all.  Keep the rest and let on_write() finish
   * it, one whole frame at a time.
   *
   * If the copy for the rest fails, nothing can finish the record that is
   * already half on the wire - and half a record leaves the reader reading
   * the next length out of this one's payload, for good.  So a partial write
   * with no memory drops the client; a write that took nothing at all can
   * simply lose the frame.
   */
  if (!(cp->mon_buf = (unsigned char *) malloc((size_t) len))) {
    if (n > 0) axmon_lost(cp);
    return;
  }
  memcpy(cp->mon_buf, rec, (size_t) len);
  cp->mon_len = len;
  cp->mon_off = n;
  on_write(cp->fd, axmon_writable, cp);
}

static void axmon_writable(void *arg)
{
  struct controlblock *cp = (struct controlblock *) arg;
  int n;

  if (cp->mon_buf == NULL) {
    off_write(cp->fd);
    return;
  }
  n = write(cp->fd, cp->mon_buf + cp->mon_off,
	    (unsigned) (cp->mon_len - cp->mon_off));
  if (n > 0)
    cp->mon_off += n;
  else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
		     errno == EINTR))
    return;                             /* still no room: wait for the next */
  else {
    axmon_lost(cp);
    return;
  }
  if (cp->mon_off < cp->mon_len) return;
  free(cp->mon_buf);
  cp->mon_buf = NULL;
  cp->mon_len = 0;
  cp->mon_off = 0;
  off_write(cp->fd);
}

void axmon_frame(struct iface *ifp, int dir, struct mbuf *bp)
{
  /* Built once and handed to every monitor that wants it.  Static because
   * the node is single threaded and nothing below calls back into here.
   */
  static unsigned char rec[AXMON_REC_MAX];
  struct controlblock *cp;
  struct controlblock *next;
  struct mbuf *p;
  unsigned char *q;
  int framelen, namelen, left, off, total;

  if (Monitors == NULL || bp == NULL || ifp == NULL) return;
  /* The interface it is on the wire on - the one whose ax25sndcnt the
   * senders move.  iface may only be passing the frame on to its "forw".
   */
  if (ifp->forw != NULL) ifp = ifp->forw;
  if (ifp->name == NULL) return;

  framelen = (int) len_p(bp);
  if (framelen <= 0 || framelen >= AXMON_FRAME_MAX) return;
  namelen = (int) strlen(ifp->name);
  if (namelen > AXMON_NAME_MAX) namelen = AXMON_NAME_MAX;

  rec[0] = (unsigned char) (framelen >> 24);
  rec[1] = (unsigned char) (framelen >> 16);
  rec[2] = (unsigned char) (framelen >> 8);
  rec[3] = (unsigned char) framelen;
  memcpy(rec + 4, ifp->name, (size_t) namelen);
  rec[4 + namelen] = 0;
  off = 4 + namelen + 1;
  q = rec + off;
  for (p = bp, left = framelen; p != NULL && left > 0; p = p->next) {
    int cnt = (int) p->cnt;

    if (cnt > left) cnt = left;
    memcpy(q, p->data, (size_t) cnt);
    q += cnt;
    left -= cnt;
  }
  total = off + framelen;

  for (cp = Monitors; cp != NULL; cp = next) {
    /* axmon_write() can drop this monitor (a dead socket), and dropping it
     * frees it and takes it off Monitors - so the next one is read BEFORE the
     * write, not after the free.
     */
    next = cp->mon_next;
    if (!(cp->mon_dirs & dir)) continue;
    if (!portlist_allows(&cp->mon_ports, ifp)) continue;
    axmon_write(cp, rec, total);
  }
}

/*---------------------------------------------------------------------------*/

static void transport_try_send(void *arg)
{
  struct controlblock *cp = (struct controlblock *) arg;

  int cnt;
  struct mbuf *bp;

  cnt = transport_send_space(cp->tp);
  if (cnt <= 0) {
    off_read(cp->fd);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = read(cp->fd, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(&bp);
    off_read(cp->fd);
    transport_close(cp->tp);
    return;
  }
  bp->cnt = cnt;
  transport_send(cp->tp, bp);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall(struct transport_cb *tp, int cnt)
{

  char buffer[1024];
  struct controlblock *cp;
  struct mbuf *bp;

  cp = (struct controlblock *) tp->user;
  transport_recv(tp, &bp, 0);
  while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
    if (write(cp->fd, buffer, (unsigned) cnt) != cnt) transport_close(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall(struct transport_cb *tp, int cnt)
{
  struct controlblock *cp;

  cp = (struct controlblock *) tp->user;
  on_read(cp->fd, transport_try_send, cp);
}

/*---------------------------------------------------------------------------*/

/* Status lines, terminated, before the first byte of payload and only there.
 * The wording follows XNET and RMNC, which operators have been reading for
 * thirty years.  Note where the stars go: they mark the answer, not the
 * progress, so "link setup ..." carries none and the outcome does.  A program
 * reads lines until one begins with "***" - that is the one it acts on.
 *
 * Afterwards the stream is pure, so "binary" stays unambiguous, and a link
 * lost later is end of file and nothing else - a trailing line could not be
 * told apart from data.
 */

/* One line and one descriptor, in a single sendmsg().  Together, because a
 * descriptor arriving on its own would have to be matched against a line
 * arriving separately and there is no key to match them with.
 */

static void pass_fd(struct controlblock *cp, int fd, const char *fmt, ...)
{
  char buf[256];
  int n;
  struct cmsghdr *cm;
  struct iovec iov;
  struct msghdr msg;
  va_list ap;
  union {
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } control;

  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (cp->crlf) buf[n++] = '\r';
  buf[n++] = '\n';

  memset(&msg, 0, sizeof(msg));
  memset(&control, 0, sizeof(control));
  iov.iov_base = buf;
  iov.iov_len = (size_t) n;
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control.buf;
  msg.msg_controllen = sizeof(control.buf);
  cm = CMSG_FIRSTHDR(&msg);
  cm->cmsg_level = SOL_SOCKET;
  cm->cmsg_type = SCM_RIGHTS;
  cm->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cm), &fd, sizeof(fd));
  (void) sendmsg(cp->fd, &msg, 0);
}

/*---------------------------------------------------------------------------*/

static void say(struct controlblock *cp, const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  int n;

  if (cp->silent) return;
  va_start(ap, fmt);
  n = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
  va_end(ap);
  if (n < 0) return;
  if (cp->crlf) buf[n++] = '\r';
  buf[n++] = '\n';
  write(cp->fd, buf, n);
}

/*---------------------------------------------------------------------------*/

static const char *why(void)
{
  switch (Net_error) {
  case CON_EXISTS: return "busy";       /* one link per callsign pair */
  case NO_CONN:    return "noconn";
  case CON_CLOS:   return "closing";
  case NO_MEM:     return "nomem";
  case NOPROTO:    return "noproto";
  case INVALID:    return "invalid";
  case NO_ROUTE:   return "no route";
  default:         return "failed";
  }
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall(struct transport_cb *tp)
{
  struct controlblock *cp = (struct controlblock *) tp->user;

  if (tp->connected) {
    if (cp->handover) {
      /* The link stands; give it away and go back to taking commands.  The
       * answer and the descriptor travel in one sendmsg, as they do for an
       * incoming call, so there is nothing to match up.
       */
      int fd = -1;

      if (!tp->svc || axserv_pipe_attach(tp->svc, cp->binary, cp->fd, &fd)) {
	say(cp, "*** link failure with %s - nomem", cp->target);
	transport_close(tp);
	return;
      }
      pass_fd(cp, fd, "*** connected to %s", cp->target);
      close(fd);
      transport_detach(tp);             /* the pipe owns the link now */
      cp->tp = 0;
      cp->handover = 0;
      on_read(cp->fd, command_receive, cp);
      return;
    }
    say(cp, "*** connected to %s", cp->target);
    return;
  }
  delete_controlblock(cp, 0, why());       /* busy, noconn, closing, ... */
  transport_del(tp);
}

/*---------------------------------------------------------------------------*/

static int ascii_command(struct controlblock *cp)
{
  cp->binary = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int binary_command(struct controlblock *cp)
{
  cp->binary = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* "sessions" and "close <handle>": what WAMPES is forwarding for programs,
 * and letting one of them go.  A descriptor served by a node has no
 * SIOCAX25CTLCON to reach - axkill(8) and axctl(8) go to AGWPE - so the
 * service socket is where a bridge asks, and where their request lands once
 * it is translated.  See documentation/wampes-sessions-in-netd.md.
 *
 * One session is one line, and the list ends with a "***" line: a reader is
 * handed rows until it reads that one.  The service socket has no prompt and
 * the answer is as long as the table is long, so without the final line a
 * reader could not tell a short answer from one cut off mid-stream - and a
 * row never begins with "*", so the last line is never taken for one.  The
 * handle is whatever "sessions" showed, and it stays the same for as long as
 * the forwarding lasts.
 */

static void sessions_line(void *arg, const char *line)
{
  say((struct controlblock *) arg, "%s", line);
}

static int sessions_command(struct controlblock *cp)
{
  axserv_sessions(sessions_line, cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int close_command(struct controlblock *cp)
{
  char *handle = getarg(0, 0);

  if (handle == 0 || *handle == 0) {
    say(cp, "*** close needs the handle from \"sessions\"");
    return 0;
  }
  if (axserv_session_close(handle) != 0)
    say(cp, "*** no session %s", handle);
  return 0;
}

/*---------------------------------------------------------------------------*/

/* "monitor [iface=<list>] [input|output]" - become a raw monitor.
 *
 * The default is input: what ARRIVED on the port, which is the value of a
 * kernel ETH_P_AX25 socket and of listen(1) with no -a - the traffic a heard
 * list is made of.  "output" adds our own transmissions, the value of
 * ETH_P_ALL and of "listen -a".  The two words are trace's, so the same two
 * say the same thing in the node's own "trace" command.
 *
 * The iface list is the one "listen ax25 add" already takes: comma separated,
 * a "!" in front of the whole list to exclude, "all" for every port, and
 * nothing at all for every port as well.  It is answered HERE and not at the
 * client: a monitor that asked for one interface is not sent the others.
 *
 * Not a datagram listen, and not accidentally: a datagram client claims a
 * callsign, a monitor watches an interface.  The frames go out whole and
 * unaltered, destination and all, so an APRS beacon addressed to APRS - which
 * has nothing to do with the monitor's own callsign - is shown to everyone
 * who may watch that port.  That is what a monitor is for, and why it has a
 * socket and a right of its own.
 */
static int monitor_command(struct controlblock *cp)
{
  char copy[sizeof(cp->buffer)];
  char err[160];
  char *argv[16];
  char *p;
  const char *ifaces = NULL;
  int dirs = IF_TRACE_IN;
  int argc;
  int i;

  strcpy(copy, getarg(0, 1));
  for (argc = 0, p = strtok(copy, " \t");
       p != NULL && argc < (int) (sizeof(argv) / sizeof(argv[0]));
       p = strtok(NULL, " \t"))
    argv[argc++] = p;

  for (i = 0; i < argc; i++) {
    if (!strncasecmp(argv[i], "iface=", 6)) {
      ifaces = argv[i] + 6;
      continue;
    }
    if (!strcasecmp(argv[i], "input")) {
      dirs = IF_TRACE_IN;
      continue;
    }
    if (!strcasecmp(argv[i], "output")) {
      dirs = IF_TRACE_IN | IF_TRACE_OUT;
      continue;
    }
    say(cp, "*** \"%s\": \"monitor\" takes iface=<list>, input or output",
	argv[i]);
    return -1;
  }
  if (portlist_set(&cp->mon_ports, ifaces, err, sizeof(err))) {
    say(cp, "*** %s", err);
    return -1;
  }
  cp->mon_dirs = dirs;
  /* A second "monitor" line on the same connection is a command too: it
   * replaces the list and the directions rather than adding a second entry.
   */
  if (!cp->monitor) {
    cp->monitor = 1;
    cp->mon_next = Monitors;
    Monitors = cp;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int command_command(struct controlblock *cp)
{

  char *cmdbuf;
  int fderr_save;
  int fdout_save;

  cmdbuf = getarg(0, 1);
  fflush(stdout);
  fflush(stderr);
  fdout_save = dup(1);
  fderr_save = dup(2);
  dup2(cp->fd, 1);
  dup2(cp->fd, 2);
  cmdparse(Cmds, cmdbuf, 0);
  fflush(stdout);
  fflush(stderr);
  dup2(fdout_save, 1);
  dup2(fderr_save, 2);
  close(fdout_save);
  close(fderr_save);
  return -1;
}

/*---------------------------------------------------------------------------*/

/* "handover" - answer the next connect with a descriptor rather than becoming
 * the pipe for it.
 *
 * A connect turns this connection into the data path, and this connection is
 * a byte stream: frames arrive packed as full as they will go.  That is
 * right for a terminal and wrong for a protocol that reads the end of an
 * uncompressed block off the end of a frame, which is what FBB forwarding
 * does.  A descriptor we make ourselves can carry the boundaries, so this
 * says "give me one" - and the command channel stays a command channel.
 */

static int handover_command(struct controlblock *cp)
{
  cp->handover = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* "listen <call> [pid=<n>]" - ask to be given incoming calls to a callsign.
 *
 * The sysop's "listen ax25 add <call> client" line is the permission; this is
 * the claim against it.  A callsign nobody configured cannot be claimed, so
 * a client cannot take the node's login or a neighbour's mailbox away by
 * asking first.
 *
 * The claim lives as long as this connection.  Nothing has to be cleaned up
 * by hand when a client dies, and nothing survives it either.
 */

static int listen_command(struct controlblock *cp)
{

  char *argv[8];
  char copy[sizeof(cp->buffer)];
  char err[120];
  char *p;
  int argc;
  int i;
  int pid = PID_NO_L3;
  int ui = 0;
  uint8 call[AXALEN];

  strcpy(copy, getarg(0, 1));
  for (argc = 0, p = strtok(copy, " \t");
       p && argc < (int) (sizeof(argv) / sizeof(argv[0]));
       p = strtok(NULL, " \t"))
    argv[argc++] = p;

  memset(call, 0, sizeof(call));
  for (i = 0; i < argc; i++) {
    /* Connections or datagrams - the same words the config line uses. */
    if (!strcmp(argv[i], "UI") || !strcmp(argv[i], "ui")) { ui = 1; continue; }
    if (!strcmp(argv[i], "I")  || !strcmp(argv[i], "i"))  { ui = 0; continue; }
    if (!strncmp(argv[i], "pid=", 4)) {
      /* Names as well as numbers, the same ones the console takes - see
       * pidfilter.c.  The answer keeps the number, because that is what
       * doc/LIBAX25-BRIDGE.md documents and a client may be reading it.
       */
      if ((pid = pid_number(argv[i] + 4)) < 0) {
	say(cp, "*** invalid pid \"%s\"", argv[i] + 4);
	return 0;
      }
      continue;
    }
    if (call[0]) {
      say(cp, "*** unexpected \"%s\"", argv[i]);
      return 0;
    }
    if (setcall(call, argv[i])) {
      say(cp, "*** invalid call \"%s\"", argv[i]);
      return 0;
    }
  }
  if (!call[0]) {
    say(cp, "*** no callsign");
    return 0;
  }
  if (axlisten_client_claim(call, pid, ui, cp->fd, err, sizeof(err))) {
    say(cp, "*** %s", err);
    return 0;
  }
  {
    char buf[AXBUF];

    say(cp, "*** listening on %s pid 0x%02x", pax25(buf, call), pid);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int connect_command(struct controlblock *cp)
{

  char *argv[64];
  char *rest;
  char copy[sizeof(cp->buffer)];
  char err[200];
  char *p;
  int argc;
  int pid;
  int silent;
  struct ax25 hdr;
  struct ax25_opts opts;

  /* The whole rest of the line: everything new lives inside it, and getarg()
   * lowercases and truncates what it hands out, so work on a copy.
   */
  rest = getarg(0, 1);
  strcpy(copy, rest);
  for (argc = 0, p = strtok(copy, " \t");
       p && argc < (int) (sizeof(argv) / sizeof(argv[0]));
       p = strtok(NULL, " \t"))
    argv[argc++] = p;

  if (!argc) {
    say(cp, "*** link failure - nothing to connect to");
    return -1;
  }
  for (p = argv[0]; *p; p++)            /* the keyword, however typed */
    *p = Xtolower(*p & 0xff);

  /* NET/ROM has no port, no pid and no digipeater path, and TCP has none of
   * those either and its own notion of a port besides.  Both keep the syntax
   * they always had rather than gaining options that could only be refused.
   */
  if (!strcmp(argv[0], "netrom") || !strcmp(argv[0], "tcp")) {
    char *protocol = argv[0];
    char *address = rest;

    while (*address && !isspace(*address & 0xff)) address++;
    while (*address && isspace(*address & 0xff)) address++;
    cp->tp = transport_open(protocol, address, transport_recv_upcall,
			    transport_send_upcall, transport_state_upcall,
			    (char *) cp);
    if (!cp->tp) {
      say(cp, "*** link failure with %s - %s", address, why());
      return -1;
    }
    strncpy(cp->target, address, sizeof(cp->target) - 1);
    say(cp, "link setup (%s)...", protocol);
    if (!cp->binary) {
      cp->tp->recv_mode = EOL_LF;
      cp->tp->send_mode = !strcmp(protocol, "tcp") ? EOL_CRLF : EOL_CR;
    }
    on_read(cp->fd, transport_try_send, cp);
    return 0;
  }

  /* Everything else is AX.25; the parser knows the rest of the grammar,
   * the optional protocol word included.
   */
  if (ax25_parse_target(argc, argv, &hdr, &opts, &pid, &silent,
			err, sizeof(err))) {
    cp->silent = 0;                     /* a parse error is always worth saying */
    say(cp, "*** link failure - %s", err);
    return -1;
  }
  cp->silent = silent;

  pax25(cp->target, hdr.dest);
  cp->tp = transport_open_target(&hdr, &opts, pid, transport_recv_upcall,
				 transport_send_upcall, transport_state_upcall,
				 (char *) cp);
  if (!cp->tp) {
    say(cp, "*** link failure with %s - %s", cp->target, why());
    return -1;
  }
  /* Which port it went out of - the router may have chosen it, and then this
   * is the only place an operator gets to see which.
   */
  say(cp, "link setup (%s)...",
      cp->tp->cb.axp && cp->tp->cb.axp->iface ?
      cp->tp->cb.axp->iface->name : "routed");

  /* Anything that is not plain text is a protocol, and EOL conversion would
   * corrupt it - flextalk was only ever the one case somebody happened to
   * hit.  The listen side has said the same all along.
   */
  if (pid != PID_NO_L3) cp->binary = 1;
  if (!cp->binary) {
    cp->tp->recv_mode = EOL_LF;
    cp->tp->send_mode = EOL_CR;
  }
  on_read(cp->fd, transport_try_send, cp);
  return 0;
}


/*---------------------------------------------------------------------------*/

/* "datagram [<port>:] [--port <p>] [--pid <n>] [--silent]"
 *
 * Afterwards every line is a frame, written the way every tool outside WAMPES
 * writes one:
 *
 *     DL9SAU>APRS,WIDE2-2:xxxxxxxx
 *
 * The source and the path come out of the data, not out of the command, which
 * is what makes forwarding possible at all:
 *
 *     ( echo "datagram hf1:"; cat ) < /dev/aprsport | socat - tcp:localhost:8213
 *
 * The port is a filter here, not a requirement.  There is no connection and
 * no state, so the same frame going out of two ports harms nothing - unlike a
 * second link to the same station, which earns a FRMR.  Left out, the node
 * sends on every AX.25 port it has.
 */

static int datagram_command(struct controlblock *cp)
{

  char *argv[64];
  char *p;
  char copy[sizeof(cp->buffer)];
  char err[200];
  int argc;
  int hasdest;
  int i;
  int pid;
  int silent;
  struct ax25_opts opts;

  strcpy(copy, getarg(0, 1));
  for (argc = 0, p = strtok(copy, " \t");
       p && argc < (int) (sizeof(argv) / sizeof(argv[0]));
       p = strtok(NULL, " \t"))
    argv[argc++] = p;

  cp->dgram_pid = PID_NO_L3;
  cp->dgram_iface = 0;
  cp->dgram_fixed = 0;

  /* Is there an address, or only a port and some options?  With an address
   * the line reads exactly like a connect and the header is fixed for the
   * whole session; without one every line brings its own, in TNC2 form.
   */
  hasdest = 0;
  for (i = 0; i < argc; i++) {
    if (!strncmp(argv[i], "--", 2)) {
      if (!strcmp(argv[i], "--port") || !strcmp(argv[i], "--pid")) i++;
      continue;
    }
    if (strcmp(argv[i], "via")
	&& !((p = strchr(argv[i], ':')) && !p[1])) {
      hasdest = 1;
      break;
    }
  }

  if (hasdest) {
    if (ax25_parse_target(argc, argv, &cp->dgram_hdr, &opts, &pid, &silent,
			  err, sizeof(err))) {
      cp->silent = 0;                   /* a parse error is always worth saying */
      say(cp, "*** %s", err);
      return -1;
    }
    cp->dgram_iface = opts.iface;
    /* No source of its own: leave it empty and let ax_send_ui() stamp the
     * callsign of the port it actually goes out of - the same rule as for a
     * connect, and it does the right thing when a beacon leaves by several
     * ports at once, each under its own callsign.
     */
    if (!opts.ownsource) memset(cp->dgram_hdr.source, 0, AXALEN);
    cp->dgram_pid = pid;
    cp->dgram_fixed = 1;
    if (silent) cp->silent = 1;
  } else {
    /* Port and options only.  Read them here rather than lending the parser
     * a destination it does not need.
     */
    for (i = 0; i < argc; i++) {
      char *port = 0;

      if (!strcmp(argv[i], "--silent")) { cp->silent = 1; continue; }
      if (!strcmp(argv[i], "--pid")) {
	/* Names as well as numbers, the same table as everywhere else - see
	 * pidfilter.c.  This branch had been missed when the others were
	 * changed, which is exactly the drift the one table exists to stop:
	 * "datagram --pid netrom" would have been refused while
	 * "connect --pid netrom" and "listen pid=netrom" were taken.
	 */
	if (++i >= argc) { say(cp, "*** --pid needs a value"); return -1; }
	if ((cp->dgram_pid = pid_number(argv[i])) < 0) {
	  say(cp, "*** invalid pid \"%s\"", argv[i]);
	  return -1;
	}
	continue;
      }
      if (!strcmp(argv[i], "--port")) {
	if (++i >= argc) { say(cp, "*** --port needs a value"); return -1; }
	port = argv[i];
      } else if ((p = strchr(argv[i], ':')) && !p[1]) {
	*p = '\0';
	port = argv[i];
      } else
	continue;

      if (!(cp->dgram_iface = if_lookup(port))) {
	say(cp, "*** no interface \"%s\"", port);
	return -1;
      }
      if (cp->dgram_iface->output != ax_output) {
	say(cp, "*** interface \"%s\" does not carry AX.25", port);
	cp->dgram_iface = 0;
	return -1;
      }
    }
  }

  /* Nothing is converted from here on: the line ends a frame, and what is in
   * front of it goes out as it stands.
   */
  cp->binary = 1;
  cp->dgram = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* One line, one frame.  Deliberately without any knowledge of APRS: no
 * opinion about WIDEn-N, no rewriting of the has-been-repeated marks.  We are
 * the sender, not a digipeater, and the sysop knows what was configured.
 */

static void datagram_line(struct controlblock *cp, char *line)
{

  char *cp1;
  char *digi;
  char *payload;
  int first;
  int i;
  struct ax25 hdr;
  struct iface *ifp;
  struct mbuf *bp;
  const uint8 *idest;
  uint8 (*mpp)[AXALEN];
  struct ax_route *rp;

  if (cp->dgram_fixed) {
    /* The header was given once; this line is payload and nothing else, not
     * even an empty one - a frame carrying only a pid would be noise.
     */
    if (!*line) return;
    hdr = cp->dgram_hdr;
    payload = line;
    goto send;
  }

  while (isspace(*line & 0xff)) line++;
  if (!*line) return;                   /* an empty line is not a frame */

  /* The first colon, not the last: an APRS message begins with one of its
   * own, as in "DL9SAU>APRS::DL1ABC   :moin".
   */
  if (!(payload = strchr(line, ':'))) {
    say(cp, "*** no colon in \"%s\"", line);
    return;
  }
  *payload++ = '\0';

  /* Nothing behind the colon is nothing to send.  Not an error - a frame with
   * only a pid in it would be noise on the channel - so it goes the way an
   * empty line goes, without a word.
   */
  if (!*payload) return;

  if (!(cp1 = strchr(line, '>'))) {
    say(cp, "*** no \">\" in the header");
    return;
  }
  *cp1++ = '\0';

  memset(&hdr, 0, sizeof(hdr));
  if (setcall(hdr.source, line)) {
    say(cp, "*** invalid call \"%s\"", line);
    return;
  }
  /* What follows the ">" is the destination, and after the first comma the
   * path.  A trailing "*" says that element has already repeated it.
   */
  first = 1;
  for (digi = strtok(cp1, ","); digi; digi = strtok(NULL, ",")) {
    int repeated = 0;
    char *star = strchr(digi, '*');

    if (star) { *star = '\0'; repeated = 1; }

    if (first) {
      first = 0;
      if (setcall(hdr.dest, digi)) {
	say(cp, "*** invalid call \"%s\"", digi);
	return;
      }
      continue;
    }
    if (hdr.ndigis >= MAXDIGIS) {
      say(cp, "*** too many digipeaters");
      return;
    }
    if (setcall(hdr.digis[hdr.ndigis], digi)) {
      say(cp, "*** invalid call \"%s\"", digi);
      return;
    }
    hdr.ndigis++;
    if (repeated) hdr.nextdigi = hdr.ndigis;
  }
  hdr.cmdrsp = LAPB_COMMAND;

send:
  if (cp->dgram_iface) {
    bp = qdata(payload, (uint) (cp->dgram_paylen ? cp->dgram_paylen
					       : (int) strlen(payload)));
    ax_send_ui(cp->dgram_iface, &hdr, cp->dgram_pid, &bp);
    return;
  }
  /* No port was named and the frame has to find its own way.  A broadcast
   * goes to every AX.25 port - that is what a broadcast is for.  Anything
   * else goes out where the autorouter puts the next hop, and is dropped
   * without a word when nobody knows it: one station's unicast shouted back
   * on every port would be noise on the air.
   */
  idest = (hdr.ndigis && hdr.nextdigi != hdr.ndigis) ?
	  hdr.digis[hdr.nextdigi] : hdr.dest;
  for (mpp = Ax25multi; (*mpp)[0]; mpp++)
    if (addreq(idest, *mpp)) {
      for (i = 0, ifp = Ifaces; ifp; ifp = ifp->next) {
	struct ax25 copy = hdr;

	if (ifp->output != ax_output) continue;
	bp = qdata(payload, (uint) (cp->dgram_paylen ? cp->dgram_paylen
						   : (int) strlen(payload)));
	ax_send_ui(ifp, &copy, cp->dgram_pid, &bp);
	i++;
      }
      if (!i) say(cp, "*** no AX.25 port to send on");
      return;
    }

  rp = ax_routeptr(idest, 0);
  ifp = (rp && rp->ifp) ? rp->ifp : Axroute_default_ifp;
  if (!ifp || ifp->output != ax_output)
    return;                 /* unbekannter Weg: verwerfen, nicht fluten */
  bp = qdata(payload, (uint) (cp->dgram_paylen ? cp->dgram_paylen
					     : (int) strlen(payload)));
  ax_send_ui(ifp, &hdr, cp->dgram_pid, &bp);
}

/*---------------------------------------------------------------------------*/

static int console_command(struct controlblock *cp)
{
  char buf[1024];

  if (fkbd >= 0 || (isatty(0) && isatty(1))) {
    sprintf(buf, "*** %s busy\n", Hostname);
    write(cp->fd, buf, strlen(buf));
    return -1;
  }
  fflush(stdin);
  fflush(stdout);
  fflush(stderr);
  dup2(cp->fd, 0);
  dup2(cp->fd, 1);
  dup2(cp->fd, 2);
  fkbd = 0;
  on_read(fkbd, keyboard, 0);
  printf(Prompt, Hostname);
  return -1;
}

/*---------------------------------------------------------------------------*/

static void command_receive(void *arg)
{
  struct controlblock *cp = (struct controlblock *) arg;

  /* Everything the command channel offers.  "command" runs the node's own
   * command table and "console" takes over its console - which is why this
   * belongs to root alone.
   */
  static const struct cmdtable full_table[] = {
    { "ascii",   ascii_command },
    { "binary",  binary_command },
    { "command", command_command },
    { "connect", connect_command },
    { "console", console_command },
    { "datagram", datagram_command },
    { "handover", handover_command },
    { "listen",  listen_command },
    { 0,         0 }
  };

  /* What the service socket offers: reaching the outside, and nothing that
   * reaches back into the node.
   */
  static const struct cmdtable service_table[] = {
    { "ascii",   ascii_command },
    { "binary",  binary_command },
    { "connect", connect_command },
    { "datagram", datagram_command },
    { "handover", handover_command },
    { "listen",  listen_command },
    { "sessions", sessions_command },
    /* "close" last on purpose: names are matched by their first letters, and
     * a bare "c" has always meant "connect".  "cl" reaches "close".
     */
    { "close",   close_command },
    { 0,         0 }
  };

  /* What the monitor socket offers, and all it offers.  It is not a place to
   * connect, send or command from: the one word opens the stream and the
   * session is a sink afterwards.
   */
  static const struct cmdtable monitor_table[] = {
    { "monitor", monitor_command },
    { 0,         0 }
  };

  char c;
  int n;

  /* The ordinary way a client goes: it closed.  Kept apart from a read
   * error, because only one of the two is somebody else's doing.
   */
  if ((n = read(cp->fd, &c, 1)) <= 0) {
    delete_controlblock(cp, 0, n == 0 ? "end of file - the client closed"
				   : strerror(errno));
    return;
  }
  /* A monitor receives; it does not command.  Read here only to hear the end
   * of the stream - a client that vanishes must free its slot - and throw
   * away whatever else it sent.  Nothing it could say would be an order, and
   * parsing it as one would end the session over a stray byte.
   */
  if (cp->monitor) return;
  /* Take a line ending however it comes: LF, CRLF or a bare CR.  A telnet
   * sends CRLF, and a stray CR left on the end of the last word turned
   * "datagram hf1:" into an unknown word - it only ever went unnoticed
   * because setcall() happens to tolerate one.
   */
  /* A counted frame: the client announced "[n]" and a header, and what
   * follows is n bytes of payload and nothing else.  No line ending closes
   * it - CR and NL are ordinary content here, which is the whole point.
   */
  if (cp->dgram_need > 0) {
    cp->buffer[cp->bufcnt++] = c;
    if (--cp->dgram_need == 0) {
      cp->dgram_paylen = cp->bufcnt - cp->dgram_hdrlen;
      cp->buffer[cp->bufcnt] = 0;
      datagram_line(cp, cp->buffer);
      cp->dgram_paylen = 0;
      cp->bufcnt = 0;
    } else if (cp->bufcnt >= (int) sizeof(cp->buffer) - 1)
      delete_controlblock(cp, 0, "counted frame longer than the buffer");
    return;
  }

  /* Does this line announce one?  "[n]" at the very front, so the first byte
   * decides and no payload can be read as a length.  The header ends at the
   * colon and the bytes start straight after it.
   */
  if (cp->dgram && cp->bufcnt > 0 && cp->buffer[0] == '[' && c == ':') {
    char *end;
    long n;

    cp->buffer[cp->bufcnt] = 0;
    n = strtol(cp->buffer + 1, &end, 10);
    if (*end == ']' && n > 0 && n < (long) sizeof(cp->buffer) - cp->bufcnt) {
      /* Drop the "[n]" and keep the header, so datagram_line() sees exactly
       * what it always sees.
       */
      int hl = cp->bufcnt - (int) (end + 1 - cp->buffer);

      memmove(cp->buffer, end + 1, (size_t) hl);
      cp->buffer[hl] = ':';
      cp->bufcnt = hl + 1;
      cp->dgram_hdrlen = cp->bufcnt;
      cp->dgram_need = (int) n;
      return;
    }
  }

  if (c != '\r' && c != '\n') {
    cp->lastcr = 0;
    cp->buffer[cp->bufcnt++] = c;
    if (cp->bufcnt >= sizeof(cp->buffer))
      delete_controlblock(cp, 0, "line longer than the buffer");
    return;
  }
  if (c == '\n' && cp->lastcr) {       /* the LF of a CRLF, already acted on */
    cp->lastcr = 0;
    return;
  }
  if (c == '\r') {
    cp->lastcr = 1;
    /* Answer in the terminator the client uses.  It says so with its first
     * line, and telnet expects to be answered its own way.
     */
    cp->crlf = 1;
  } else
    cp->lastcr = 0;
  cp->buffer[cp->bufcnt] = 0;
  cp->bufcnt = 0;
  if (cp->dgram) {                      /* no longer commands, frames */
    datagram_line(cp, cp->buffer);
    return;
  }
  if (command_switcher(cp, getarg(cp->buffer, 0),
		       cp->restricted == 2 ? monitor_table
		       : cp->restricted ? service_table : full_table))
    delete_controlblock(cp, 0,
			"unknown command, or the command ended the session");
}

/*---------------------------------------------------------------------------*/

static void accept_connection_net(void *p)
{

  socklen_t addrlen;
  int fd;
  struct controlblock *cp;
  /* sockaddr_storage, not sockaddr: the latter is 16 bytes, while
   * sockaddr_un needs over 100 and sockaddr_in6 needs 28.  accept() would
   * write what it can and report the full length in addrlen.
   */
  struct sockaddr_storage addr;

  struct listener *l = (struct listener *) p;

  addrlen = sizeof(addr);
  /* Which socket woke us, and what may be said on it: on_read() carries the
   * entry, so this works with any number of listeners.
   */
  if ((fd = accept(l->fd, (struct sockaddr *) &addr, &addrlen)) < 0)
    return;
  cp = (struct controlblock *) calloc(1, sizeof(struct controlblock));
  if (!cp) {
    close(fd);
    return;
  }
  cp->fd = fd;
  cp->restricted = l->restricted;
  /* The monitor socket's writes must never wait: a node held up in write()
   * would miss frames on the air, and the monitor is only watching.  The
   * reads stay as they are - command_receive() reads only when select says
   * there is something, and a non-blocking read that returns EAGAIN there
   * would be answered by the same nul check as the end of the stream.  The
   * one connect() on accept() does not inherit O_NONBLOCK from the listener,
   * so it is set here and only for the monitor.
   */
  if (l->restricted == 2) {
    int fl = fcntl(cp->fd, F_GETFL, 0);

    if (fl != -1) fcntl(cp->fd, F_SETFL, fl | O_NONBLOCK);
  }
  on_read(cp->fd, command_receive, cp);
}

/*---------------------------------------------------------------------------*/

int dobye(int argc, char *argv[], void *p)
{
struct iface *ifp;

  if (fkbd >= 0) {
    freopen("/dev/null", "r+", stdin);
    freopen("/dev/null", "r+", stdout);
    freopen("/dev/null", "r+", stderr);
    off_read(fkbd);
    fkbd = -1;
    for (ifp = Ifaces; ifp; ifp = ifp->next)
      if (ifp->trfp == NULL || ifp->trfp == stdout)
	ifp->trace = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Say it where it can be heard.  Without a terminal, ioinit() closes the
 * standard descriptors and reopens them on /dev/null (hpux.c), so a printf
 * at startup reaches only a sysop sitting at the console - and a node started
 * at boot has none, which is precisely when these messages matter.
 */

static void complain(const char *fmt, ...)
{
  char buf[512];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  printf("%s\n", buf);
  syslog(LOG_ERR, "%s", buf);
}

/*---------------------------------------------------------------------------*/

/* The service socket is created wide open, and that is not carelessness: what
 * decides who may reach it is the socket's own mode, and 0666 is the widest a
 * sysop gets.  sockets/ itself is 0755 (see create_rundir() in lib/rundir.c):
 * it decides who may walk the path and who may create the socket, and both are
 * the sysop's business, but it no longer decides who may reach a socket that
 * is already there.  That moved onto the socket, where "axsock mode"/"axsock
 * group" and their monitor twins set it, after the bind.
 *
 * Guessing a group here is what did not work.  A group looked up by name gave
 * whoever happened to hold that name the transmitter, and on BSD a socket
 * inherits the group of its DIRECTORY, so "no such group" quietly produced
 * group staff - every local account.  Refusing instead, with 0600, produced a
 * node that no client could reach, which drives an operator to chmod -R 777
 * on the lot.  Both are worse than saying plainly what each socket's mode is
 * and leaving the choice to net.rc.
 *
 * 0666 AND NOT 0777, because connect() to a unix socket asks for WRITE and
 * never for execute.  Linux says it in af_unix.c, unix_find_bsd():
 * path_permission(&path, MAY_WRITE).  Measured on macOS, a fresh socket per
 * mode, connect() and accept():
 *
 *     0666, 0600, 0200   OK        w set, x clear
 *     0111               EACCES    x set, w clear
 *     0466, 0000         EACCES
 *
 * Nothing ever consulted the x bit; it only made the mode look more alarming
 * than the decision behind it.  For a socket, "wide open" is 0666.  It was
 * 0777 because that was the plainest way to write "the directory decides",
 * without anybody checking which bits get read (Thomas' question).
 *
 * The monitor socket is not the service socket, and 0666 is not its answer.
 * What the service socket decides is CONNECT, and connect() asks for write;
 * what the monitor decides is WATCH, and watching is being handed every frame
 * on the node.  That audience is the group, so the monitor comes up 0660, its
 * group inherited from sockets/ (inherit_dir_group() below), and "axsock
 * monitor mode"/"group" may say otherwise exactly as "axsock mode"/"group" do
 * for the service socket.
 */

static void set_service_rights(const char *path)
{
  chmod(path, 0666);
}

/* The monitor's group is the group of the directory that holds it - sockets/ -
 * because 0660 says so and because that is the one decision the sysop already
 * makes there.  BSD gives a new socket the group of its directory by itself;
 * Linux gives it the group of the process, so it has to be said.  Saying it
 * makes "chgrp hams $TCPDIR/sockets" mean the same on both platforms, and
 * "axsock monitor group" may still override it from net.rc.
 *
 * A failure is left alone: the mode still holds, and the group stays what
 * bind() gave it, which is what an installation that never set one expects.
 */
static void inherit_dir_group(const char *path)
{
  char dir[sizeof(((struct sockaddr_un *) 0)->sun_path) + 1];
  struct stat st;
  char *slash;

  if (strlen(path) >= sizeof(dir)) return;
  strcpy(dir, path);
  if (!(slash = strrchr(dir, '/'))) return;
  *slash = '\0';
  if (stat(dir, &st)) return;
  chown(path, (uid_t) -1, st.st_gid);
}

/*---------------------------------------------------------------------------*/

/* "axsock" - the sysop's say over the service socket.
 *
 * The defaults above are a starting point, not a policy.  0660 group hams is
 * the usual answer, but it is not the only sensible one: 0660 group staff
 * keeps the hams group from transmitting, and 0606 group hams lets everybody
 * in EXCEPT the logged-in amateurs, which is somebody's considered decision
 * about who may open outgoing links.  Forcing 0660 on every start made all of
 * that impossible.
 *
 * It has to be a command rather than a mode left on the file, because a
 * socket does not survive: bind() creates it afresh every start.  So this
 * belongs in net.rc, where it runs after the socket exists.
 */

int doaxsock(int argc, char *argv[], void *p)
{
  struct group *gr;
  struct stat st;
  unsigned long mode;
  char *end;

  (void) p;

  listener_paths();
  if (argc < 2) {
    if (stat(Axsock_path, &st)) {
      printf("%s: not there\n", Axsock_path);
      return 1;
    }
    printf("%s  mode 0%03o  uid %lu  gid %lu\n", Axsock_path,
           (unsigned) (st.st_mode & 07777),
           (unsigned long) st.st_uid, (unsigned long) st.st_gid);
    if (stat(Monitor_path, &st))
      printf("%s: not there\n", Monitor_path);
    else
      printf("%s  mode 0%03o  uid %lu  gid %lu\n", Monitor_path,
	     (unsigned) (st.st_mode & 07777),
	     (unsigned long) st.st_uid, (unsigned long) st.st_gid);
    if (Tcpax25gw_port)
      printf("tcp-listen on port %d (127.0.0.1 and ::1)\n", Tcpax25gw_port);
    else
      printf("tcp-listen off (default port %d)\n",
	     TCP_AX25_GW_PORT_DEFAULT);
    return 0;
  }

  if (!strcmp(argv[1], "group")) {
    if (argc < 3) {
      printf("axsock group <name>\n");
      return 1;
    }
    if (!(gr = getgrnam(argv[2]))) {
      printf("no group \"%s\"\n", argv[2]);
      return 1;
    }
    if (chown(Axsock_path, (uid_t) -1, gr->gr_gid)) {
      printf("%s: cannot set the group\n", Axsock_path);
      return 1;
    }
    return 0;
  }

  if (!strcmp(argv[1], "mode")) {
    if (argc < 3) {
      printf("axsock mode <octal>\n");
      return 1;
    }
    mode = strtoul(argv[2], &end, 8);
    if (*end || mode > 07777) {
      printf("axsock mode <octal>, e.g. 0660\n");
      return 1;
    }
    if (chmod(Axsock_path, (mode_t) mode)) {
      printf("%s: cannot set the mode\n", Axsock_path);
      return 1;
    }
    return 0;
  }

  /* The monitor socket's rights, on their own because it is a socket of its
   * own: 0660 and the group of sockets/ is a different answer from 0666, and
   * the sysop may want to change either - see the note on sockets/ above.
   */
  if (!strcmp(argv[1], "monitor")) {
    if (argc < 3) {
      printf("axsock monitor mode <octal>|group <name>\n");
      return 1;
    }
    if (!strcmp(argv[2], "mode")) {
      if (argc < 4) {
	printf("axsock monitor mode <octal>\n");
	return 1;
      }
      mode = strtoul(argv[3], &end, 8);
      if (*end || mode > 07777) {
	printf("axsock monitor mode <octal>, e.g. 0660\n");
	return 1;
      }
      if (chmod(Monitor_path, (mode_t) mode)) {
	printf("%s: cannot set the mode\n", Monitor_path);
	return 1;
      }
      return 0;
    }
    if (!strcmp(argv[2], "group")) {
      if (argc < 4) {
	printf("axsock monitor group <name>\n");
	return 1;
      }
      if (!(gr = getgrnam(argv[3]))) {
	printf("no group \"%s\"\n", argv[3]);
	return 1;
      }
      if (chown(Monitor_path, (uid_t) -1, gr->gr_gid)) {
	printf("%s: cannot set the group\n", Monitor_path);
	return 1;
      }
      return 0;
    }
    printf("axsock monitor mode <octal>|group <name>\n");
    return 1;
  }

  /* The Unix sockets are always there - they come up with the node and go
   * down with it.  Only the TCP door is a decision, so it is a setting on
   * axsock and not a start of the axtcp carrier: nothing gets started here,
   * a listener that is already running just gains a second way in.
   */
  if (!strcmp(argv[1], "tcp-listen")) {
    int port;

    if (argc < 3) {
      if (Tcpax25gw_port)
	printf("tcp-listen on port %d (127.0.0.1 and ::1)\n", Tcpax25gw_port);
      else
	printf("tcp-listen off (default port %d)\n",
	       TCP_AX25_GW_PORT_DEFAULT);
      return 0;
    }
    if (!strcmp(argv[2], "off")) {
      tcpax25gw_off();
      return 0;
    }
    if (!strcmp(argv[2], "on"))
      port = TCP_AX25_GW_PORT_DEFAULT;
    else {
      /* "port 8011" and a bare "8011" both, so that the word from the
       * listing reads back as a command.
       */
      char *arg = strcmp(argv[2], "port") ? argv[2] : (argc > 3 ? argv[3] : "");

      port = (int) strtol(arg, &end, 10);
      if (!*arg || *end || port <= 0 || port > 65535) {
	printf("axsock tcp-listen <on|off|port <n>>  (default %d)\n",
	       TCP_AX25_GW_PORT_DEFAULT);
	return 1;
      }
    }
    if (Tcpax25gw_port == port) return 0;   /* already where it is wanted */
    tcpax25gw_off();                        /* a new port replaces the old one */
    return tcpax25gw_on(port);
  }

  printf("axsock [group <name>|mode <octal>|tcp-listen <on|off|port <n>>|"
	 "monitor mode <octal>|group <name>]\n"
	 "       (tcp-listen default port %d)\n", TCP_AX25_GW_PORT_DEFAULT);
  return 1;
}

/*---------------------------------------------------------------------------*/

/* loud: say so when this one cannot be opened.  The loopback pair is opened
 * quietly, because a machine without IPv6 is not a misconfiguration and
 * neither is one without IPv4 - only having neither is worth a word.
 */

static int open_listener(struct listener *l, int loud)
{

  int addrlen;
  int arg;
  int fd;
  struct sockaddr *addr;

  if (l->fd >= 0) return 0;             /* already listening */

  if (!(addr = build_sockaddr(l->name, &addrlen))) {
    if (loud) complain("cannot use %s as a listening address", l->name);
    return -1;
  }
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) {
    if (loud) complain("cannot make a socket for %s: %s", l->name,
		       strerror(errno));
    return -1;
  }

  switch (addr->sa_family) {
  case AF_INET:
    arg = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
    break;
#if HAS_AF_INET6
  case AF_INET6:
    arg = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
    /* Pin this down rather than inheriting it: whether an IPv6 socket also
     * accepts IPv4 is a system default that differs between Linux and the
     * BSDs.  Fixed to v6-only, 127.0.0.1 and ::1 can be two entries
     * everywhere instead of fighting over the port on some systems.
     */
    arg = 1;
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &arg, sizeof(arg));
    break;
#endif
  }

  if (bind_socket(fd, addr, addrlen) || listen(fd, SOMAXCONN)) {
    if (loud)
      complain("cannot listen on %s: %s", l->name,
	       errno == EADDRINUSE ?
	       "in use - another net is already running" : strerror(errno));
    close(fd);
    return -1;
  }

  if (addr->sa_family == AF_UNIX) {
    const char *sun = ((struct sockaddr_un *) addr)->sun_path;

    if (l->restricted == 2) {
      /* 0660, and the group of the directory that holds it; see the note on
       * sockets/ above.  chown first, so no reader sees the bitmap without
       * the group it belongs to.
       */
      inherit_dir_group(sun);
      chmod(sun, 0660);
    } else if (l->restricted)
      set_service_rights(sun);
    else
      /* The command channel leaned entirely on 0700 of the directory around
       * it - nothing set a mode on the socket itself.  Say it on the object
       * as well, the way the service socket has always stated its terms.
       */
      chmod(sun, 0600);
  }

  l->fd = fd;
  on_read(fd, accept_connection_net, l);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void close_listener(struct listener *l)
{
  if (l->fd < 0) return;
  off_read(l->fd);
  close(l->fd);
  l->fd = -1;
}

/*---------------------------------------------------------------------------*/

/* The loopback listeners behind "axsock tcp-listen".  Off unless asked for:
 * a TCP port has no owner and no group, so switching it on says that every
 * local account may use the transmitter.
 */

static int tcpax25gw_on(int port)
{

  int i;

  sprintf(Tcpax25gw_addr[0], "127.0.0.1:%d", port);
  sprintf(Tcpax25gw_addr[1], "[::1]:%d", port);
  for (i = 0; i < 2; i++) {
    Tcpax25gw[i].name = Tcpax25gw_addr[i];
    open_listener(&Tcpax25gw[i], 0);
  }

  /* One of the two is enough to be useful - a machine without IPv6 is not an
   * error here, and neither is one without IPv4.
   */
  if (Tcpax25gw[0].fd < 0 && Tcpax25gw[1].fd < 0) {
    complain("axsock tcp-listen: neither 127.0.0.1 nor ::1 could be opened "
	     "on port %d", port);
    return 1;
  }
  Tcpax25gw_port = port;
  return 0;
}

/*---------------------------------------------------------------------------*/

static void tcpax25gw_off(void)
{
  int i;

  for (i = 0; i < 2; i++)
    close_listener(&Tcpax25gw[i]);
  Tcpax25gw_port = 0;
}

/*---------------------------------------------------------------------------*/

void remote_net_initialize(void)
{
  struct listener *l;

  openlog("wampes-net", LOG_PID, LOG_DAEMON);

  listener_paths();
  for (l = Listeners; l->name; l++)
    open_listener(l, 1);
}
