/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
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
#include "pidfilter.h"
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
  /* Switched off because something local took the callsign, rather than
   * deleted: an entry that vanishes is one the operator cannot see any more,
   * and the reason vanishes with it.
   */
  int disabled;
  char *why;
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
  /* WHO THE PROGRAM RUNS AS.  A node usually runs as root - it binds ports
   * and reads /dev/tun - and without this the program behind a listener
   * inherited that, for no reason anybody chose.  Default "daemon", which is
   * uid 1 and gid 1 on Linux and macOS alike, unlike "nobody" (65534 against
   * 4294967294).  Only for a program: "client" hands the session to something
   * that is already running under its own account, and there is nothing to
   * set.
   */
  uid_t uid;
  gid_t gid;
  char *username;                       /* for initgroups(), if we have one */
  char *home;                           /* pw_dir: where the program runs,
                                         * so it does not inherit whatever
                                         * directory the node was started
                                         * from - "/" when there is none */
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

/* Is a "client:<call>" NET/ROM entry pointing at this callsign?
 *
 * That entry holds the CALLSIGN and never a pointer to the AX.25 entry, which
 * is what makes dropping or rewriting the latter safe: there is nothing to go
 * stale, the callsign is looked up again for every session, and a session
 * already handed over holds only its own descriptor (struct axpipe keeps no
 * entry either).  What the lookup cannot do is warn anybody, so the commands
 * that take the client away ask this and say so.
 */

static struct axlisten *axlisten_netrom_pointing_at(const uint8 *call)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (lp->netrom && lp->kind == LK_CLIENT && addreq(lp->call, call))
      return lp;
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
    if (!lp->netrom && !lp->disabled && addreq(lp->call, call)) return 1;
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
 * <switch> is --silent/--verbose, --wait/--nowait, --ascii/--binary.  The
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

/* The same thing said to the operator.  It was only ever a comment, and a
 * command with this many words is unusable without it.
 */

static int user_id(const char *name, uid_t *uid, gid_t *gid, char **keep,
		   char **home);
static int group_id(const char *name, gid_t *gid);

char Axlisten_usage[] =
"listen                              show what we answer to\n"
"       listen ax25 add [pid=<n>] [I|UI] [iface=<list>] [<switch>...]\n"
"                       <call> <target>\n"
"       listen ax25 drop [I|UI] [pid=<n>] <call>\n"
"       listen netrom add [<switch>...] <target>\n"
"       listen netrom drop\n"
"       listen ax25|netrom enable <n>       switch an entry back on\n"
"\n"
"  Each target says how a line ends where it goes, and that is what an\n"
"  entry converts to unless --ascii or --binary says otherwise:\n"
"\n"
"  <target>   builtin:login          the node's own login, as it always was\n"
"                                    ASCII - the login lives on CR\n"
"             tcp:<host>:<port>      dial it and pipe; unix:<path> likewise\n"
"                                    ASCII - measured: the announcement goes\n"
"                                    out as \"...\\n\", and a CR from the AX.25\n"
"                                    side arrives at the socket as LF\n"
"             /path/to/program args  run it and pipe\n"
"                                    ASCII - an ordinary unix program wants LF\n"
"             client                 give the call to a libax25 program that\n"
"                                    claims this callsign over the service\n"
"                                    socket - ax25d, a mailbox, anything that\n"
"                                    binds and listens.  This line IS the\n"
"                                    permission: a callsign nobody configured\n"
"                                    cannot be claimed.  doc/LIBAX25-BRIDGE.md\n"
"                                    BINARY - a libax25 program speaks the\n"
"                                    packet radio convention itself, and\n"
"                                    converting for it corrupts quietly\n"
"             client:<call>          netrom only: hand the session to whoever\n"
"                                    holds <call>, shown as a call to <call>.\n"
"                                    Same callsign as an ax25 listener meshes\n"
"                                    the two; a different one keeps them\n"
"                                    apart, since the program is told which\n"
"                                    callsign was reached\n"
"  <switch>   --silent/--verbose, --wait/--nowait, --ascii/--binary\n"
"             --user <name|uid>, --group <name|gid> - who a PROGRAM\n"
"             runs as, default daemon (uid 1 and gid 1 on Linux and\n"
"             macOS alike, which \"nobody\" is not).  Without this it\n"
"             inherited whatever the node runs as, usually root.  A\n"
"             name nothing knows falls back to 1 and says so.\n"
"\n"
"  iface=     WHICH INTERFACE the call may come in on - the name attach and\n"
"             ifconfig give it, not a protocol and not a TCP port.  It is\n"
"             what the rest of the AX.25 world calls a \"port\" (axports,\n"
"             ax25d), and it is not called that here because \"port\" is\n"
"             already the number in tcp:<host>:<port> on the same line.\n"
"             A comma separated list; \"!\" in front of the whole list\n"
"             excludes instead; \"all\" is every interface and is also what\n"
"             leaving it out means.  A name that is no interface is not\n"
"             refused - one may be attached later - but it is said, because\n"
"             until then the entry answers nothing.\n"
"  pid=       WHICH PROTOCOL rides on the connection, default text (0xf0) -\n"
"             see \"ax25 pid-info\".  I and UI are two separate listeners and\n"
"             each may name its own interfaces.\n"
"\n"
"  What is carried decides the rest: pid=text announces, does not wait, and\n"
"  converts as the target above says; another pid does none of the three,\n"
"  and the two switches that would corrupt a binary protocol are refused\n"
"  rather than ignored.";

