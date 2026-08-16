/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <syslog.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "iface.h"
#include "hpux.h"
#include "buildsaddr.h"
#include "netrom.h"
#include "flexnet.h"
#include "transport.h"
#include "login.h"
#include "remote_net.h"

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

/* What is behind a listened callsign. */

enum axlisten_kind {
  LK_LOGIN,                             /* builtin:login - the node's own */
  LK_SOCKET,                            /* dial an address and pipe */
  LK_PROGRAM,                           /* run a program and pipe */
  LK_CLIENT                             /* hand the descriptor to a client
                                         * that registered on the service
                                         * socket - see axlisten_client_claim */
};

struct axlisten {
  struct axlisten *next;
  int netrom;                           /* NET/ROM L4, which has no callsign
					 * and no service selector of its own */
  uint8 call[AXALEN];
  int pid;
  /* Connections or datagrams, never both - the kernel draws the same line,
   * listen() being for connections while a datagram socket only binds.  Two
   * entries for one callsign and pid are therefore two different listeners.
   */
  int ui;
  struct portlist ports;                /* empty: every port */
  enum axlisten_kind kind;
  char *target;
  /* Three switches.  Their defaults differ by what is being carried, and two
   * of them are not open to argument where the answer cannot be sensible -
   * see the table beside axlisten_add().
   */
  int silent;                           /* no announcement line */
  int wait;                             /* start on the first frame, not on
					 * the connect */
  int binary;                           /* no text conversion */
  int clientfd;                         /* LK_CLIENT: the service socket of
					 * the client holding this entry, -1
					 * when nobody does.  The entry is the
					 * sysop's permission, the claim is
					 * what makes it answer. */
};

static struct axlisten *Axlisten;

/*---------------------------------------------------------------------------*/

static struct axlisten *axlisten_find(const uint8 *call, int pid, int ui)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (!lp->netrom && lp->pid == pid && lp->ui == ui && addreq(lp->call, call))
      return lp;
  return 0;
}

/*---------------------------------------------------------------------------*/

static struct axlisten *axlisten_netrom(void)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (lp->netrom) return lp;
  return 0;
}

/*---------------------------------------------------------------------------*/

