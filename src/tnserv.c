#include <stdio.h>
#include <termio.h>

#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "timer.h"
#include "tcp.h"
#include "telnet.h"
#include "login.h"

static struct tcb *tcb_server;

/*---------------------------------------------------------------------------*/

static void tnserv_poll(tcb)
struct tcb *tcb;
{

  char  *p;
  int  c;
  int  cnt;
  struct login_cb *tp;
  struct mbuf *bp;

  tp = (struct login_cb *) tcb->user;
  start_timer(&tp->poll_timer);
  if ((cnt = space_tcp(tcb)) <= 0) return;
  if (!(bp = alloc_mbuf(cnt + 1))) return;
  p = bp->data;
  while (p - bp->data < cnt && (c = login_read(tp)) != -1) {
    *p++ = c;
    if (c == IAC) *p++ = IAC;
  }
  if (bp->cnt = p - bp->data)
    send_tcp(tcb, bp);
  else {
    free_p(bp);
    if (login_dead(tp)) {
      stop_timer(&tp->poll_timer);
      close_tcp(tcb);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void tnserv_recv_upcall(tcb, cnt)
struct tcb *tcb;
int16 cnt;
{

  char  c;
  int  ci;
  struct login_cb *tp;
  struct mbuf *bp;
  struct termio termio;

  tp = (struct login_cb *) tcb->user;
  recv_tcp(tcb, &bp, cnt);
  while (pullup(&bp, &c, 1) == 1) {
    ci = uchar(c);
    switch (tp->state) {
    case TS_DATA:
      if (ci == IAC)
	tp->state = TS_IAC;
      else {
	/***  if (!tp->option[TN_TRANSMIT_BINARY]) c &= 0x7f;   ***/
	login_write(tp, c);
      }
      break;
    case TS_IAC:
      switch (ci) {
      case WILL:
	tp->state = TS_WILL;
	break;
      case WONT:
	tp->state = TS_WONT;
	break;
      case DO:
	tp->state = TS_DO;
	break;
      case DONT:
	tp->state = TS_DONT;
	break;
      case IAC:
	login_write(tp, c);
	tp->state = TS_DATA;
	break;
      default:
	tp->state = TS_DATA;
	break;
      }
      break;
    case TS_WILL:
      tp->state = TS_DATA;
      break;
    case TS_WONT:
      tp->state = TS_DATA;
      break;
    case TS_DO:
      if (ci <= NOPTIONS) tp->option[ci] = 1;
      if (ci == TN_ECHO) {
	ioctl(tp->pty, TCGETA, &termio);
	termio.c_lflag |= (ECHO | ECHOE | ECHOK);
	ioctl(tp->pty, TCSETA, &termio);
      }
      tp->state = TS_DATA;
      break;
    case TS_DONT:
      if (ci <= NOPTIONS) tp->option[ci] = 0;
      if (ci == TN_ECHO) {
	ioctl(tp->pty, TCGETA, &termio);
	termio.c_lflag &= ~(ECHO | ECHOE | ECHOK);
	ioctl(tp->pty, TCSETA, &termio);
      }
      tp->state = TS_DATA;
      break;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void tnserv_state_upcall(tcb, old, new)
struct tcb *tcb;
char  old, new;
{
  struct login_cb *tp;

  switch (new) {
#ifdef  QUICKSTART
  case SYN_RECEIVED:
#else
  case ESTABLISHED:
#endif
    tcb->user = (char *) login_open(psocket(&tcb->conn.remote), "TELNET", tnserv_poll, (char *) tcb);
    if (!tcb->user)
      close_tcp(tcb);
    else
      log(tcb, "open TELNET");
    break;
  case CLOSE_WAIT:
    tp = (struct login_cb *) tcb->user;
    if (tp = (struct login_cb *) tcb->user) stop_timer(&tp->poll_timer);
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
  tcb_server = open_tcp(&lsocket, NULLSOCK, TCP_SERVER, 0, tnserv_recv_upcall, NULLVFP, tnserv_state_upcall, 0, NULLCHAR);
}