static void axlisten_usage(void)
{
  printf("Usage: %s\n", Axlisten_usage);
}

/*---------------------------------------------------------------------------*/

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

/* Where the entry hands a call to, in the one wording used both by the
 * listing and by the line that reports a replacement - written down twice,
 * the two would drift apart.
 */

static const char *axlisten_targetname(const struct axlisten *lp)
{
  /* One caller per printf, and the "changed from" line copies the string
   * before the entry is touched, so one buffer is enough.
   */
  static char buf[80];
  const struct axlisten *cp;

  if (lp->kind != LK_CLIENT) return lp->target;
  /* A NET/ROM entry holds no claim of its own - it names the callsign whose
   * client takes the session, and THAT entry is where the claim lives.  So
   * the state shown here is the other entry's, including the case the column
   * exists for: the callsign has no client listener at all and the line
   * points nowhere.  Without it the entry reads "active" either way.
   */
  if (lp->netrom) {
    cp = axlisten_find(lp->call, PID_NO_L3, 0);
    snprintf(buf, sizeof(buf), "%s (%s)", lp->target,
	     !cp || cp->kind != LK_CLIENT ? "no listener" :
	     cp->clientfd >= 0            ? "claimed"     : "nobody");
    return buf;
  }
  return lp->clientfd >= 0 ? "client (claimed)" : "client (nobody)";
}

/*---------------------------------------------------------------------------*/

/* One header for both views - the whole list and one protocol's - because a
 * column layout that is written down twice is one that drifts apart.
 */

static void axlisten_header(void)
{
  printf(" #  Call       I/UI Pid       Kind    Mode     Iface      "
	 "State                          Handed to\n");
}

/*---------------------------------------------------------------------------*/