int axlisten_active(const uint8 *call)
{
  struct axlisten *lp;

  /* Any protocol id will do here: the question is whether we answer to the
   * callsign at all, not what we would do with the frame.
   */
  for (lp = Axlisten; lp; lp = lp->next)
    if (!lp->netrom && addreq(lp->call, call)) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* listen                                     show what we answer to
 * listen ax25 add [pid=<n>] [<switch>...] <call> <target>
 * listen ax25 drop [pid=<n>] <call>
 * listen netrom add [<switch>...] <target>
 * listen netrom drop
 *
 * <target> is one of
 *      builtin:login           the node's own login, as it always was
 *      tcp:<host>:<port>       dial it and pipe; unix:<path> likewise
 *      /path/to/program args   run it and pipe, circumstances in the
 *                              environment
 *
 * <switch> is --silent/--noisy, --wait/--nowait, --ascii/--binary.  The
 * defaults follow what is being carried:
 *
 *                announcement   wait for first frame   conversion
 *   pid=text     on             off                    ascii
 *   other pid    off, fixed     on                     binary, fixed
 *   netrom L4    on             off                    ascii
 *
 * The two marked fixed are refused rather than ignored: an announcement line
 * inside a binary protocol is rubbish on the wire, and converting a protocol
 * that is not text corrupts it.  Waiting is merely unusual for NET/ROM, not
 * senseless, so that one may be set.
 */

static const char *axlisten_kindname(struct axlisten *lp)
{
  switch (lp->kind) {
  case LK_LOGIN:   return "login";
  case LK_PROGRAM: return "program";
  case LK_CLIENT:  return "client";
  default:         return "socket";
  }
}

/*---------------------------------------------------------------------------*/

static void axlisten_show(struct axlisten *lp)
{
  char buf[AXBUF];
  char ports[64];

  /* Show the "!" back: it lives in the flag, not in the text, and a display
   * that hides it would show a list meaning the opposite of what it says.
   */
  if (!lp->ports.spec)
    strcpy(ports, "(all)");
  else
    snprintf(ports, sizeof(ports), "%s%s",
	     lp->ports.exclude ? "!" : "", lp->ports.spec);

  printf("%-10s %-3s %-6s %-7s %-8s %-10s %s%s%s\n",
	 lp->netrom ? "(netrom)" : pax25(buf, lp->call),
	 lp->netrom ? "-" : (lp->ui ? "UI" : "I"),
	 lp->netrom ? "-" : (lp->pid == PID_NO_L3 ? "text" : "pid"),
	 axlisten_kindname(lp), lp->binary ? "binary" : "ascii",
	 ports,
	 lp->kind == LK_CLIENT
	   ? (lp->clientfd >= 0 ? "client (claimed)" : "client (nobody)")
	   : lp->target,
	 lp->silent ? "  silent" : "", lp->wait ? "  wait" : "");
}

/*---------------------------------------------------------------------------*/

static int axlisten_drop(int netrom, const uint8 *call, int pid, int ui)
{
  struct axlisten *lp, **pp;

  for (pp = &Axlisten; *pp; pp = &(*pp)->next) {
    lp = *pp;
    if (lp->netrom != netrom) continue;
    if (!netrom && (lp->pid != pid || lp->ui != ui || !addreq(lp->call, call)))
      continue;
    *pp = lp->next;
    portlist_free(&lp->ports);
    free(lp->target);
    free(lp);
    return 0;
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

static int axlisten_add(int netrom, int argc, char *argv[])
{

  char *target;
  char buf[512];
  int ascii_set = 0;
  int i;
  int noisy_set = 0;
  int pid = PID_NO_L3;
  long n;
  struct axlisten *lp;
  uint8 call[AXALEN];
  int silent = -1, wait = -1, binary = -1;
  int ui = 0;
  char *ports = 0;
  char perr[160];
  struct portlist newports = { 0, 0 };

  memset(call, 0, sizeof(call));

  for (i = 0; i < argc; i++) {
    char *cp = argv[i];

    if (!strncmp(cp, "pid=", 4)) {
      if (netrom) {
	printf("NET/ROM has no protocol id of its own\n");
	return 1;
      }
      n = strtol(cp + 4, &cp, 0);
      if (*cp || n < 0 || n > 255) {
	printf("Invalid pid \"%s\"\n", argv[i] + 4);
	return 1;
      }
      pid = (int) n;
      continue;
    }
    if (!strcmp(cp, "--silent")) { silent = 1; continue; }
    if (!strcmp(cp, "--noisy"))  { silent = 0; noisy_set = 1; continue; }
    if (!strcmp(cp, "--wait"))   { wait = 1; continue; }
    if (!strcmp(cp, "--nowait")) { wait = 0; continue; }
    /* Connections or datagrams, and which ports.  Written without dashes
     * because they say what the entry IS, not how it behaves.
     */
    if (!strncmp(cp, "port=", 5)) { ports = cp + 5; continue; }
    if (!strcmp(cp, "UI") || !strcmp(cp, "ui")) { ui = 1; continue; }
    if (!strcmp(cp, "I")  || !strcmp(cp, "i"))  { ui = 0; continue; }
    if (!strcmp(cp, "--binary")) { binary = 1; continue; }
    if (!strcmp(cp, "--ascii"))  { binary = 0; ascii_set = 1; continue; }
    if (!strncmp(cp, "--", 2)) {
      printf("Unknown option \"%s\"\n", cp);
      return 1;
    }
    break;                              /* the callsign, or the target */
  }

  if (!netrom) {
    if (i >= argc) {
      printf("No callsign\n");
      return 1;
    }
    if (setcall(call, argv[i])) {
      printf("Invalid call \"%s\"\n", argv[i]);
      return 1;
    }
    if (ismyax25addr(call)) {
      printf("%s is an interface callsign already\n", argv[i]);
      return 1;
    }
    i++;
  }

  if (i >= argc) {
    printf("Nothing to hand it to\n");
    return 1;
  }

  /* A program takes the rest of the line, arguments and all. */
  buf[0] = '\0';
  for (; i < argc; i++) {
    if (buf[0] && strlen(buf) + strlen(argv[i]) + 2 >= sizeof(buf)) break;
    if (buf[0]) strcat(buf, " ");
    strcat(buf, argv[i]);
  }
  target = buf;

  /* The announcement line exists because a socket has no environment.  A
   * program has one, and the same facts twice would only be a line it has to
   * skip - so it is quiet unless somebody asks for it.  The login must not
   * see it at all, or it reads it as a password.
   */
  if (silent < 0 && (*target == '/' || !strcmp(target, "builtin:login")))
    silent = 1;

  /* Now the defaults, which depend on what is being carried. */
  if (!netrom && pid != PID_NO_L3) {
    if (noisy_set) {
      printf("An announcement line would be rubbish inside a binary "
	     "protocol\n");
      return 1;
    }
    if (ascii_set) {
      printf("Converting a protocol that is not text corrupts it\n");
      return 1;
    }
    if (silent < 0) silent = 1;
    if (wait < 0) wait = 1;
    binary = 1;
  } else {
    if (silent < 0) silent = 0;
    if (wait < 0) wait = 0;
    if (binary < 0) binary = 0;
  }

  /* Everything that can be refused is refused before anything is created.
   * The port list used to be set on the entry after it was linked in, so a
   * rejected list left an entry behind that listened on EVERY port - wider
   * than what was asked for, which is the wrong direction to fail in.
   */
  if (portlist_set(&newports, ports, perr, sizeof(perr))) {
    printf("%s\n", perr);
    return 1;
  }

  if (!(lp = netrom ? axlisten_netrom() : axlisten_find(call, pid, ui))) {
    if (!(lp = (struct axlisten *) calloc(1, sizeof(struct axlisten)))) {
      printf("No memory\n");
      return 1;
    }
    lp->netrom = netrom;
    addrcp(lp->call, call);
    lp->pid = pid;
    lp->ui = ui;
    lp->clientfd = -1;
    lp->next = Axlisten;
    Axlisten = lp;
  } else
    free(lp->target);

  if (!strcmp(target, "builtin:login"))
    lp->kind = LK_LOGIN;
  else if (!strcmp(target, "client"))
    lp->kind = LK_CLIENT;
  else if (*target == '/')
    lp->kind = LK_PROGRAM;
  else {
    lp->kind = LK_SOCKET;
    /* "tcp:host:port" is how a sysop writes it; build_sockaddr() wants the
     * host and port alone, and knows "unix:" for itself.
     */
    if (!strncmp(target, "tcp:", 4)) target += 4;
  }
  portlist_free(&lp->ports);
  lp->ports = newports;
  lp->target = strdup(target);
  lp->silent = silent;
  lp->wait = wait;
  lp->binary = binary;
  /* A client is told about the call in the same sendmsg() that carries the
   * descriptor, so the announcement must not also go down the pipe - it
   * would be the first thing the far end read as data.
   */
  if (lp->kind == LK_CLIENT) lp->silent = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* A client on the service socket asks to be given calls to a callsign.  The
 * configured entry is the sysop's permission and this is the claim against
 * it: without an entry there is nothing to claim, which is the whole access
 * rule.  First come, first served - and a second client is told so rather
 * than quietly shadowing the first.
 */

int axlisten_client_claim(const uint8 *call, int pid, int ui, int fd,
			  char *err, int errlen)
{
  char buf[AXBUF];
  struct axlisten *lp;

  if (ismyax25addr(call)) {
    snprintf(err, errlen, "%s belongs to a port", pax25(buf, call));
    return 1;
  }
  if (!(lp = axlisten_find(call, pid, ui)) || lp->kind != LK_CLIENT) {
    snprintf(err, errlen, "%s is not open for clients", pax25(buf, call));
    return 1;
  }
  if (lp->clientfd >= 0 && lp->clientfd != fd) {
    snprintf(err, errlen, "%s is already taken", pax25(buf, call));
    return 1;
  }
  lp->clientfd = fd;
  return 0;
}

/* The client is gone.  Everything it held falls free at once, so a crashed
 * client leaves nothing behind for the sysop to clear.
 */

/* A port list, written "hf1,hf2" for those ports or "!aprs,foo" for every
 * port but those.  Absent means every port.
 *
 * The NAMES are kept, not resolved pointers, and the match happens when a
 * frame arrives.  A list may perfectly well be written before the ports it
 * mentions exist - net.rc is read from the top and nothing says the attach
 * lines come first - and a list resolved at configuration time would quietly
 * miss every port attached after it.
 */

int portlist_set(struct portlist *pl, const char *spec, char *err, int errlen)
{
  const char *p;

  free(pl->spec);
  pl->spec = 0;
  pl->exclude = 0;

  if (!spec || !*spec) return 0;        /* every port */

  if (*spec == '!') {
    pl->exclude = 1;
    spec++;
    if (!*spec) {
      snprintf(err, errlen, "\"!\" without a port");
      return 1;
    }
  }
  /* Only the shape is checked here.  Whether a name is a port is a question
   * for the moment a frame arrives, not for now - see above.
   *
   * A "!" on a single name is allowed once the whole list already excludes:
   * "!foo,!bar" and "!foo,bar" mean the same thing, and the first is what
   * people write.  Spelling it "not (foo,bar)" would be tidier and nobody
   * would type it.
   *
   * Mixing the two is refused.  "hf1,hf2,!foo" reads as though it added an
   * exception, and it does not - it is an inclusion list in which no port is
   * called "!foo", so it quietly means "hf1,hf2".  Right by accident is not
   * right.
   */
  for (p = spec; *p; p++) {
    if (*p == '!' && (p == spec || p[-1] == ',')) {
      if (!pl->exclude) {
        snprintf(err, errlen,
                 "\"%s\" mixes ports and exceptions - put the \"!\" in front "
                 "of the whole list", spec);
        return 1;
      }
      if (p[1] == ',' || !p[1]) {
        snprintf(err, errlen, "\"!\" without a port");
        return 1;
      }
      continue;
    }
    if (*p == ',' && (p == spec || p[1] == ',' || !p[1])) {
      snprintf(err, errlen, "empty port name in \"%s\"", spec);
      return 1;
    }
  }
  if (!(pl->spec = strdup(spec))) {
    snprintf(err, errlen, "no memory");
    return 1;
  }
  return 0;
}

void portlist_free(struct portlist *pl)
{
  free(pl->spec);
  pl->spec = 0;
  pl->exclude = 0;
}

/* Is this port in the list?  An empty list is every port. */

int portlist_allows(const struct portlist *pl, const struct iface *ifp)
{
  const char *p;
  size_t n;
  int found = 0;

  if (!pl->spec) return 1;
  if (!ifp || !ifp->name) return 0;

  n = strlen(ifp->name);
  for (p = pl->spec; *p; ) {
    const char *end = strchr(p, ',');
    size_t len = end ? (size_t) (end - p) : strlen(p);

    /* A redundant "!" on a name inside an exclusion list - see above. */
    if (*p == '!' && len > 1) {
      p++;
      len--;
    }
    if (len == n && !strncmp(p, ifp->name, n)) {
      found = 1;
      break;
    }
    p = end ? end + 1 : p + len;
  }
  return pl->exclude ? !found : found;
}

/*---------------------------------------------------------------------------*/

/* Local services come first.
 *
 * A callsign under which the node is on the air belongs to the node, and a
 * forwarding entry for it has to go - not be shadowed, go, so that what the
 * listen display says is what happens.  The alternative was to let whichever
 * came first in net.rc win, and that cannot be right: nobody can tell from a
 * configuration file whether "listen add ... pid=netrom" above "netrom start"
 * was meant as a forwarding or as an accident.
 *
 * Which protocols are the node's own is decided by the PID table and not by
 * what has been started, so there is no state and no ordering.  That leaves
 * room for the case which is genuinely legitimate: a protocol nobody here
 * serves - PID_FLEXTALK, say - may be forwarded from a port callsign,
 * because taking it away costs the node nothing.
 *
 * A client holding a dropped entry is closed.  On a binary stream that is the
 * only thing we may say, a text line being exactly what must not appear
 * there.
 */

static int pid_is_local(int pid)
{
  struct axlink *ipp;

  if (pid == PID_NO_L3)
    return 1;                           /* connects to the node itself */
  for (ipp = Axlink; ipp->funct; ipp++)
    if (ipp->pid == pid)
      return 1;
  return 0;
}

void axlisten_drop_local(const uint8 *call)
{
  char buf[AXBUF];
  struct axlisten *lp, **pp;

  if (!call || !*call) return;

  for (pp = &Axlisten; *pp; ) {
    lp = *pp;
    if (lp->netrom || !addreq(lp->call, call) || !pid_is_local(lp->pid)) {
      pp = &lp->next;
      continue;
    }
    printf("listen %s pid=0x%02x dropped: %s is a port callsign and the node "
           "serves that protocol itself\n", pax25(buf, lp->call), lp->pid,
           pax25(buf, lp->call));
    if (lp->clientfd >= 0) remote_net_drop_client(lp->clientfd);
    *pp = lp->next;
    portlist_free(&lp->ports);
    free(lp->target);
    free(lp);
  }
}

/*---------------------------------------------------------------------------*/

/* A UI frame has arrived.  Is a datagram client waiting for it?
 *
 * Asked before the node's own protocols get their turn, but only answered
 * for a callsign the sysop opened and a client has actually claimed - an
 * entry nobody holds is nobody waiting, and the frame goes on its usual way.
 *
 * 1 means taken and the buffer is gone.
 */

int axlisten_ui_deliver(struct iface *ifp, struct ax25 *hdr, int pid,
			struct mbuf **bpp)
{
  char buf[AXBUF];
  char line[256];
  int i;
  size_t n;
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next) {
    if (lp->netrom || !lp->ui || lp->pid != pid) continue;
    if (!addreq(lp->call, hdr->dest)) continue;
    if (lp->kind != LK_CLIENT || lp->clientfd < 0) continue;
    if (!portlist_allows(&lp->ports, ifp)) continue;
    break;
  }
  if (!lp) return 0;

  /* TNC2, the same shape the sending direction parses: source, ">", the
   * destination, then the path behind commas, a "*" on each element that has
   * repeated it already.
   */
  n = 0;
  n += (size_t) snprintf(line + n, sizeof(line) - n, "%s>",
			 pax25(buf, hdr->source));
  if (n < sizeof(line))
    n += (size_t) snprintf(line + n, sizeof(line) - n, "%s",
			   pax25(buf, hdr->dest));
  for (i = 0; i < hdr->ndigis && n < sizeof(line); i++)
    n += (size_t) snprintf(line + n, sizeof(line) - n, ",%s%s",
			   pax25(buf, hdr->digis[i]),
			   i < hdr->nextdigi ? "*" : "");
  if (n >= sizeof(line)) return 0;      /* absurd path: leave it alone */

  if (remote_net_send_frame(lp->clientfd, line, *bpp))
    remote_net_drop_client(lp->clientfd);
  free_p(bpp);
  return 1;
}

/*---------------------------------------------------------------------------*/

void axlisten_client_release(int fd)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (lp->kind == LK_CLIENT && lp->clientfd == fd) lp->clientfd = -1;
}

/*---------------------------------------------------------------------------*/

int dolisten(int argc, char *argv[], void *p)
{

  int netrom;
  struct axlisten *lp;
  uint8 call[AXALEN];

  (void) p;
  if (argc < 2) {
    if (!Axlisten) {
      printf("Not listening for anything\n");
      return 0;
    }
    printf("Call       I/UI Pid    Kind    Mode     Ports      Handed to\n");
    for (lp = Axlisten; lp; lp = lp->next)
      axlisten_show(lp);
    return 0;
  }

  if (!strcmp(argv[1], "ax25"))
    netrom = 0;
  else if (!strcmp(argv[1], "netrom"))
    netrom = 1;
  else {
    printf("Usage: listen [ax25|netrom] [add|drop] ...\n");
    return 1;
  }

  if (argc < 3) {
    for (lp = Axlisten; lp; lp = lp->next)
      if (lp->netrom == netrom) axlisten_show(lp);
    return 0;
  }

  if (!strcmp(argv[2], "add"))
    return axlisten_add(netrom, argc - 3, argv + 3);

  if (!strcmp(argv[2], "drop")) {
    int pid = PID_NO_L3;
    int ui = 0;
    int i = 3;

    memset(call, 0, sizeof(call));
    if (!netrom) {
      /* Same words as on the add line: I and UI are two listeners. */
      while (i < argc && (!strcmp(argv[i], "UI") || !strcmp(argv[i], "ui") ||
			  !strcmp(argv[i], "I")  || !strcmp(argv[i], "i"))) {
	ui = (argv[i][0] == 'U' || argv[i][0] == 'u');
	i++;
      }
      if (i < argc && !strncmp(argv[i], "pid=", 4))
	pid = (int) strtol(argv[i++] + 4, NULL, 0);
      if (i >= argc || setcall(call, argv[i])) {
	printf("Which callsign?\n");
	return 1;
      }
    }
    if (axlisten_drop(netrom, call, pid, ui)) {
      printf("Not listening for that\n");
      return 1;
    }
    return 0;
  }

  printf("Usage: listen [ax25|netrom] [add|drop] ...\n");
  return 1;
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
  /* One of these two: an AX.25 consumer, or a NET/ROM circuit.  The rest of
   * the machinery is the same for both, so it is written once and the four
   * operations that differ are wrapped below.
   */
  struct axservice *sp;
  struct circuit *pc;
  int fd;
  struct mbuf *sndq;                    /* waiting for the socket */
  int connecting;                       /* connect() not finished yet */
  int seqpacket;                        /* the socket carries frame
					 * boundaries, so sndq holds whole
					 * frames and each is written on its
					 * own - see axpipe_socketpair() */
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

static struct mbuf *axpipe_recv(struct axpipe *pp, uint cnt)
{
  struct mbuf *bp = 0;

  /* One frame, whole, where the socket can carry the boundary.  A NET/ROM
   * circuit has none to carry - L4 is a byte stream - so it keeps the
   * count.
   */
  if (pp->sp) return pp->seqpacket ? recv_axservice_packet(pp->sp)
				   : recv_axservice(pp->sp, cnt);
  if (pp->pc) recv_nr(pp->pc, &bp, (int) cnt);
  return bp;
}

/* One write is one frame.  Kernel AX.25 was SOCK_SEQPACKET and the protocols
 * that ride on a connection rely on it: FBB's compressed forwarding reads the
 * end of an uncompressed block off the frame boundary, so a stream that packs
 * frames as full as they will go breaks it.  Not every system offers it on a
 * unix socket - macOS does not - so ask, and take a stream when the answer is
 * no.  There the boundaries are lost, which costs that one kind of service
 * and nothing else.
 */

static int axpipe_socketpair(int sv[2])
{
  if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0) return 1;
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) return 0;
  return -1;
}

/* Ask the descriptor itself rather than remembering what made it: a pipe is
 * handed one by a socketpair, by connect() and later by a client, and only
 * one of those knows what it asked for.
 */

static int axpipe_is_seqpacket(int fd)
{
  int type = 0;
  socklen_t len = sizeof(type);

  return getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &len) == 0
    && type == SOCK_SEQPACKET;
}

