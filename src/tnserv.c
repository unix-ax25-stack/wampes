/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tnserv.c,v 1.2 1990/01/29 09:37:28 deyke Exp $ */

#include "global.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "login.h"

static struct tcb *tcb_server;

/*---------------------------------------------------------------------------*/

static void tnserv_recv_upcall(tcb)
struct tcb *tcb;
{
  struct mbuf *bp;

  recv_tcp(tcb, &bp, 0);
  login_write((struct login_cb *) tcb->user, bp);
}

/*---------------------------------------------------------------------------*/

static void tnserv_send_upcall(tcb)
struct tcb *tcb;
{
  struct mbuf *bp;

  if (bp = login_read((struct login_cb *) tcb->user, space_tcp(tcb)))
    send_tcp(tcb, bp);
}

/*---------------------------------------------------------------------------*/

static void tnserv_state_upcall(tcb, old, new)
struct tcb *tcb;
char  old, new;
{
  switch (new) {
#ifdef  QUICKSTART
  case SYN_RECEIVED:
#else
  case ESTABLISHED:
#endif
    tcb->user = (char *) login_open(psocket(&tcb->conn.remote), "TELNET", tnserv_send_upcall, close_tcp, (char *) tcb);
    if (!tcb->user)
      close_tcp(tcb);
    else
      log(tcb, "open TELNET");
    break;
  case CLOSE_WAIT:
    close_tcp(tcb);
    break;
  case CLOSED:
    if (old != LISTEN) {
      login_close((struct login_cb *) tcb->user);
      log(tcb, "close TELNET");
    }
    del_tcp(tcb);
    if (tcb == tcb_server) tcb_server = NULLTCB;
    break;
  }
}

/*---------------------------------------------------------------------------*/

tn0()
{
  if (tcb_server) close_tcp(tcb_server);
}

/*---------------------------------------------------------------------------*/

tn1(argc, argv)
int  argc;
char  *argv[];
{
  struct socket lsocket;

  if (tcb_server) close_tcp(tcb_server);
  lsocket.address = ip_addr;
  lsocket.port = (argc < 2) ? TELNET_PORT : tcp_portnum(argv[1]);
  tcb_server = open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tnserv_recv_upcall, tnserv_send_upcall, tnserv_state_upcall, 0, NULLCHAR);
}