static void axlisten_show(struct axlisten *lp, int n)
{
  char buf[AXBUF];
  char pidbuf[16];
  char ports[64];

  /* Show the "!" back: it lives in the flag, not in the text, and a display
   * that hides it would show a list meaning the opposite of what it says.
   */
  if (!lp->ports.spec)
    strcpy(ports, "(all)");
  else
    snprintf(ports, sizeof(ports), "%s%s",
	     lp->ports.exclude ? "!" : "", lp->ports.spec);

  printf("%2d  %-10s %-4s %-9s %-7s %-8s %-10s %-30s %s%s%s\n", n,
	 lp->netrom ? "(netrom)" : pax25(buf, lp->call),
	 lp->netrom ? "-" : (lp->ui ? "UI" : "I"),
	 /* The protocol by name, not "pid" for everything that is not text:
	  * the column was showing that a pid was set and never which one.
	  */
	 lp->netrom ? "-" : pid_name(lp->pid, pidbuf, sizeof(pidbuf)),
	 axlisten_kindname(lp), lp->binary ? "binary" : "ascii",
	 ports,
	 lp->disabled ? (lp->why ? lp->why : "inactive") : "active",
	 axlisten_targetname(lp),
	 lp->silent ? "  silent" : "", lp->wait ? "  wait" : "");
  /* Who it runs as, where we run anything - a rights change nobody can see
   * is one nobody checks.
   */
  if (lp->kind == LK_PROGRAM)
    printf("%*sas %s(%lu) group %lu\n", 66, "",
	   lp->username ? lp->username : "uid",
	   (unsigned long) lp->uid, (unsigned long) lp->gid);
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
    /* A NET/ROM entry may be sending its sessions to this callsign's client.
     * It holds the callsign and not a pointer, so nothing here dangles - but
     * from now on it finds nobody, and that should not happen quietly.
     */
    if (!netrom && lp->kind == LK_CLIENT &&
	axlisten_netrom_pointing_at(lp->call)) {
      char cbuf[AXBUF];

      printf("  \"listen netrom add client:%s\" now has no client - NET/ROM\n"
	     "  sessions go to the node's own login until one holds it again\n",
	     pax25(cbuf, lp->call));
    }
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
  char callbuf[AXBUF];
  char oldtarget[160];
  int ascii_set = 0;
  int i;
  int lostclaim = 0;
  int replaced = 0;
  int wasclient = 0;
  int noisy_set = 0;
  char *user = 0;
  char *group = 0;
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
      /* A name as well as a number - "pid=netrom" beside "pid=0xcf" - and
       * the same names everywhere, see pidfilter.c.  A number alone was
       * hard to write and harder to read back off a display.
       */
      if ((pid = pid_number(cp + 4)) < 0) {
	printf("Invalid pid \"%s\" - a name or a number, see "
	       "\"ax25 pid-info\"\n", cp + 4);
	return 1;
      }
      continue;
    }
    if (!strcmp(cp, "--silent")) { silent = 1; continue; }
    if (!strcmp(cp, "--verbose")) { silent = 0; noisy_set = 1; continue; }
    if (!strcmp(cp, "--wait"))   { wait = 1; continue; }
    if (!strcmp(cp, "--nowait")) { wait = 0; continue; }
    /* Connections or datagrams, and which ports.  Written without dashes
     * because they say what the entry IS, not how it behaves.
     */
    if (!strncmp(cp, "iface=", 6)) { ports = cp + 6; continue; }
    if (!strncmp(cp, "port=", 5)) {
      /* The AX.25 world says "port" and means the interface - axports, ax25d,
       * the port column there.  Here it is called what attach and ifconfig
       * call it, and saying so is better than "unknown option": whoever
       * typed it was not guessing, only coming from the other direction.
       */
      printf("\"port=\" is called \"iface=\" here - the interface, the way "
	     "attach and\nifconfig name it.  For the protocol id there is "
	     "pid=\n");
      return 1;
    }
    if (!strcmp(cp, "UI") || !strcmp(cp, "ui")) { ui = 1; continue; }
    if (!strcmp(cp, "I")  || !strcmp(cp, "i"))  { ui = 0; continue; }
    if (!strcmp(cp, "--binary")) { binary = 1; continue; }
    if (!strcmp(cp, "--user")) {
      if (++i >= argc) { printf("--user needs a name\n"); return 1; }
      user = argv[i];
      continue;
    }
    if (!strcmp(cp, "--group")) {
      if (++i >= argc) { printf("--group needs a name\n"); return 1; }
      group = argv[i];
      continue;
    }
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

  /* A datagram is handed over byte for byte with a count in front, and
   * nothing on that path converts anything - see remote_net_send_frame().
   * Offering ascii here would be promising a conversion nobody performs,
   * and the display would then show a mode that is not in force.
   */
  if (ui) {
    if (ascii_set) {
      printf("Converting a datagram would corrupt it\n");
      return 1;
    }
    binary = 1;
  }

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
    /* binary is left open on purpose: for text it depends on WHERE the
     * session goes, and the target is not known yet.  See below.
     */
  }

  /* "client" hands the call to a program that claimed a CALLSIGN over the
   * service socket.  A NET/ROM session has no callsign of its own - it
   * reaches the node, not a service on it - so a NET/ROM entry has to name
   * the callsign whose client is meant, and "client" alone cannot say it.
   *
   * Written as the same callsign an AX.25 listener uses, the two are meshed
   * on purpose: one program takes both kinds of call.  Written as a different
   * one, they stay apart, because the client is told which callsign was
   * reached - the same field ax25d picks its stanza by.
   */
  if (netrom && !strcmp(target, "client")) {
    printf("A NET/ROM session has no callsign of its own - write "
	   "\"client:<call>\"\nto hand it to the program holding <call>\n");
    return 1;
  }
  if (!netrom && !strncmp(target, "client:", 7)) {
    /* Say what the form is FOR.  "already has its callsign" alone reads as a
     * complaint about the line, and the line is right - what is missing is
     * that the colon form exists only because a NET/ROM session has no
     * callsign to name a client by.  Here the entry names one.
     */
    printf("\"client:<call>\" belongs to netrom, where a session has no "
	   "callsign of its\nown to find a client by.  This entry names one "
	   "already: whoever holds\n%s gets these calls, so the target is "
	   "plain \"client\"\n", pax25(callbuf, call));
    return 1;
  }
  if (netrom && !strncmp(target, "client:", 7)) {
    if (!target[7] || setcall(call, target + 7)) {
      printf("\"%s\" is not a callsign\n", target + 7);
      return 1;
    }
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
  } else {
    /* An "add" for a callsign already listed REPLACES that entry, and it used
     * to do so without a word.  A sysop who meant to add a second listener saw
     * nothing telling him he had overwritten the first - and for a claimed
     * client entry the silent replacement takes a running program's listener
     * away from it.  Keep what was there; the line is printed once the new
     * target is known, so it can name both.
     */
    snprintf(oldtarget, sizeof(oldtarget), "%s", axlisten_targetname(lp));
    wasclient = (lp->kind == LK_CLIENT);
    replaced = 1;
    free(lp->target);
  }

  if (!strcmp(target, "builtin:login"))
    lp->kind = LK_LOGIN;
  else if (!strcmp(target, "client") || !strncmp(target, "client:", 7)) {
    lp->kind = LK_CLIENT;
    /* A NET/ROM entry carries a callsign that is not its own to listen for:
     * it names whose client takes the session, and it is what the far end is
     * told it reached.  On an AX.25 entry the callsign is already there.
     */
    if (netrom) addrcp(lp->call, call);
  } else if (*target == '/')
    lp->kind = LK_PROGRAM;
  else {
    lp->kind = LK_SOCKET;
    /* "tcp:host:port" is how a sysop writes it; build_sockaddr() wants the
     * host and port alone, and knows "unix:" for itself.
     */
    if (!strncmp(target, "tcp:", 4)) target += 4;
  }
  /* THE END-OF-LINE DEFAULT FOLLOWS THE TARGET, because the target is what
   * decides which convention the other side speaks.  Only where neither
   * --ascii nor --binary was given, and only for text - anything else was
   * settled above, where converting would corrupt the protocol.
   *
   *   builtin:login   ascii - the node's own login lives on CR
   *   /path/program   ascii - an ordinary unix program wants LF
   *   tcp:host:port   ascii - measured on db0fhn 2026-08-20: the announcement
   *                   leaves as "...\n", and a CR typed on the AX.25 side
   *                   arrives at the socket as LF
   *   client          BINARY - the other end is a libax25 program, which
   *                   speaks the packet radio convention itself.  Converting
   *                   for it corrupts quietly, and having to remember
   *                   --binary on every such line is the wrong way round.
   */
  if (binary < 0)
    binary = (lp->kind == LK_CLIENT);

  /* WHO IT RUNS AS, and only where we run something: "client" hands the
   * session to a program that is already running under its own account, and
   * "builtin:login" asks the system who this is before it grants anything.
   */
  if (lp->kind == LK_PROGRAM) {
    free(lp->username);
    lp->username = 0;
    /* The user first: without --group he brings his own, which is what "su"
     * does and what "--user root" plainly means.  Only where no account
     * answers at all does 1 remain.
     */
    lp->gid = 1;
    user_id(user ? user : "daemon", &lp->uid, &lp->gid, &lp->username,
	    &lp->home);
    if (group)
      group_id(group, &lp->gid);
  } else if (user || group)
    printf("--user and --group are for a program; \"%s\" runs elsewhere\n",
	   axlisten_targetname(lp));

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
  if (lp->kind == LK_CLIENT)
    lp->silent = 1;
  else if (lp->clientfd >= 0) {
    /* It stops being a client entry while a client still holds it.  The claim
     * belongs to the entry, so it goes with it - otherwise the descriptor sits
     * here unreachable: axlisten_client_gone() only clears LK_CLIENT entries,
     * so a later switch back to "client" would show a long dead fd as
     * "claimed" and refuse the next claimant with "already taken".
     */
    lp->clientfd = -1;
    lostclaim = 1;
  }
  if (replaced)
    printf("%s: changed from %s to %s%s\n",
	   netrom ? "netrom" : pax25(callbuf, lp->call),
	   oldtarget, axlisten_targetname(lp),
	   lostclaim ? " - the program that had claimed it loses the listener"
		     : "");
  /* It has stopped being a client entry, and a NET/ROM entry sends its
   * sessions to the client of this callsign.  Nothing breaks - the lookup
   * simply finds no client and the session goes to the node's own login - but
   * silently, which is the wrong way for a configuration to change meaning.
   */
  if (!netrom && wasclient && lp->kind != LK_CLIENT &&
      axlisten_netrom_pointing_at(lp->call))
    printf("  \"listen netrom add client:%s\" now has no client - NET/ROM\n"
	   "  sessions go to the node's own login until one holds it again\n",
	   pax25(callbuf, lp->call));

  /* The other direction, and it is NOT refused: net.rc is read from the top
   * and nothing says the ax25 line comes first, so a NET/ROM entry written
   * ahead of it is perfectly reasonable.  It is said once, because an entry
   * pointing at a callsign nobody listens for looks exactly like a working
   * one otherwise - which is what "active" in the listing used to claim.
   */
  if (netrom && lp->kind == LK_CLIENT) {
    struct axlisten *cp = axlisten_find(lp->call, PID_NO_L3, 0);

    if (!cp || cp->kind != LK_CLIENT)
      printf("  no \"listen ax25 add %s client\" yet - until there is one,\n"
	     "  NET/ROM sessions go to the node's own login\n",
	     pax25(callbuf, lp->call));
  }
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