static int axpipe_space(struct axpipe *pp)
{
  if (pp->sp) return space_axservice(pp->sp);
  if (pp->pc) return space_nr(pp->pc);
  return 0;
}

static void axpipe_put(struct axpipe *pp, struct mbuf **bpp)
{
  if (pp->sp) send_axservice(pp->sp, bpp);
  else if (pp->pc) send_nr(pp->pc, bpp);
  else free_p(bpp);
}

static void axpipe_hangup(struct axpipe *pp)
{
  if (pp->sp) close_axservice(pp->sp);
  else if (pp->pc) close_nr(pp->pc);
}

static int axpipe_alive(struct axpipe *pp)
{
  return pp->sp != 0 || pp->pc != 0;
}

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

static void axpipe_pull(struct axpipe *pp, int cnt)
{
  struct mbuf *bp;
  int room;
  int took = 0;

  if (!axpipe_alive(pp)) return;
  /* The queue holds whole frames where the socket can carry them, so it is
   * counted in bytes across the queue, not along one chain.
   */
  while ((room = AXPIPE_HIGHWATER - (int) len_qbytes(pp->sndq)) > 0) {
    if (!(bp = axpipe_recv(pp, (uint) (cnt < room ? cnt : room)))) break;
    if (pp->ascii) convert_eol(&bp, EOL_LF, &pp->recv_char);
    if (pp->seqpacket)
      enqueue(&pp->sndq, &bp);          /* one entry, one frame */
    else
      append(&pp->sndq, &bp);
    took = 1;
    if (!pp->seqpacket) break;          /* the byte form took all there was */
  }
  if (!took) return;                    /* nothing there - and pumping now
					 * would call us straight back */
  axpipe_pump(pp);
}

