/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axserver.c,v 1.15 1996/08/11 18:16:09 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "login.h"

int Axserver_enabled;

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  bp = recv_ax25(axp, 0);
  login_write((struct login_cb *) axp->user, &bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  if ((bp = login_read((struct login_cb *) axp->user, space_ax25(axp))))
    send_ax25(axp, &bp, PID_NO_L3);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(struct ax25_cb *axp, enum lapb_state oldstate, enum lapb_state newstate)
{
  if (newstate == LAPB_DISCONNECTED) {
    login_close((struct login_cb *) axp->user);
    del_ax25(axp);
  }
}

/*---------------------------------------------------------------------------*/

void axserv_open(struct ax25_cb *axp, int cnt)
{
  char callsign[AXBUF];

  if (Axserver_enabled) {
    pax25(callsign, axp->hdr.dest);
    axp->user = (char *) login_open(callsign, "AX25", (void (*)(void *)) axserv_send_upcall, (void (*)(void *)) disc_ax25, axp);
  }
  if (axp->user) {
    free_q(&axp->rxq);
    axp->r_upcall = axserv_recv_upcall;
    axp->t_upcall = axserv_send_upcall;
    axp->s_upcall = axserv_state_upcall;
  } else
    disc_ax25(axp);
}

/*---------------------------------------------------------------------------*/

int ax25start(int argc, char *argv[], void *p)
{
  Axserver_enabled = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

int ax250(int argc, char *argv[], void *p)
{
  Axserver_enabled = 0;
  return 0;
}