/* "--user" and "--group" take a name or a number.  A name that no system
 * knows is not a reason to run the program with the node's own rights, so it
 * falls back to 1 - which is "daemon" on Linux and macOS both, and the reason
 * "daemon" is the default rather than "nobody" (65534 against 4294967294).
 * Said out loud, because a program silently running as somebody else than
 * configured is the sort of thing found much later.
 */

static int user_id(const char *name, uid_t *uid, gid_t *gid, char **keep,
		   char **home)
{
  struct passwd *pw;
  char *end;
  long n;

  if ((pw = getpwnam(name)) != NULL) {
    *uid = pw->pw_uid;
    *gid = pw->pw_gid;                  /* his own group, unless --group */
    free(*keep);
    *keep = strdup(name);
    free(*home);
    *home = (pw->pw_dir && *pw->pw_dir) ? strdup(pw->pw_dir) : 0;
    return 0;
  }
  n = strtol(name, &end, 10);
  if (*name && !*end && n >= 0) {
    /* A number is allowed, and the account behind it is still worth asking
     * about: it carries the groups and the home directory.
     */
    *uid = (uid_t) n;
    free(*keep);
    free(*home);
    *keep = 0;
    *home = 0;
    if ((pw = getpwuid(*uid)) != NULL) {
      *gid = pw->pw_gid;
      *keep = strdup(pw->pw_name);
      if (pw->pw_dir && *pw->pw_dir) *home = strdup(pw->pw_dir);
    }
    return 0;
  }
  printf("No user \"%s\" - running as uid 1 (daemon)\n", name);
  *uid = 1;
  free(*keep);
  free(*home);
  *keep = 0;
  *home = 0;
  return 0;
}