static void axpipe_recv_upcall(struct axservice *sp, int cnt)
{
  struct axpipe *pp = (struct axpipe *) sp->user;

  if (pp) axpipe_pull(pp, cnt);
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
  char buf[8192];
  int n;
  int room;

  if (!axpipe_alive(pp)) return;
  room = axpipe_space(pp);
  if (room <= 0) {
    off_read(pp->fd);                   /* the link will call us back */
    return;
  }
  if (pp->seqpacket) {
    /* Whole or not at all.  Reading less than the message throws the rest of
     * it away, which is worse than waiting - so the window does not shorten
     * the read.  Overshooting it costs one frame on the send queue, and
     * send_ax25() cuts that to paclen regardless.
     */
#ifdef	MSG_TRUNC
    n = (int) recv(pp->fd, buf, sizeof(buf), MSG_TRUNC);
#else
    n = (int) read(pp->fd, buf, sizeof(buf));
#endif
    if (n > (int) sizeof(buf)) {
      /* Cannot happen with any sane paclen, and if it ever does it says so
       * rather than quietly delivering half a frame.
       */
      syslog(LOG_ERR, "axpipe: frame of %d bytes cut to %u", n,
	     (unsigned) sizeof(buf));
      n = (int) sizeof(buf);
    }
  } else {
    if (room > (int) sizeof(buf)) room = sizeof(buf);
    n = (int) read(pp->fd, buf, (size_t) room);
  }
  if (n > 0) {
    bp = qdata(buf, (uint) n);
    if (pp->ascii) convert_eol(&bp, EOL_CR, &pp->send_char);
    axpipe_put(pp, &bp);
    return;
  }
  if (n < 0 && (errno == EAGAIN || errno == EINTR)) return;
  axpipe_hangup(pp);                    /* end of file: let the link go */
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
      axpipe_hangup(pp);
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
  char buf[8192];
  int n;
  int want;

  if (pp->connecting) return;
  while (pp->sndq) {
    /* On a boundary-carrying socket the head of the queue is one whole
     * frame and goes out in one write, because that write is what the
     * boundary is made of.  On a stream it is one chain and the buffer
     * decides how much of it goes at a time.
     */
    want = pp->seqpacket ? (int) len_p(pp->sndq) : (int) len_qbytes(pp->sndq);
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
      if (pp->seqpacket) {
	/* A seqpacket write is all or nothing, so the frame is gone. */
	struct mbuf *done = dequeue(&pp->sndq);

	free_p(&done);
      } else
	pullup(&pp->sndq, NULL, (uint) n);
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
      on_write(pp->fd, axpipe_writable, pp);
      return;
    }
    axpipe_hangup(pp);
    return;
  }
  off_write(pp->fd);
  /* Room again: pull whatever the connection has been holding, which is what
   * lets the window reopen.
   */
  if (pp->sp && pp->sp->rxq)
    axpipe_pull(pp, (int) len_qbytes(pp->sp->rxq));
  else if (pp->pc)
    axpipe_pull(pp, AXPIPE_HIGHWATER);
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
  if (pp->pc) pp->pc->user = 0;
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

