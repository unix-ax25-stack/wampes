/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axserver.c,v 1.8 1993/01/29 06:48:17 deyke Exp $ */

#include <stdlib.h>

#include "global.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include "login.h"

static void axserv_recv_upcall __ARGS((struct ax25_cb *cp, int cnt));
static void axserv_send_upcall __ARGS((struct ax25_cb *cp, int cnt));
static void axserv_state_upcall __ARGS((struct ax25_cb *cp, int oldstate, int newstate));

/*---------------------------------------------------------------------------*/

static void axserv_recv_upcall(cp, cnt)
struct ax25_cb *cp;
int cnt;
{
  struct mbuf *bp;

  bp = recv_ax25(cp, 0);
  if (bp) login_write((struct login_cb *) cp->user, bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_send_upcall(cp, cnt)
struct ax25_cb *cp;
int cnt;
{
  struct mbuf *bp;

  if (bp = login_read((struct login_cb *) cp->user, space_ax25(cp)))
    send_ax25(cp, bp);
}

/*---------------------------------------------------------------------------*/

static void axserv_state_upcall(cp, oldstate, newstate)
struct ax25_cb *cp;
int oldstate, newstate;
{
  char callsign[AXBUF];

  switch (newstate) {
  case LAPB_CONNECTED:
    pax25(callsign, cp->hdr.dest);
    cp->user = (char *) login_open(callsign, "AX25", (void (*)()) axserv_send_upcall, (void (*)()) disc_ax25, cp);
    if (!cp->user) disc_ax25(cp);
    break;
  case LAPB_DISCONNECTED:
    login_close((struct login_cb *) cp->user);
    del_ax25(cp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

int ax250(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  if (Axcb_server) del_ax25(Axcb_server);
  return 0;
}

/*---------------------------------------------------------------------------*/

int ax25start(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  if (!Axcb_server)
    Axcb_server = open_ax25(0, AX_SERVER, axserv_recv_upcall,
			    axserv_send_upcall, axserv_state_upcall, NULLCHAR);
  return Axcb_server ? 0 : -1;
}

