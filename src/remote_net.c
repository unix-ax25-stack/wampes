/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/remote_net.c,v 1.12 1992/08/24 10:09:40 deyke Exp $ */

#include "global.h"

#include <sys/types.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#ifdef LINUX
#include "linux.h"
#endif

#include "mbuf.h"
#include "timer.h"
#include "transport.h"
#include "hpux.h"
#include "buildsaddr.h"

struct controlblock {
  int  fd;                              /* Socket descriptor */
  char  buffer[1024];                   /* Input buffer */
  int  bufcnt;                          /* Number of bytes in buffer */
  struct transport_cb *tp;              /* Transport handle */
  int  binary;                          /* Transfer is binary (no EOL conv) */
};

struct cmdtable {
  char  *name;                          /* Command name (lower case) */
  int  (*fnc) __ARGS((struct controlblock *cp)); /* Command function */
};

static int  flisten = -1;

static char *getarg __ARGS((char *line, int all));
static int command_switcher __ARGS((struct controlblock *cp, char *name, struct cmdtable *tableptr));
static void delete_controlblock __ARGS((struct controlblock *cp));
static void transport_try_send __ARGS((struct controlblock *cp));
static void transport_recv_upcall __ARGS((struct transport_cb *tp, int cnt));
static void transport_send_upcall __ARGS((struct transport_cb *tp, int cnt));
static void transport_state_upcall __ARGS((struct transport_cb *tp));
static int ascii_command __ARGS((struct controlblock *cp));
static int binary_command __ARGS((struct controlblock *cp));
static int connect_command __ARGS((struct controlblock *cp));
static void command_receive __ARGS((struct controlblock *cp));
static void accept_connection __ARGS((void *p));

/*---------------------------------------------------------------------------*/

static char  *getarg(line, all)
char  *line;
int  all;
{

  char  *arg;
  int  c, quote;
  static char  *p;

  if (line) p = line;
  while (isspace(uchar(*p))) p++;
  if (all) return p;
  quote = '\0';
  if (*p == '"' || *p == '\'') quote = *p++;
  arg = p;
  if (quote) {
    if (!(p = strchr(p, quote))) p = "";
  } else
    while (*p && !isspace(uchar(*p))) {
      c = tolower(uchar(*p));
      *p++ = c;
    }
  if (*p) *p++ = '\0';
  return arg;
}

/*---------------------------------------------------------------------------*/

static int  command_switcher(cp, name, tableptr)
struct controlblock *cp;
char  *name;
struct cmdtable *tableptr;
{
  int  namelen;

  namelen = strlen(name);
  for (; ; ) {
    if (!tableptr->name) return (-1);
    if (!strncmp(tableptr->name, name, namelen)) return (*tableptr->fnc)(cp);
    tableptr++;
  }
}

/*---------------------------------------------------------------------------*/

static void delete_controlblock(cp)
struct controlblock *cp;
{
  off_read(cp->fd);
  close(cp->fd);
  free(cp);
}

/*---------------------------------------------------------------------------*/

static void transport_try_send(cp)
struct controlblock *cp;
{

  int  cnt;
  struct mbuf *bp;

  cnt = transport_send_space(cp->tp);
  if (cnt <= 0) {
    off_read(cp->fd);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = read(cp->fd, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(bp);
    off_read(cp->fd);
    transport_close(cp->tp);
    return;
  }
  bp->cnt = cnt;
  transport_send(cp->tp, bp);
}

/*---------------------------------------------------------------------------*/

static void transport_recv_upcall(tp, cnt)
struct transport_cb *tp;
int16 cnt;
{

  char  buffer[1024];
  struct controlblock *cp;
  struct mbuf *bp;

  cp = (struct controlblock *) tp->user;
  transport_recv(tp, &bp, 0);
  while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
    if (write(cp->fd, buffer, (unsigned) cnt) != cnt) transport_close(tp);
}

/*---------------------------------------------------------------------------*/

static void transport_send_upcall(tp, cnt)
struct transport_cb *tp;
int16 cnt;
{
  struct controlblock *cp;

  cp = (struct controlblock *) tp->user;
  on_read(cp->fd, (void (*)()) transport_try_send, cp);
}

/*---------------------------------------------------------------------------*/

static void transport_state_upcall(tp)
struct transport_cb *tp;
{
  delete_controlblock((struct controlblock *) tp->user);
  transport_del(tp);
}

/*---------------------------------------------------------------------------*/

static int  ascii_command(cp)
struct controlblock *cp;
{
  cp->binary = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  binary_command(cp)
struct controlblock *cp;
{
  cp->binary = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  connect_command(cp)
struct controlblock *cp;
{
  char  *protocol, *address;

  protocol = getarg((char *) 0, 0);
  address = getarg((char *) 0, 1);
  cp->tp = transport_open(protocol, address, transport_recv_upcall, transport_send_upcall, transport_state_upcall, (char *) cp);
  if (!cp->tp) return (-1);
  if (!cp->binary) {
    cp->tp->recv_mode = EOL_LF;
    cp->tp->send_mode = (!strcmp(protocol, "tcp")) ? EOL_CRLF : EOL_CR;
  }
  on_read(cp->fd, (void (*)()) transport_try_send, cp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void command_receive(cp)
struct controlblock *cp;
{

  static struct cmdtable command_table[] = {
    "ascii",   ascii_command,
    "binary",  binary_command,
    "connect", connect_command,
    0,         0
  };

  char  c;

  if (read(cp->fd, &c, 1) <= 0) {
    delete_controlblock(cp);
    return;
  }
  if (c != '\n') {
    cp->buffer[cp->bufcnt++] = c;
    if (cp->bufcnt >= sizeof(cp->buffer)) delete_controlblock(cp);
    return;
  }
  cp->buffer[cp->bufcnt] = '\0';
  cp->bufcnt = 0;
  if (command_switcher(cp, getarg(cp->buffer, 0), command_table))
    delete_controlblock(cp);
}

/*---------------------------------------------------------------------------*/

static void accept_connection(p)
void *p;
{

  int  addrlen;
  int  fd;
  struct controlblock *cp;
  struct sockaddr addr;

  addrlen = sizeof(addr);
  if ((fd = accept(flisten, &addr, &addrlen)) < 0) return;
  cp = (struct controlblock *) calloc(1, sizeof (struct controlblock ));
  if (!cp) {
    close(fd);
    return;
  }
  cp->fd = fd;
  on_read(cp->fd, (void (*)()) command_receive, cp);
}

/*---------------------------------------------------------------------------*/

void remote_net_initialize()
{

  static char  *socketnames[] = {
#if (defined(hpux)||defined(__hpux)||defined(LINUX))
    "unix:/tcp/.sockets/netcmd",
#else
    "*:4718",
#endif
    (char *) 0
  };

  int  addrlen, i;
  int  arg;
  struct sockaddr *addr;

  for (i = 0; socketnames[i]; i++) {
    if (addr = build_sockaddr(socketnames[i], &addrlen)) {
      if ((flisten = socket(addr->sa_family, SOCK_STREAM, 0)) >= 0) {
	switch (addr->sa_family) {
	case AF_UNIX:
	  if (!Debug) unlink(addr->sa_data);
	  break;
	case AF_INET:
	  arg = 1;
	  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
	  break;
	}
	if (!bind(flisten, addr, addrlen) && !listen(flisten, SOMAXCONN)) {
	  on_read(flisten, accept_connection, (void *) 0);
	} else {
	  close(flisten);
	  flisten = -1;
	}
      }
    }
  }
}

