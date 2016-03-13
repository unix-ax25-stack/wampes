/* @(#) $Id: tnserv.c,v 1.18 2016/03/13 07:14:39 dl9sau Exp $ */

#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "netuser.h"
#include "tcp.h"
#include "login.h"
#include "tipmail.h"

static struct tcb *tcb_server;

/*---------------------------------------------------------------------------*/

static void tnserv_recv_upcall(struct tcb *tcb, int32 cnt)
{
  struct mbuf *bp;

  if (tcb->user) {
    bp = 0;
    recv_tcp(tcb, &bp, 0);
    login_write((struct login_cb *) tcb->user, &bp);
  }
}

/*---------------------------------------------------------------------------*/

static void tnserv_send_upcall(struct tcb *tcb, int32 cnt)
{
  struct mbuf *bp;

  if (tcb->user &&
      (bp = login_read((struct login_cb *) tcb->user, space_tcp(tcb))))
    send_tcp(tcb, &bp);
}

/*---------------------------------------------------------------------------*/

static void tnserv_state_upcall(struct tcb *tcb, enum tcp_state old, enum tcp_state new)
{
  switch (new) {
  case TCP_ESTABLISHED:
    tcb->user = (long) login_open(pinet_tcp(&tcb->conn.remote), "TELNET", (void (*)(void *)) tnserv_send_upcall, (void (*)(void *)) close_tcp, tcb);
    if (!tcb->user)
      close_tcp(tcb);
    else
      logmsg(tcb, "open TELNET", "");
    break;
  case TCP_CLOSE_WAIT:
    close_tcp(tcb);
    break;
  case TCP_CLOSED:
    if (tcb->user) {
      login_close((struct login_cb *) tcb->user);
      logmsg(tcb, "close TELNET", "");
    }
    del_tcp(&tcb);
    if (tcb == tcb_server) tcb_server = NULL;
    break;
  default:
    break;
  }
}

/*---------------------------------------------------------------------------*/

int telnet0(int argc, char *argv[], void *p)
{
  if (tcb_server) close_tcp(tcb_server);
  return 0;
}

/*---------------------------------------------------------------------------*/

int telnet1(int argc, char *argv[], void *p)
{
  struct socket lsocket;

  if (tcb_server) close_tcp(tcb_server);
  lsocket.address = INADDR_ANY;
  lsocket.port = (argc < 2) ? IPPORT_TELNET : tcp_port_number(argv[1]);
  tcb_server = open_tcp(&lsocket, NULL, TCP_SERVER, 0, tnserv_recv_upcall,
			tnserv_send_upcall, tnserv_state_upcall, 0, 0);
  return 0;
}
