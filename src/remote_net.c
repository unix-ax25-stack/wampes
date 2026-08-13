/* @(#) $Id: remote_net.c,v 1.35 2006/02/12 17:49:57 dl9sau Exp $ */

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#include "global.h"
#include "mbuf.h"
#include "iface.h"
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
};

struct cmdtable {
  const char *name;                     /* Command name (lower case) */
  int (*fnc)(struct controlblock *cp);  /* Command function */
};

static int fkbd = -1;

/* The listening sockets, and their descriptors alongside.  There used to be
 * a single descriptor here while the name list was already an array - which
 * worked only because the array held one entry.  A second listener would
 * have overwritten the variable, and both accept handlers would then have
 * taken connections off whichever socket came last.
 */
static const char *socketnames[] = {
  "unix:" TCPDIR "/.sockets/netcmd",
  0
};

#define NSOCKETNAMES (sizeof(socketnames) / sizeof(socketnames[0]))

static int flisten_net[NSOCKETNAMES];

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

static void transport_state_upcall(struct transport_cb *tp)
{
  delete_controlblock((struct controlblock *) tp->user);
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
  char *protocol, *address;

  protocol = getarg(0, 0);
  address = getarg(0, 1);
  cp->tp = transport_open(protocol, address, transport_recv_upcall, transport_send_upcall, transport_state_upcall, (char *) cp);
  if (!cp->tp) return -1;
  if (cp->tp->type == TP_AXFLEXTALK)
    cp->binary = 1;
  if (!cp->binary) {
    cp->tp->recv_mode = EOL_LF;
    cp->tp->send_mode = (!strcmp(protocol, "tcp")) ? EOL_CRLF : EOL_CR;
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

  static const struct cmdtable command_table[] = {
    { "ascii",   ascii_command },
    { "binary",  binary_command },
    { "command", command_command },
    { "connect", connect_command },
    { "console", console_command },
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
  if (command_switcher(cp, getarg(cp->buffer, 0), command_table))
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

  addrlen = sizeof(addr);
  /* Which socket woke us: on_read() carries the slot, so this works with
   * any number of listeners.
   */
  if ((fd = accept(*(int *) p, (struct sockaddr *) &addr, &addrlen)) < 0)
    return;
  cp = (struct controlblock *) calloc(1, sizeof(struct controlblock));
  if (!cp) {
    close(fd);
    return;
  }
  cp->fd = fd;
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

void remote_net_initialize(void)
{

  int addrlen, i;
  int arg;
  int fd;
  struct sockaddr *addr;

  for (i = 0; i < (int) NSOCKETNAMES; i++)
    flisten_net[i] = -1;

  for (i = 0; socketnames[i]; i++) {
    if ((addr = build_sockaddr(socketnames[i], &addrlen))) {
      if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) >= 0) {
	switch (addr->sa_family) {
	case AF_INET:
	  arg = 1;
	  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	  break;
#if HAS_AF_INET6
	case AF_INET6:
	  arg = 1;
	  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	  /* Pin this down rather than inheriting it: whether an IPv6 socket
	   * also accepts IPv4 is a system default that differs between Linux
	   * and the BSDs.  Fixed to v6-only, a "*:port" and a "[::]:port" entry
	   * can coexist everywhere instead of fighting over the port on some
	   * systems.
	   */
	  arg = 1;
	  setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, (char *) &arg, sizeof(arg));
	  break;
#endif
	}
	if (!bind_socket(fd, addr, addrlen) &&
	    !listen(fd, SOMAXCONN)) {
	  flisten_net[i] = fd;
	  on_read(fd, accept_connection_net, &flisten_net[i]);
	} else {
	  /* Worth saying out loud: without this socket there is no cnet, and
	   * the usual reason is the one named here.
	   */
	  printf("Cannot listen on %s: %s\n", socketnames[i],
		 errno == EADDRINUSE ?
		 "in use - another net is already running" : strerror(errno));
	  close(fd);
	}
      }
    } else {
      printf("Cannot use %s as a listening address\n", socketnames[i]);
    }
  }
}
