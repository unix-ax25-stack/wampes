/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpgate.c,v 1.10 1993/05/17 13:45:19 deyke Exp $ */

#include "global.h"

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "mbuf.h"
#include "netuser.h"
#include "tcp.h"
#include "hpux.h"
#include "buildsaddr.h"

struct dest {
  int port;
  char *name;
  struct dest *next;
};

static struct dest *dests;

/*---------------------------------------------------------------------------*/

static void tcp_send(struct tcb *tcb)
{

  int cnt;
  struct mbuf *bp;

  if ((cnt = space_tcp(tcb)) <= 0) {
    off_read(tcb->user);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = read(tcb->user, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(bp);
    off_read(tcb->user);
    close_tcp(tcb);
    return;
  }
  bp->cnt = cnt;
  send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tcp_receive(struct tcb *tcb, int cnt)
{

  char buffer[1024];
  struct mbuf *bp;

  if (tcb->user > 0) {
    recv_tcp(tcb, &bp, 0);
    while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
      if (write(tcb->user, buffer, (unsigned) cnt) != cnt) {
	free_p(bp);
	close_tcp(tcb);
	return;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void tcp_ready(struct tcb *tcb, int cnt)
{
  if (tcb->user > 0) on_read(tcb->user, (void (*)()) tcp_send, tcb);
}

/*---------------------------------------------------------------------------*/

static void tcp_state(struct tcb *tcb, int old, int new)
{

  int addrlen;
  struct dest *dp;
  struct sockaddr *addr;

  switch (new) {
#ifdef QUICKSTART
  case TCP_SYN_RECEIVED:
#else
  case TCP_ESTABLISHED:
#endif
    log(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    for (dp = dests; dp && dp->port != tcb->conn.local.port; dp = dp->next) ;
    if (!dp ||
	!(addr = build_sockaddr(dp->name, &addrlen)) ||
	(tcb->user = socket(addr->sa_family, SOCK_STREAM, 0)) <= 0 ||
	connect(tcb->user, addr, addrlen)) {
      close_tcp(tcb);
      return;
    }
    on_read(tcb->user, (void (*)()) tcp_send, tcb);
    return;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    return;
  case TCP_CLOSED:
    if (tcb->user > 0) {
      log(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
      off_read(tcb->user);
      close(tcb->user);
    }
    del_tcp(tcb);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int tcpgate1(int argc, char *argv[], void *p)
{

  char *name;
  char buf[80];
  struct dest *dp;
  struct socket lsocket;

  lsocket.address = INADDR_ANY;
  lsocket.port = tcp_port_number(argv[1]);
  if (argc < 3)
    sprintf(name = buf, "loopback:%d", lsocket.port);
  else
    name = argv[2];
  for (dp = dests; dp && dp->port != lsocket.port; dp = dp->next) ;
  if (!dp) {
    dp = malloc(sizeof(*dp));
    dp->port = lsocket.port;
    dp->name = 0;
    dp->next = dests;
    dests = dp;
  }
  if (dp->name) free(dp->name);
  dp->name = strdup(name);
  open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tcp_receive, tcp_ready, tcp_state, 0, 0);
  return 0;
}

