/* @(#) $Id: axserver.c,v 1.19 2002/02/05 12:00:34 dl9sau Exp $ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "login.h"

int Axserver_enabled;

/* The callsigns this node answers to besides those of its interfaces, and
 * where a call to each is to be handed.
 *
 * A listener has to be configured.  The first frame of an incoming connection
 * arrives before there is any control block, so unlike a callsign one of our
 * own links is using, there is nothing to look it up in - see the note beside
 * link_answers_to() in ax25.c.
 *
 * Deliberately not tied to an interface.  An interface callsign belongs to a
 * port; this is a service of the node, and a caller may reach it over
 * whichever port they can.
 */

struct axlisten {
  struct axlisten *next;
  uint8 call[AXALEN];
  char *dest;                           /* where the session is handed */
};

static struct axlisten *Axlisten;

/*---------------------------------------------------------------------------*/

static struct axlisten *axlisten_find(const uint8 *call)
{
  struct axlisten *lp;

  for (lp = Axlisten; lp; lp = lp->next)
    if (addreq(lp->call, call)) return lp;
  return 0;
}

/*---------------------------------------------------------------------------*/

int axlisten_active(const uint8 *call)
{
  return axlisten_find(call) != 0;
}

/*---------------------------------------------------------------------------*/

/* ax25 listen                    show what we answer to
 * ax25 listen <call> <dest>      answer to <call>, hand the session to <dest>
 * ax25 listen <call> off         stop answering to it
 */

int doaxlisten(int argc, char *argv[], void *p)
{

  char buf[AXBUF];
  struct axlisten *lp, **pp;
  uint8 call[AXALEN];

  if (argc < 2) {
    if (!Axlisten) {
      printf("Not listening for any callsign\n");
      return 0;
    }
    printf("Call       Handed to\n");
    for (lp = Axlisten; lp; lp = lp->next)
      printf("%-9s  %s\n", pax25(buf, lp->call), lp->dest);
    return 0;
  }

  if (setcall(call, argv[1])) {
    printf("Invalid call \"%s\"\n", argv[1]);
    return 1;
  }

  if (argc < 3) {
    if ((lp = axlisten_find(call)))
      printf("%-9s  %s\n", pax25(buf, lp->call), lp->dest);
    else
      printf("Not listening for %s\n", argv[1]);
    return 0;
  }

  if (!strcmp(argv[2], "off")) {
    for (pp = &Axlisten; *pp; pp = &(*pp)->next)
      if (addreq((*pp)->call, call)) {
        lp = *pp;
        *pp = lp->next;
        free(lp->dest);
        free(lp);
        return 0;
      }
    printf("Not listening for %s\n", argv[1]);
    return 1;
  }

  /* One of our own interface callsigns would be answered anyway, and by the
   * login server rather than by this - say so instead of pretending.
   */
  if (ismyax25addr(call)) {
    printf("%s is an interface callsign already\n", argv[1]);
    return 1;
  }

  if (!(lp = axlisten_find(call))) {
    if (!(lp = (struct axlisten *) calloc(1, sizeof(struct axlisten)))) {
      printf("No memory\n");
      return 1;
    }
    addrcp(lp->call, call);
    lp->next = Axlisten;
    Axlisten = lp;
  } else
    free(lp->dest);
  lp->dest = strdup(argv[2]);
  return 0;
}

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
  struct axlisten *lp;

  /* A callsign we were told to listen for is not a login.  Until the handing
   * on is built, say so and let go rather than dropping the caller into a
   * shell they never asked for.
   */
  if ((lp = axlisten_find(axp->hdr.dest))) {
    struct mbuf *bp;
    char buf[120];

    sprintf(buf, "*** %s is listened for, but handing it to %s is not built "
	    "yet\r", pax25(callsign, axp->hdr.dest), lp->dest);
    bp = qdata(buf, strlen(buf));
    send_ax25(axp, &bp, PID_NO_L3);
    axp->flags.closed = 1;
    axp->r_upcall = axserv_recv_upcall_discard;
    disc_ax25(axp);
    return;
  }

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
