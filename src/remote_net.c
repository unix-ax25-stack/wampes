/* @(#) $Id: remote_net.c,v 1.35 2006/02/12 17:49:57 dl9sau Exp $ */

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
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
};

struct cmdtable {
  const char *name;                     /* Command name (lower case) */
  int (*fnc)(struct controlblock *cp);  /* Command function */
};

static int fkbd = -1;

/* What we listen on.  Two kinds, and the difference is what may be said:
 *
 * The command channel carries "command" and "console" and is therefore the
 * node's own command line - whoever reaches it can reconfigure everything.
 * It lives in .sockets, mode 0700.
 *
 * The service socket carries connect and datagram and nothing that changes
 * the node.  It lives in the public sockets directory, and there the DIRECTORY
 * is what decides who may reach it - 0750, group of the sysop's choosing.
 *
 * Each entry keeps its own descriptor.  The accept handler is given the
 * entry, not a global - with one shared variable the first connection would
 * hang the node in accept() on the wrong socket.
 */
struct listener {
  const char *name;                     /* in build_sockaddr() notation */
  int restricted;                       /* service socket, not command channel */
  int fd;
};

static struct listener Listeners[] = {
  { "unix:" TCPDIR "/.sockets/netcmd", 0, -1 },
  { "unix:" TCPDIR "/sockets/ax25",    1, -1 },
  { 0,                                 0, -1 }
};

/* The loopback listeners are not among them: they stay closed unless net.rc
 * says so, because a TCP port carries no rights of its own.  Switching it on
 * is the statement that every local account may use the transmitter.
 */
#define AXTCP_PORT_DEFAULT 8010

static struct listener Axtcp[] = {
  { 0, 1, -1 },                         /* 127.0.0.1 */
  { 0, 1, -1 },                         /* ::1       */
  { 0, 0, -1 }
};

static char Axtcp_addr[2][32];
static int Axtcp_port;                  /* 0 while nothing is listening */

/*---------------------------------------------------------------------------*/

static void command_receive(void *arg);
static int axtcp_on(int port);
static void axtcp_off(void);
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
  off_read(cp->fd);
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

      if (!tp->svc || axserv_pipe_attach(tp->svc, cp->binary, &fd)) {
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
 *     ( echo "datagram hf1:"; cat ) < /dev/aprsport | socat - tcp:localhost:8010
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
  for (i = 0, ifp = Ifaces; ifp; ifp = ifp->next) {
    struct ax25 copy = hdr;

    if (ifp->output != ax_output) continue;
    bp = qdata(payload, (uint) (cp->dgram_paylen ? cp->dgram_paylen
					       : (int) strlen(payload)));
    ax_send_ui(ifp, &copy, cp->dgram_pid, &bp);
    i++;
  }
  if (!i) say(cp, "*** no AX.25 port to send on");
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
		       cp->restricted ? service_table : full_table))
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

/* The service socket is created wide open, and that is not carelessness: the
 * directory around it is the gate.  sockets/ is 0750, so who may reach this
 * socket is decided by the group on that directory - one chgrp, and the sysop
 * has moved the whole service from one group to another without having to
 * think about socket modes at all.
 *
 * Guessing here is what does not work.  A group looked up by name gave
 * whoever happened to hold that name the transmitter, and on BSD a socket
 * inherits the group of its DIRECTORY, so "no such group" quietly produced
 * group staff - every local account.  Refusing instead, with 0600, produced a
 * node that no client could reach, which drives an operator to chmod -R 777
 * on the lot.  Both are worse than saying plainly: the door is the directory.
 *
 * net.rc can still say otherwise with "axsock mode" and "axsock group", and
 * those run after the bind.  0707 to admit everyone except one group, for
 * instance.
 */

static void set_service_rights(const char *path)
{
  chmod(path, 0777);
}

/*---------------------------------------------------------------------------*/

/* "axsock" - the sysop's say over the service socket.
 *
 * The defaults above are a starting point, not a policy.  0660 group hams is
 * the usual answer, but it is not the only sensible one: 0660 group staff
 * keeps the hams group from transmitting, and 0707 group hams lets everybody
 * in EXCEPT the logged-in amateurs, which is somebody's considered decision
 * about who may open outgoing links.  Forcing 0660 on every start made all of
 * that impossible.
 *
 * It has to be a command rather than a mode left on the file, because a
 * socket does not survive: bind() creates it afresh every start.  So this
 * belongs in net.rc, where it runs after the socket exists.
 */

