/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpgate.c,v 1.7 1991/04/25 18:27:38 deyke Exp $ */

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "tcp.h"
#include "hpux.h"
#include "buildsaddr.h"

struct dest {
  int  port;
  char  *name;
  struct dest *next;
};

static struct dest *dests;

/*---------------------------------------------------------------------------*/

static void tcp_send(tcb)
struct tcb *tcb;
{

  int  cnt, fd;
  struct mbuf *bp;

  fd = tcb->user;
  if ((cnt = space_tcp(tcb)) <= 0) {
    clrmask(chkread, fd);
    return;
  }
  if (!(bp = alloc_mbuf(cnt))) return;
  cnt = doread(fd, bp->data, (unsigned) cnt);
  if (cnt <= 0) {
    free_p(bp);
    clrmask(chkread, fd);
    close_tcp(tcb);
    return;
  }
  bp->cnt = cnt;
  send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tcp_receive(tcb, cnt)
struct tcb *tcb;
int  cnt;
{

  char  buffer[1024];
  int  fd;
  struct mbuf *bp;

  if ((fd = tcb->user) > 0) {
    recv_tcp(tcb, &bp, 0);
    while ((cnt = pullup(&bp, buffer, sizeof(buffer))) > 0)
      if (dowrite(fd, buffer, (unsigned) cnt) < 0) {
	free_p(bp);
	close_tcp(tcb);
	return;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void tcp_ready(tcb, cnt)
struct tcb *tcb;
int  cnt;
{
  if (tcb->user > 0) setmask(chkread, tcb->user);
}

/*---------------------------------------------------------------------------*/

static void tcp_state(tcb, old, new)
struct tcb *tcb;
int  old, new;
{

  int  addrlen, fd;
  struct dest *dp;
  struct sockaddr *addr;

  switch (new) {
#ifdef  QUICKSTART
  case TCP_SYN_RECEIVED:
#else
  case TCP_ESTABLISHED:
#endif
    log(tcb, "open %s", tcp_port_name(tcb->conn.local.port));
    for (dp = dests; dp && dp->port != tcb->conn.local.port; dp = dp->next) ;
    if (!dp ||
	!(addr = build_sockaddr(dp->name, &addrlen)) ||
	(fd = tcb->user = socket(addr->sa_family, SOCK_STREAM, 0)) <= 0 ||
	connect(fd, addr, addrlen)) {
      close_tcp(tcb);
      return;
    }
    readfnc[fd] = (void (*)()) tcp_send;
    readarg[fd] = tcb;
    setmask(chkread, fd);
    return;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    return;
  case TCP_CLOSED:
    if ((fd = tcb->user) > 0) {
      log(tcb, "close %s", tcp_port_name(tcb->conn.local.port));
      clrmask(chkread, fd);
      readfnc[fd] = 0;
      readarg[fd] = 0;
      close(fd);
    }
    del_tcp(tcb);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int  tcpgate1(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{

  char  *name;
  char  buf[80];
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