static int group_id(const char *name, gid_t *gid)
{
  struct group *gr;
  char *end;
  long n;

  if ((gr = getgrnam(name)) != NULL) {
    *gid = gr->gr_gid;
    return 0;
  }
  n = strtol(name, &end, 10);
  if (*name && !*end && n >= 0) {
    *gid = (gid_t) n;
    return 0;
  }
  printf("No group \"%s\" - running as gid 1 (daemon)\n", name);
  *gid = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* A name that is no port is not refused - a port may be attached after the
 * listen entry is written, and that is why this was never checked - but it is
 * SAID, because the entry is then deaf and looks exactly like a working one.
 *
 * "port=text" is the way it happens: port and pid sit next to each other in
 * the same line, and "text" is a pid.  So a name that names a protocol is
 * answered with the question that was probably meant.
 */

/* WHERE THE CALL CAME IN, as the client is told it - and the client picks
 * what to run by it: ax25d chooses its stanza by port and callsign.
 *
 * A session that never left the node has no interface, and "?" would leave
 * the client with nothing to match.  It is called "local", which is a name a
 * sysop can write in ax25d.conf like any other port.  (If an interface is
 * ever named "local" the two read alike - nothing breaks, but the line no
 * longer says which of the two it was.)
 */

const char *axlisten_portname(const struct ax25_cb *axp)
{
	if (axp->iface != NULL)
		return axp->iface->name;
	return axp->loop != NULL ? "local" : "?";
}

/*---------------------------------------------------------------------------*/

static void portlist_warn_unknown(const char *spec)
{
  char copy[256];
  char *name;
  char *rest;

  snprintf(copy, sizeof(copy), "%s", spec);
  for (name = strtok_r(copy, ",", &rest); name;
       name = strtok_r(NULL, ",", &rest)) {
    if (*name == '!') name++;
    if (!*name || if_lookup(name)) continue;
    printf("No interface called \"%s\" - this entry answers nothing until one "
	   "is attached\n", name);
    if (pid_number(name) >= 0)
      printf("  (\"%s\" is a protocol id - did you mean pid=%s?)\n",
	     name, name);
  }
}

/*---------------------------------------------------------------------------*/

int portlist_set(struct portlist *pl, const char *spec, char *err, int errlen)
{
  const char *p;

  free(pl->spec);
  pl->spec = 0;
  pl->exclude = 0;

  if (!spec || !*spec) return 0;        /* every port */

  /* "all" says out loud what an empty list means.  Worth having: an entry
   * that listens everywhere then looks different from one where the port was
   * forgotten, and the two are impossible to tell apart in a config file.
   */
  if (!strcmp(spec, "all")) return 0;

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
  portlist_warn_unknown(spec);
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

/* Is this callsign a port's, and does the node serve that protocol? */

static int local_conflict(const uint8 *call, int pid)
{
  return ismyax25addr((uint8 *) call) && pid_is_local(pid);
}

void axlisten_drop_local(const uint8 *call)
{
  char buf[AXBUF];
  char pidbuf[16];
  struct axlisten *lp;

  if (!call || !*call) return;

  for (lp = Axlisten; lp; lp = lp->next) {
    if (lp->netrom || lp->disabled) continue;
    if (!addreq(lp->call, call) || !pid_is_local(lp->pid)) continue;
    printf("listen %s pid=%s switched off: the port answers to that "
	   "callsign and the node serves that protocol itself\n",
	   pax25(buf, lp->call), pid_name(lp->pid, pidbuf, sizeof(pidbuf)));
    if (lp->clientfd >= 0) {
      remote_net_drop_client(lp->clientfd);
      lp->clientfd = -1;
    }
    lp->disabled = 1;
    free(lp->why);
    lp->why = strdup("inactive: port callsign");
  }
}

/* "listen ax25 enable <n>": try again.  The operator says when, because a
 * node that switched entries back on by itself would need a hook wherever a
 * conflict can end - interface detached, callsign moved away, protocol
 * stopped - and one forgotten hook leaves an entry dead that looks alive.
 */

int axlisten_enable(int n)
{
  char buf[AXBUF];
  char pidbuf[16];
  struct axlisten *lp;
  int i = 1;

  for (lp = Axlisten; lp; lp = lp->next, i++)
    if (i == n) break;
  if (!lp) {
    printf("No entry %d\n", n);
    return 1;
  }
  if (!lp->disabled) {
    printf("Entry %d is active\n", n);
    return 0;
  }
  if (local_conflict(lp->call, lp->pid)) {
    printf("%s pid=%s is still a port callsign the node serves\n",
	   pax25(buf, lp->call), pid_name(lp->pid, pidbuf, sizeof(pidbuf)));
    return 1;
  }
  lp->disabled = 0;
  free(lp->why);
  lp->why = 0;
  return 0;
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
    axlisten_header();
    { int n = 1;
      for (lp = Axlisten; lp; lp = lp->next) axlisten_show(lp, n++); }
    return 0;
  }

  if (!strcmp(argv[1], "?") || !strcmp(argv[1], "help")) {
    axlisten_usage();
    return 0;
  }

  if (!strcmp(argv[1], "ax25"))
    netrom = 0;
  else if (!strcmp(argv[1], "netrom"))
    netrom = 1;
  else {
    printf("\"%s\": the first word is \"ax25\" or \"netrom\"\n", argv[1]);
    axlisten_usage();
    return 1;
  }

  if (argc < 3) {
    int n = 1;
    int seen = 0;

    /* Numbered by position in the whole list, not in what is shown, so that
     * an index means the same thing whichever view it came from.
     */
    for (lp = Axlisten; lp; lp = lp->next, n++)
      if (lp->netrom == netrom) {
	if (!seen++) axlisten_header();
	axlisten_show(lp, n);
      }
    /* With nothing configured this printed NOTHING AT ALL, and a command that
     * answers with silence reads as though it did not exist.  The useful
     * reply is what one would have to write, so say so and show it - the same
     * reason "netrom filter" carries its own help.
     */
    if (!seen) {
      printf("Not listening for anything on %s\n",
	     netrom ? "netrom" : "ax25");
      axlisten_usage();
    }
    return 0;
  }

  if (!strcmp(argv[2], "?") || !strcmp(argv[2], "help")) {
    axlisten_usage();
    return 0;
  }

  if (!strcmp(argv[2], "enable")) {
    if (argc < 4) {
      printf("Which entry?  The number is the first column of \"listen\"\n");
      return 1;
    }
    return axlisten_enable(atoi(argv[3]));
  }

  if (!strcmp(argv[2], "add")) {
    if (argc > 3 && (!strcmp(argv[3], "?") || !strcmp(argv[3], "help"))) {
      axlisten_usage();
      return 0;
    }
    return axlisten_add(netrom, argc - 3, argv + 3);
  }

  if (!strcmp(argv[2], "drop")) {
    int pid = PID_NO_L3;
    int ui = 0;
    int i = 3;

    memset(call, 0, sizeof(call));
    if (!netrom) {
      /* Same words as on the add line, and in any order for the same reason:
       * "drop pid=netrom UI <call>" reads exactly like the add line that
       * created the entry, and used to find nothing because pid= was only
       * looked for after the I/UI word.
       */
      for (; i < argc; i++) {
	if (!strcmp(argv[i], "UI") || !strcmp(argv[i], "ui") ||
	    !strcmp(argv[i], "I")  || !strcmp(argv[i], "i")) {
	  ui = (argv[i][0] == 'U' || argv[i][0] == 'u');
	  continue;
	}
	if (!strncmp(argv[i], "pid=", 4)) {
	  if ((pid = pid_number(argv[i] + 4)) < 0) {
	    printf("Invalid pid \"%s\" - a name or a number, see "
		   "\"ax25 pid-info\"\n", argv[i] + 4);
	    return 1;
	  }
	  continue;
	}
	break;
      }
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

  printf("\"%s\": say \"add\", \"drop\" or \"enable\"\n", argv[2]);
  axlisten_usage();
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
static int axserv_handover(struct axlisten *lp, const char *line, int *fdp);

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
	  axlisten_portname(axp), pax25(call, axp->hdr.dest));
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


static int axspawn_fd(struct axlisten *lp, const char *user, int netrom,
		      const char *proto, const char *dest, const char *path)
{

  char *argv[32];
  char buf[512];
  char env[10][200];
  char *envp[11];
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
  /* Which world this is comes in as a flag and no longer from comparing the
   * protocol name against "netrom".  The name is now the real protocol id
   * (pid_name()), so an AX.25 listener configured with pid=netrom would have
   * been taken for a NET/ROM session and told the wrong story entirely.
   */
  {
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
    /* And the same two things the shell would have set, since we set who the
     * program is: a program that looks for its files under $HOME would
     * otherwise find the node's.
     */
    sprintf(env[envc++], "HOME=%.150s", lp->home ? lp->home : "/");
    if (lp->username)
      sprintf(env[envc++], "USER=%.60s", lp->username);
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
    /* GIVE UP WHAT THE NODE HAS before running anything.  The group first:
     * afterwards there is no privilege left to change it with.  A failure
     * here is fatal to the child and not a warning - carrying on would run
     * the program with exactly the rights this is here to take away.
     */
    if (getuid() == 0) {
      if (lp->username != NULL)
	(void) initgroups(lp->username, lp->gid);
      if (setgid(lp->gid) < 0 || setuid(lp->uid) < 0)
	_exit(1);
    }
    /* And somewhere it may be: without this the program stands wherever the
     * node was started from, which depends on the start script and on nothing
     * the sysop configured here.
     */
    if (lp->home == NULL || chdir(lp->home) < 0)
      (void) chdir("/");
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
    if ((fd = axspawn_fd(lp, user, 1, "netrom", node, 0)) < 0) return 0;
  }

  /* "client:<call>" - the session goes to the program holding that callsign,
   * and it is shown the call as though it had arrived over AX.25.  The claim
   * lives on the AX.25 entry for the callsign; this entry only names it, so a
   * callsign nobody claimed leaves the session to the node's own login rather
   * than dropping it.
   */
  if (lp->kind == LK_CLIENT) {
    char line[256];
    char user[AXBUF], node[AXBUF], called[AXBUF];
    struct axlisten *cp;

    if (!(cp = axlisten_find(lp->call, PID_NO_L3, 0)) ||
	cp->kind != LK_CLIENT || cp->clientfd < 0)
      return 0;
    /* The node the user sits on goes where a digipeater would, because that
     * is what it is: the way the call came.  parse_call() at the far end
     * reads it as one, so the program sees "DL1ABC-7 via DB0XYZ" calling
     * <call> - an ordinary incoming connection, and ax25d picks its stanza by
     * the callsign after the ">" exactly as it always does.
     */
    snprintf(line, sizeof(line), "netrom %s,%s > %s",
	     pax25(user, pc->cuser), pax25(node, pc->node),
	     pax25(called, lp->call));
    if (axserv_handover(cp, line, &fd) < 0) return 0;
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

static int axserv_handover(struct axlisten *lp, const char *line, int *fdp)
{
  char buf[256];
  int sv[2];
  struct cmsghdr *cm;
  struct iovec iov;
  struct msghdr msg;
  union {                               /* aligned as a cmsghdr wants */
    char buf[CMSG_SPACE(sizeof(int))];
    struct cmsghdr align;
  } control;

  /* "<port> <caller>[,<path>...] > <called>".  Built by the caller, because
   * an AX.25 link and a NET/ROM circuit hold those names in different places
   * and neither shape belongs in here.  The far side does not ask which
   * protocol it was: wampes_accept() reads two callsigns out of this line and
   * nothing else.
   */
  snprintf(buf, sizeof(buf), "%s\n", line);

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
      char line[256];
      char call[AXBUF];
      int fd;
      int i;

      if (lp->clientfd < 0) break;      /* nobody there - fall through to
					 * the refusal below, with a reason */
      snprintf(line, sizeof(line), "%s %s",
	       axlisten_portname(axp), pax25(call, axp->hdr.dest));
      for (i = 0; i < axp->hdr.ndigis; i++) {
	strcat(line, ",");
	strcat(line, pax25(call, axp->hdr.digis[i]));
      }
      strcat(line, " > ");
      strcat(line, pax25(call, axp->hdr.source));
      if (axserv_handover(lp, line, &fd) == 0) sp = axpipe_open(axp, lp, fd);
    }
    break;
  case LK_PROGRAM:
    {
      char user[AXBUF], dest[AXBUF], path[160];
      char pidbuf[16];
      int i, fd;

      pax25(user, axp->hdr.dest);
      pax25(dest, axp->hdr.source);
      path[0] = '\0';
      for (i = 0; i < axp->hdr.ndigis; i++) {
	char one[AXBUF];

	if (i) strcat(path, ",");
	strcat(path, pax25(one, axp->hdr.digis[i]));
      }
      if ((fd = axspawn_fd(lp, user, 0,
			   pid_name(pid, pidbuf, sizeof(pidbuf)),
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
  /* nr_is_peer() is the configured interlink partner, and it belongs here
   * even before the interlink itself exists: he was named as a partner, so a
   * greeting is wrong whether or not he has said anything yet.  The other two
   * ask what a station has already done, and a partner that has just come up
   * has done nothing - which is exactly when the greeting would land in his
   * first L3 frame.
   */
  partner = nr_is_neighbour(axp->hdr.dest) || nr_is_peer(axp->hdr.dest) ||
	    flexnet_is_peer(axp->hdr.dest);

  /* Connections only.  A UI entry describes what to do with a frame that
   * arrives without one, which is axlisten_ui_deliver()'s business, and a
   * link coming up is none of its own.  Without the test a callsign that has
   * both an I and a UI entry - the same pid, as it must be - matches twice
   * here, and both rounds resolve to the same connected entry, because
   * axserv_start() looks it up with axlisten_find(..., pid, 0).  The session
   * is then opened twice on one link: the caller sees the service greet him
   * twice and one of the two carries on.  Observed on db0fhn with
   * "listen ax25 add ui db0fhn-13 client" beside the connected entry.
   */
  for (lp = Axlisten; lp; lp = lp->next)
    if (!lp->netrom && !lp->ui && !lp->wait && !partner
	&& addreq(lp->call, axp->hdr.source))
      (void) axserv_start(axp, lp->pid);
}

/*---------------------------------------------------------------------------*/

int ax25start(int argc, char *argv[], void *p)
{
  if (!Axserver_enabled) login_announce("AX.25", 1);
  Axserver_enabled = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

int ax250(int argc, char *argv[], void *p)
{
  if (Axserver_enabled) login_announce("AX.25", 0);
  Axserver_enabled = 0;
  return 0;
}
