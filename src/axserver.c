#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "axproto.h"
#include "login.h"

extern void free();

/*---------------------------------------------------------------------------*/

static void axserv_poll(cp)
struct axcb *cp;
{

  char  *p;
  int  c;
  int  cnt;
  struct login_cb *tp;
  struct mbuf *bp;

  tp = (struct login_cb *) cp->user;
  start_timer(&tp->poll_timer);
  if ((cnt = space_ax(cp)) <= 0) return;
  if (!(bp = alloc_mbuf(cnt))) return;
  p = bp->data;
  while (p - bp->data < cnt && (c = login_read(tp)) != -1)
    if (c != '\n') *p++ = c;
  if (bp->cnt = p - bp->data)
    send_ax(cp, bp);
  else {
    free_p(bp);
    if (login_dead(tp)) {
      stop_timer(&tp->poll_timer);
      close_ax(cp);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(cp, cnt)
struct axcb *cp;
int16 cnt;
{

  char  c;
  struct login_cb *tp;
  struct mbuf *bp;

  tp = (struct login_cb *) cp->user;
  recv_ax(cp, &bp, 0);
  while (pullup(&bp, &c, 1) == 1) login_write(tp, c);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(cp, oldstate, newstate)
struct axcb *cp;
int  oldstate, newstate;
{
  switch (newstate) {
  case CONNECTED:
    cp->user = (char *) login_open(pathtostr(cp), "AX25", axserv_poll, (char *) cp);
    if (!cp->user) close_ax(cp);
    break;
  case DISCONNECTED:
    login_close((struct login_cb *) cp->user);
    del_ax(cp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int axserv_stop()
{
  if (!axcb_server) return (-1);
  free((char *) axcb_server);
  axcb_server = NULLAXCB;
  return 0;
}

/*---------------------------------------------------------------------------*/

int axserv_start(argc, argv)
int  argc;
char  *argv[];
{
  axserv_stop();
  axcb_server = open_ax(NULLCHAR, AX25_SERVER, axserv_recv_upcall, NULLVFP, axserv_state_upcall, NULLCHAR);
  return axcb_server ? 0 : -1;
}

