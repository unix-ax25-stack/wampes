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
};

struct cmdtable {
  const char *name;                     /* Command name (lower case) */
  int (*fnc)(struct controlblock *cp);  /* Command function */
};

static int fkbd = -1;

/* Where the group that may reach the service socket is looked up.  Without
 * it the socket keeps its owner, which is the safe direction: a sysop can
 * always widen it, and a wrong guess here would hand out the transmitter.
 */
#define AXSOCK_GROUP  "hams"

/* What we listen on.  Two kinds, and the difference is what may be said:
 *
 * The command channel carries "command" and "console" and is therefore the
 * node's own command line - whoever reaches it can reconfigure everything.
 * It lives in .sockets, mode 0700.
 *
 * The service socket carries connect and datagram and nothing that changes
 * the node.  It lives in the public sockets directory, so its own mode has
 * to do the work: 0660, group "hams".
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

/*---------------------------------------------------------------------------*/

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

static void delete_controlblock(struct controlblock *cp)
{
  off_read(cp->fd);
  close(cp->fd);
  free(cp);
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
  default:         return "failed";
  }
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall(struct transport_cb *tp)
{
  struct controlblock *cp = (struct controlblock *) tp->user;

  if (tp->connected) {
    say(cp, "*** connected to %s", cp->target);
    return;
  }
  delete_controlblock(cp);
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

  if (pid == PID_FLEXTALK) cp->binary = 1;
  if (!cp->binary) {
    cp->tp->recv_mode = EOL_LF;
    cp->tp->send_mode = EOL_CR;
  }
  on_read(cp->fd, transport_try_send, cp);
  return 0;
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
    { 0,         0 }
  };

  /* What the service socket offers: reaching the outside, and nothing that
   * reaches back into the node.
   */
  static const struct cmdtable service_table[] = {
    { "ascii",   ascii_command },
    { "binary",  binary_command },
    { "connect", connect_command },
    { 0,         0 }
  };

  char c;

  if (read(cp->fd, &c, 1) <= 0) {
    delete_controlblock(cp);
    return;
  }
  if (c != '\n') {
    cp->buffer[cp->bufcnt++] = c;
    if (cp->bufcnt >= sizeof(cp->buffer)) delete_controlblock(cp);
    return;
  }
  cp->buffer[cp->bufcnt] = 0;
  cp->bufcnt = 0;
  if (command_switcher(cp, getarg(cp->buffer, 0),
		       cp->restricted ? service_table : full_table))
    delete_controlblock(cp);
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

/* The service socket sits in the public directory, so its own mode is what
 * keeps it to the operators.  If the group is missing the socket stays with
 * its owner - narrower than intended rather than wider.
 */

static void set_service_rights(const char *path)
{
  struct group *gr;

  if ((gr = getgrnam(AXSOCK_GROUP)))
    chown(path, (uid_t) -1, gr->gr_gid);
  else
    complain("no group \"%s\": %s stays with its owner", AXSOCK_GROUP, path);
  chmod(path, 0660);
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

  if (addr->sa_family == AF_UNIX && l->restricted)
    set_service_rights(((struct sockaddr_un *) addr)->sun_path);

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

/* "start axtcp [<port>]" - the loopback listeners.  Off unless asked for:
 * a TCP port has no owner and no group, so switching it on says that every
 * local account may use the transmitter.
 */

int axtcpstart(int argc, char *argv[], void *p)
{

  int i;
  int port;

  (void) p;
  port = (argc > 1) ? atoi(argv[1]) : AXTCP_PORT_DEFAULT;
  if (port <= 0 || port > 65535) {
    printf("Invalid port \"%s\"\n", argv[1]);
    return 1;
  }
  if (Axtcp[0].fd >= 0 || Axtcp[1].fd >= 0) {
    printf("axtcp is already running\n");
    return 1;
  }


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
    complain("axtcp: neither 127.0.0.1 nor ::1 could be opened on port %d",
	     port);
    return 1;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int axtcp0(int argc, char *argv[], void *p)
{
  int i;

  (void) argc; (void) argv; (void) p;
  for (i = 0; i < 2; i++)
    close_listener(&Axtcp[i]);
  return 0;
}

/*---------------------------------------------------------------------------*/

void remote_net_initialize(void)
{
  struct listener *l;

  openlog("wampes-net", LOG_PID, LOG_DAEMON);

  for (l = Listeners; l->name; l++)
    open_listener(l, 1);
}