static const char Axsock_path[] = TCPDIR "/sockets/ax25";

int doaxsock(int argc, char *argv[], void *p)
{
  struct group *gr;
  struct stat st;
  unsigned long mode;
  char *end;

  (void) p;

  if (argc < 2) {
    if (stat(Axsock_path, &st)) {
      printf("%s: not there\n", Axsock_path);
      return 1;
    }
    printf("%s  mode 0%03o  uid %lu  gid %lu\n", Axsock_path,
           (unsigned) (st.st_mode & 07777),
           (unsigned long) st.st_uid, (unsigned long) st.st_gid);
    if (Axtcp_port)
      printf("tcp-listen on port %d (127.0.0.1 and ::1)\n", Axtcp_port);
    else
      printf("tcp-listen off\n");
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

  /* The Unix sockets are always there - they come up with the node and go
   * down with it.  Only the TCP door is a decision, so it is a setting on
   * axsock and not a "start axtcp": nothing gets started here, a listener
   * that is already running just gains a second way in.
   */
  if (!strcmp(argv[1], "tcp-listen")) {
    int port;

    if (argc < 3) {
      if (Axtcp_port)
	printf("tcp-listen on port %d (127.0.0.1 and ::1)\n", Axtcp_port);
      else
	printf("tcp-listen off\n");
      return 0;
    }
    if (!strcmp(argv[2], "off")) {
      axtcp_off();
      return 0;
    }
    if (!strcmp(argv[2], "on"))
      port = AXTCP_PORT_DEFAULT;
    else {
      /* "port 8011" and a bare "8011" both, so that the word from the
       * listing reads back as a command.
       */
      char *arg = strcmp(argv[2], "port") ? argv[2] : (argc > 3 ? argv[3] : "");

      port = (int) strtol(arg, &end, 10);
      if (!*arg || *end || port <= 0 || port > 65535) {
	printf("axsock tcp-listen <on|off|port <n>>\n");
	return 1;
      }
    }
    if (Axtcp_port == port) return 0;   /* already where it is wanted */
    axtcp_off();                        /* a new port replaces the old one */
    return axtcp_on(port);
  }

  printf("axsock [group <name>|mode <octal>|tcp-listen <on|off|port <n>>]\n");
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
    if (l->restricted)
      set_service_rights(((struct sockaddr_un *) addr)->sun_path);
    else
      /* The command channel leaned entirely on 0700 of the directory around
       * it - nothing set a mode on the socket itself.  Say it on the object
       * as well, the way the service socket has always stated its terms.
       */
      chmod(((struct sockaddr_un *) addr)->sun_path, 0600);
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

static int axtcp_on(int port)
{

  int i;

  sprintf(Axtcp_addr[0], "127.0.0.1:%d", port);
  sprintf(Axtcp_addr[1], "[::1]:%d", port);
  for (i = 0; i < 2; i++) {
    Axtcp[i].name = Axtcp_addr[i];
    open_listener(&Axtcp[i], 0);
  }

  /* One of the two is enough to be useful - a machine without IPv6 is not an
   * error here, and neither is one without IPv4.
   */
  if (Axtcp[0].fd < 0 && Axtcp[1].fd < 0) {
    complain("axsock tcp-listen: neither 127.0.0.1 nor ::1 could be opened "
	     "on port %d", port);
    return 1;
  }
  Axtcp_port = port;
  return 0;
}

/*---------------------------------------------------------------------------*/

static void axtcp_off(void)
{
  int i;

  for (i = 0; i < 2; i++)
    close_listener(&Axtcp[i]);
  Axtcp_port = 0;
}

/*---------------------------------------------------------------------------*/

void remote_net_initialize(void)
{
  struct listener *l;

  openlog("wampes-net", LOG_PID, LOG_DAEMON);

  for (l = Listeners; l->name; l++)
    open_listener(l, 1);
}