static struct axpipe *axpipe_new(struct axlisten *lp, int fd)
{

  int addrlen;
  struct axpipe *pp;
  struct sockaddr *addr = 0;

  /* fd >= 0: already connected to something, a program we just started.
   * Otherwise dial the configured address.
   */
  if (fd < 0) {
    if (!(addr = build_sockaddr(lp->target, &addrlen))) return NULL;
    if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) return NULL;
  }
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  if (!(pp = (struct axpipe *) calloc(1, sizeof(struct axpipe)))) {
    close(fd);
    return NULL;
  }
  pp->fd = fd;
  pp->ascii = !lp->binary;
  /* A dialled socket is a stream: the service at the other end chose what it
   * listens on and we do not get to argue.  Frame boundaries survive on the
   * pairs we make ourselves, which is where they are wanted.
   */
  pp->seqpacket = axpipe_is_seqpacket(fd);

  if (addr && connect(fd, addr, addrlen)) {
    if (errno != EINPROGRESS) {
      axpipe_close(pp);
      return NULL;
    }
    pp->connecting = 1;                 /* the node must not wait here */
  }
  return pp;
}

/*---------------------------------------------------------------------------*/

static void axpipe_start(struct axpipe *pp)
{
  if (pp->connecting)
    on_write(pp->fd, axpipe_writable, pp);
  else {
    on_read(pp->fd, axpipe_readable, pp);
    axpipe_pump(pp);
  }
}

