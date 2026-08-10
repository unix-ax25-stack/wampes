/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "login.h"

int Axserver_enabled;

/*---------------------------------------------------------------------------*/

// dl9sau: compatibilty feature for IP.VC with xnet hosts
// incoming ax25 PID=Text droper
void axserv_recv_upcall_discard(struct ax25_cb *axp, int cnt)
{
  struct mbuf *bp;

  bp = recv_ax25(axp, 0);
  free_p(&bp);
}

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

static void axserv_send_login_upcall(void *arg)
{
  axserv_send_upcall((struct ax25_cb *) arg, 0);
}

static void axserv_close_upcall(void *arg)
{
  disc_ax25((struct ax25_cb *) arg);
}

void axserv_open(struct ax25_cb *axp, int cnt)
{
  char callsign[AXBUF];

  if (Axserver_enabled) {
    pax25(callsign, axp->hdr.dest);
    axp->user = (char *) login_open(callsign, "AX25", axserv_send_login_upcall, axserv_close_upcall, axp);
  }
  if (axp->user) {
    free_q(&axp->rxq);
    axp->r_upcall = axserv_recv_upcall;
    axp->t_upcall = axserv_send_upcall;
    axp->s_upcall = axserv_state_upcall;
  } else
#ifdef	notdef
    disc_ax25(axp);
#else
    // dl9sau: don't disconnect. the client may deceide to do it. this
    // keeps the session open for transports with other PIDs
    axp->r_upcall = axserv_recv_upcall_discard;
#endif
    
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
