/* @(#) $Id: tcpgate.c,v 1.18 1997/08/04 16:06:42 deyke Exp $ */

#include "global.h"

#include <sys/types.h>

#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mbuf.h"
#include "netuser.h"
#include "tcp.h"
#include "hpux.h"
#include "buildsaddr.h"
#include "domain.h"
#include "commands.h"

/* Addresses allowed to reach one gate.  An empty list allows everyone, which
 * is what this always did.
 */
struct allow {
  int32 lo;
  int32 hi;
  struct allow *next;
};

struct dest {
  int port;
  char *name;
  struct allow *allow;
  struct dest *next;
};

static struct dest *dests;

/*---------------------------------------------------------------------------*/

/* <addr>[/<bits>], the same form ipfilter takes */

static int parse_allow(const char *arg, int32 *lop, int32 *hip)
{
  char buf[256];
  char *bitp;
  int bits;
  int32 addr;
  int32 mask;

  if (strlen(arg) >= sizeof(buf)) return -1;
  strcpy(buf, arg);
  bits = 32;
  if ((bitp = strchr(buf, '/'))) {
    *bitp++ = 0;
    bits = atoi(bitp);
    if (bits < 0 || bits > 32) return -1;
  }
  if (!(addr = resolve(buf)) && strcmp(buf, "0.0.0.0")) return -1;
  mask = bits ? (int32) (~0UL << (32 - bits)) : 0;
  *lop = addr & mask;
  *hip = *lop | ~mask;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int allowed(const struct dest *dp, int32 addr)
{
  const struct allow *ap;

  if (!dp->allow) return 1;             /* no list: everyone, as before */
  for (ap = dp->allow; ap; ap = ap->next)
    if ((uint32) addr >= (uint32) ap->lo && (uint32) addr <= (uint32) ap->hi)
      return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static void tcp_send(void *arg)
{
  struct tcb *tcb = (struct tcb *) arg;

  int cnt;
  struct mbuf *bp;

  if ((cnt = space_tcp(tcb)) <= 0) {
    off_read(tcb->user);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = read(tcb->user, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(&bp);
    off_read(tcb->user);
    close_tcp(tcb);
    return;
  }
  bp->cnt = cnt;
  send_tcp(tcb, &bp);
}

/*---------------------------------------------------------------------------*/

static void tcp_receive(struct tcb *tcb, int32 cnt)
{

  char buffer[1024];
  struct mbuf *bp;

  if (tcb->user > 0) {
    recv_tcp(tcb, &bp, 0);
    while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
      if (write(tcb->user, buffer, (unsigned) cnt) != cnt) {
	free_p(&bp);
	close_tcp(tcb);
	return;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void tcp_ready(struct tcb *tcb, int32 cnt)
{
  if (tcb->user > 0) on_read(tcb->user, tcp_send, tcb);
}

/*---------------------------------------------------------------------------*/

static void tcp_state(struct tcb *tcb, enum tcp_state old, enum tcp_state new)
{

  int addrlen;
  struct dest *dp;
  struct sockaddr *addr = 0;

  switch (new) {
  case TCP_ESTABLISHED:
    logmsg(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    for (dp = dests; dp && dp->port != tcb->conn.local.port; dp = dp->next) ;
    if (dp && !allowed(dp, tcb->conn.remote.address)) {
      logmsg(tcb, "refused %s", tcp_port_name(tcb->conn.local.port));
      close_tcp(tcb);
      return;
    }
    if (!dp ||
	!(addr = build_sockaddr(dp->name, &addrlen)) ||
	(tcb->user = socket(addr->sa_family, SOCK_STREAM, 0)) <= 0 ||
	connect(tcb->user, addr, addrlen)) {
      close_tcp(tcb);
      return;
    }
    on_read(tcb->user, tcp_send, tcb);
    return;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    return;
  case TCP_CLOSED:
    if (tcb->user > 0) {
      logmsg(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
      off_read(tcb->user);
      close(tcb->user);
    }
    del_tcp(&tcb);
    break;
  default:
    break;
  }
}

/*---------------------------------------------------------------------------*/

int tcpgate1(int argc, char *argv[], void *p)
{

  char *name;
  char buf[80];
  int i;
  struct allow *ap;
  struct dest *dp;
  struct socket lsocket;

  lsocket.address = INADDR_ANY;
  lsocket.port = tcp_port_number(argv[1]);
  if (argc < 3)
    snprintf(name = buf, sizeof(buf), "loopback:%d", lsocket.port);
  else
    name = argv[2];
  for (dp = dests; dp && dp->port != lsocket.port; dp = dp->next) ;
  if (!dp) {
    dp = (struct dest *) callocw(1, sizeof(struct dest));
    dp->port = lsocket.port;
    dp->next = dests;
    dests = dp;
  }
  if (dp->name) free(dp->name);
  if (!(dp->name = strdup(name))) return 1;

  /* Any further arguments are the addresses allowed to use this gate */
  while ((ap = dp->allow)) {
    dp->allow = ap->next;
    free(ap);
  }
  for (i = 3; i < argc; i++) {
    int32 lo, hi;

    if (parse_allow(argv[i], &lo, &hi)) {
      printf("tcpgate: cannot parse \"%s\"\n", argv[i]);
      return 1;
    }
    ap = (struct allow *) callocw(1, sizeof(struct allow));
    ap->lo = lo;
    ap->hi = hi;
    ap->next = dp->allow;
    dp->allow = ap;
  }
  open_tcp(&lsocket, NULL, TCP_SERVER, 0, tcp_receive, tcp_ready, tcp_state, 0, 0);
  return 0;
}