/*---------------------------------------------------------------------------*/

static struct axservice *axpipe_open(struct ax25_cb *axp, struct axlisten *lp,
				     int fd)
{
  struct axpipe *pp;
  struct axservice *sp;

  if (!(pp = axpipe_new(lp, fd))) return NULL;
  if (!(sp = open_axservice(axp, lp->pid, axpipe_recv_upcall,
			    axpipe_send_upcall, axpipe_state_upcall, pp))) {
    axpipe_close(pp);
    return NULL;
  }
  pp->sp = sp;
  if (!lp->silent) axpipe_announce(pp, axp);
  axpipe_start(pp);
  return sp;
}


static int axspawn_fd(struct axlisten *lp, const char *user, const char *proto,
		      const char *dest, const char *path)
{

  char *argv[32];
  char buf[512];
  char env[8][200];
  char *envp[9];
  char *p;
  int argc;
  int envc = 0;
  int i;
  int sv[2];
  pid_t child;

  if (axpipe_socketpair(sv) < 0) return -1;

  strcpy(buf, lp->target);
  for (argc = 0, p = strtok(buf, " \t");
       p && argc < (int) (sizeof(argv) / sizeof(argv[0])) - 1;
       p = strtok(NULL, " \t"))
    argv[argc++] = p;
  argv[argc] = 0;

  /* The circumstances go in the environment, where a program can read them
   * without parsing anything.
   *
   * The first three names are axspawn's and mean exactly what they mean
   * there, so a script written for one world runs in the other: AXCALL is the
   * calling station with its SSID, CALL the same lower case and without it -
   * the shape a unix account name takes - and PROTOCOL is the address family.
   * Ours carry the protocol in the name, which is what tells AX25_DEST and
   * NETROM_NODE apart when both could be meant.  Not PATH for the
   * digipeaters, however tempting: that is the shell's, and AX25_PATH says
   * whose path it is.
   */
  {
    int netrom = !strcmp(proto, "netrom");
    char lower[80];
    char *dash;

    strncpy(lower, user, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    for (p = lower; *p; p++) *p = Xtolower(*p & 0xff);
    if ((dash = strchr(lower, '-')) != NULL) *dash = '\0';

    sprintf(env[envc++], "AXCALL=%.60s", user);
    sprintf(env[envc++], "CALL=%.60s", lower);
    sprintf(env[envc++], "PROTOCOL=%s", netrom ? "NET/ROM" : "AX.25");
    if (netrom)
      sprintf(env[envc++], "NETROM_NODE=%.60s", dest);
    else {
      sprintf(env[envc++], "AX25_DEST=%.60s", dest);
      sprintf(env[envc++], "AX25_PID=%.20s", proto);
      sprintf(env[envc++], "AX25_PATH=%.150s", path ? path : "");
    }
    /* A path of our own, not the one the node was started with.  A service
     * run for a caller on the air has no business inheriting whatever the
     * sysop happened to have in his shell - which is what execv() gave it
     * until now.
     */
    sprintf(env[envc++], "PATH=/usr/local/bin:/usr/bin:/bin");
    for (i = 0; i < envc; i++) envp[i] = env[i];
    envp[envc] = 0;
  }

  if ((child = fork()) < 0) {
    close(sv[0]);
    close(sv[1]);
    return -1;
  }
  if (child == 0) {
    int fd;

    dup2(sv[1], 0);
    dup2(sv[1], 1);
    dup2(sv[1], 2);
    for (fd = 3; fd < FD_SETSIZE; fd++) close(fd);
    execve(argv[0], argv, envp);
    _exit(1);
  }
  close(sv[1]);
  return sv[0];
}

/*---------------------------------------------------------------------------*/

/* The same thing for a NET/ROM circuit.  There is no callsign to listen for
 * and no protocol id - a NET/ROM connect reaches the node, not a service on
 * it - so there is one entry and it takes every incoming L4 session.
 */

static void nrpipe_recv_upcall(struct circuit *pc, int cnt)
{
  struct axpipe *pp = (struct axpipe *) pc->user;

  if (pp) axpipe_pull(pp, cnt);
}

static void nrpipe_send_upcall(struct circuit *pc, int cnt)
{
  struct axpipe *pp = (struct axpipe *) pc->user;

  (void) cnt;
  if (pp && !pp->connecting) on_read(pp->fd, axpipe_readable, pp);
}

static void nrpipe_close_upcall(struct circuit *pc)
{
  struct axpipe *pp = (struct axpipe *) pc->user;

  if (pp) {
    pc->user = 0;
    pp->pc = 0;
    axpipe_close(pp);
  }
}

/*---------------------------------------------------------------------------*/

int nrserv_listen_start(struct circuit *pc)
{

  char buf[160];
  int fd = -1;
  struct axlisten *lp;
  struct axpipe *pp;

  if (!(lp = axlisten_netrom())) return 0;      /* nothing configured */
  if (lp->kind == LK_LOGIN) return 0;           /* the node's own login */

  if (lp->kind == LK_PROGRAM) {
    char user[AXBUF], node[AXBUF];

    pax25(user, pc->cuser);
    pax25(node, pc->node);
    if ((fd = axspawn_fd(lp, user, "netrom", node, 0)) < 0) return 0;
  }

  if (!(pp = axpipe_new(lp, fd))) return 0;
  pp->pc = pc;
  pc->user = (char *) pp;
  pc->r_upcall = nrpipe_recv_upcall;
  pc->t_upcall = nrpipe_send_upcall;

  if (!lp->silent) {
    struct mbuf *bp;

    sprintf(buf, "*** incoming netrom %s\n", nr_addr2str(pc));
    bp = qdata(buf, (uint) strlen(buf));
    append(&pp->sndq, &bp);
  }
  axpipe_start(pp);
  return 1;
}

/*---------------------------------------------------------------------------*/

void nrserv_listen_close(struct circuit *pc)
{
  nrpipe_close_upcall(pc);
}

/*---------------------------------------------------------------------------*/


static struct axservice *axserv_login_open(struct ax25_cb *axp)
{
  char callsign[AXBUF];
  struct axservice *sp;

  if (!Axserver_enabled) return NULL;
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

/* Hand a link we made ourselves to whoever asked for it, the same way an
 * incoming call is handed to a client: a socketpair, one end on the AX.25
 * consumer and the other to the caller.
 *
 * The point is the boundary.  A connect on the service socket turns that
 * connection into the pipe, and it is a stream - frames arrive packed as full
 * as they will go, which breaks a protocol that reads the end of a block off
 * the end of a frame.  A pair we make ourselves can carry them, because both
 * ends are ours to choose.
 *
 * Takes the consumer the caller already opened rather than opening one: the
 * link exists, the transport made it, and what changes here is only who is
 * fed by it.
 */

int axserv_pipe_attach(struct axservice *sp, int binary, int *fdp)
{
  static struct axlisten pipe_lp;       /* only binary is read for a live fd */
  struct axpipe *pp;
  int sv[2];

  if (axpipe_socketpair(sv) < 0) return -1;
  pipe_lp.binary = binary;
  pipe_lp.silent = 1;
  if (!(pp = axpipe_new(&pipe_lp, sv[0]))) {
    close(sv[0]);
    close(sv[1]);
    return -1;
  }
  pp->sp = sp;
  sp->r_upcall = axpipe_recv_upcall;
  sp->t_upcall = axpipe_send_upcall;
  sp->s_upcall = axpipe_state_upcall;
  sp->user = pp;
  axpipe_start(pp);
  *fdp = sv[1];
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Hand one accepted call to a client: the trace line and the descriptor in a
 * single sendmsg().  Together, deliberately - a descriptor arriving on its
 * own would have to be matched against a line arriving separately, and there
 * is no key to match them with.  This way the client's accept() is one
 * recvmsg() and there is nothing to correlate.
 *
 * The socketpair is ours at both ends, so it can carry frame boundaries
 * where the system has them; see axpipe_socketpair().
 */

static int axserv_handover(struct axlisten *lp, struct ax25_cb *axp,
			   int *fdp)
{
  char buf[256];
  char call[AXBUF];
  int i;
  int sv[2];
  struct cmsghdr *cm;
  struct iovec iov;
  struct msghdr msg;
  union {                               /* aligned as a cmsghdr wants */
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } control;

  sprintf(buf, "%s %s", axp->iface ? axp->iface->name : "?",
	  pax25(call, axp->hdr.dest));
  for (i = 0; i < axp->hdr.ndigis; i++) {
    strcat(buf, ",");
    strcat(buf, pax25(call, axp->hdr.digis[i]));
  }
  strcat(buf, " > ");
  strcat(buf, pax25(call, axp->hdr.source));
  strcat(buf, "\n");

  if (axpipe_socketpair(sv) < 0) return -1;

  memset(&msg, 0, sizeof(msg));
  memset(&control, 0, sizeof(control));
  iov.iov_base = buf;
  iov.iov_len = strlen(buf);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control.buf;
  msg.msg_controllen = sizeof(control.buf);
  cm = CMSG_FIRSTHDR(&msg);
  cm->cmsg_level = SOL_SOCKET;
  cm->cmsg_type = SCM_RIGHTS;
  cm->cmsg_len = CMSG_LEN(sizeof(int));
  memcpy(CMSG_DATA(cm), &sv[1], sizeof(int));

  /* Never wait on the client: the scheduler is cooperative, and a client
   * that is not calling accept() must not be able to stop the node.  A full
   * buffer is refused like any other failure to hand the call on.
   */
  if (sendmsg(lp->clientfd, &msg, MSG_DONTWAIT) < 0) {
    close(sv[0]);
    close(sv[1]);
    return -1;
  }
  close(sv[1]);                         /* the client owns it now */
  *fdp = sv[0];
  return 0;
}

/*---------------------------------------------------------------------------*/

/* What serves this protocol id on this link?  Asked when the first frame for
 * it arrives and nothing is attached yet - and, for entries that do not want
 * to wait, already when the link comes up.
 */

struct axservice *axserv_start(struct ax25_cb *axp, int pid)
{

  char callsign[AXBUF];
  struct axlisten *lp;
  struct axservice *sp = NULL;

  /* On an incoming link build_path() has already turned the header round:
   * hdr.dest is who called us, hdr.source is the address they called.  It is
   * the latter we listen for.
   */
  if (!(lp = axlisten_find(axp->hdr.source, pid, 0))) {
    /* Nothing configured.  Plain text still reaches the node's own login,
     * as it always did.
     */
    return pid == PID_NO_L3 ? axserv_login_open(axp) : NULL;
  }

  switch (lp->kind) {
  case LK_LOGIN:
    return axserv_login_open(axp);
  case LK_SOCKET:
    sp = axpipe_open(axp, lp, -1);
    break;
  case LK_CLIENT:
    {
      int fd;

      if (lp->clientfd < 0) break;      /* nobody there - fall through to
					 * the refusal below, with a reason */
      if (axserv_handover(lp, axp, &fd) == 0) sp = axpipe_open(axp, lp, fd);
    }
    break;
  case LK_PROGRAM:
    {
      char user[AXBUF], dest[AXBUF], path[160];
      int i, fd;

      pax25(user, axp->hdr.dest);
      pax25(dest, axp->hdr.source);
      path[0] = '\0';
      for (i = 0; i < axp->hdr.ndigis; i++) {
	char one[AXBUF];

	if (i) strcat(path, ",");
	strcat(path, pax25(one, axp->hdr.digis[i]));
      }
      if ((fd = axspawn_fd(lp, user, pid == PID_NO_L3 ? "text" : "pid",
			   dest, path)) >= 0)
	sp = axpipe_open(axp, lp, fd);
    }
    break;
  }
  if (sp) return sp;

  /* Nobody there.  The AX.25 side has already had to say UA, so the only way
   * to decline now is to say why and let go.  The caller is told the
   * callsign, not our socket path - where the node keeps its files is
   * nobody's business on the air.
   */
  {
    struct mbuf *bp;
    char buf[120];

    sprintf(buf, "*** %s is not answering\r", pax25(callsign, axp->hdr.source));
    syslog(LOG_ERR, "%s: cannot hand the call to %s: %s",
	   pax25(callsign, axp->hdr.source), lp->target, strerror(errno));
    bp = qdata(buf, (uint) strlen(buf));
    send_ax25(axp, &bp, PID_NO_L3);
  }
  disc_ax25(axp);
  return NULL;
}

/*---------------------------------------------------------------------------*/

/* The link has come up.  Everything configured for this callsign that does
 * not want to wait for a first frame starts now - a mailbox has to be able to
 * greet, and a protocol speaker to announce itself.
 */

void axserv_connected(struct ax25_cb *axp)
{

  struct axlisten *lp;
  int partner;

  /* Unless the caller is a node we route with.  It called to speak NET/ROM
   * or FlexNet, and a greeting would land inside its L3 session - the same
   * hazard that makes "wait" the default for every pid but text, except
   * that here it is the text entry doing the damage.  Asking who called is
   * better than asking the sysop to remember a --wait on every port
   * callsign.  Nothing is taken away: if the neighbour really does send
   * text, that frame still starts the service, it just loses the head
   * start.  A sysop connecting from a linked node therefore still reaches
   * the mailbox, one keystroke later.
   */
  partner = nr_is_neighbour(axp->hdr.dest) || flexnet_is_peer(axp->hdr.dest);

  for (lp = Axlisten; lp; lp = lp->next)
    if (!lp->netrom && !lp->wait && !partner
	&& addreq(lp->call, axp->hdr.source))
      (void) axserv_start(axp, lp->pid);
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
