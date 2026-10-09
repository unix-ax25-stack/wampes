/* @(#) $Id: ax25file.c,v 1.20 2002/01/12 15:55:53 dl9sau Exp $ */

#include <stdio.h>

#include "global.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "main.h"

#ifdef	AX25_VJCOMP
#define AXROUTE_FILE_VERSION    2
#else
#define AXROUTE_FILE_VERSION    1
#endif
#define AXROUTE_HOLDTIME        (0x7fffffff / 1000)
#define AXROUTE_SAVETIME        (10L*60L*1000L)

struct axroute_saverecord_0 {
  uint8 call[AXALEN];
  char pad1;
  uint8 digi[AXALEN];
  char pad2;
  short dev;
  long time;
};

struct axroute_saverecord_1 {
  uint8 call[AXALEN];
  uint8 digi[AXALEN];
  long time;
/*char ifname[]; */
};

#ifdef	AX25_VJCOMP
struct axroute_saverecord_2 {
  uint8 call[AXALEN];
  uint8 digi[AXALEN];
  long time;
  int vjcomp;
};
#endif

/* The route file lives in the node's directory like everything else that
 * outlives the process - see Node_dir in main.c.  The names are built once,
 * on first use, because a file-scope initializer would put TCPDIR back in.
 */
static char axroute_filename[512];
static char axroute_tmpfilename[512];

static void
axroute_paths(void)
{
	if(axroute_filename[0] == '\0'){
		nodedir_path(axroute_filename, sizeof(axroute_filename),
			     "axroute_data");
		nodedir_path(axroute_tmpfilename, sizeof(axroute_tmpfilename),
			     "axroute_tmp");
	}
}

/*---------------------------------------------------------------------------*/

/* The writing half, on its own so that "ax25 route del" can have the file on
 * disk at once.  A deleted route that is still in the file comes back at the
 * next start, and an operator who deleted it has a reason.
 */
static void
axroute_write(void)
{

  FILE *fp;
  int i;
#ifdef	AX25_VJCOMP
  struct axroute_saverecord_2 buf;
#else
  struct axroute_saverecord_1 buf;
#endif
  struct ax_route *rp, *lp;

  axroute_paths();
  if (!(fp = fopen(axroute_tmpfilename, "w"))) return;
  putc(AXROUTE_FILE_VERSION, fp);
  for (i = 0; i < AXROUTESIZE; i++)
    for (lp = 0, rp = Ax_routes[i]; rp; )
      if (rp->perm || rp->jumpstart ||
	  rp->time + AXROUTE_HOLDTIME >= secclock()) {
	memset(&buf, 0, sizeof(buf));
	addrcp(buf.call, rp->target);
	if (rp->digi)
	  addrcp(buf.digi, rp->digi->target);
	buf.time = rp->time;
#ifdef	AX25_VJCOMP
	buf.vjcomp = rp->vjcomp;
#endif
	fwrite((char *) &buf, sizeof(buf), 1, fp);
	if (rp->ifp)
	  fwrite(rp->ifp->name, strlen(rp->ifp->name) + 1, 1, fp);
	else
	  putc(0, fp);
	lp = rp;
	rp = rp->next;
      } else if (lp) {
	/* alter Eintrag: geht nicht mit in die Datei.  Vor dem Frei auch
	 * die Zeiger abloesen, die andere Wege noch auf ihn halten -
	 * axroute_unref() in ax25.c, dieselbe Gefahr wie beim Loeschen mit
	 * der Hand.  "instead" ist hier absichtlich NULL: der naechste
	 * Sprung kann in demselben Durchgang alt sein.
	 */
	axroute_unref(rp, 0);
	lp->next = rp->next;
	free(rp);
	rp = lp->next;
      } else {
	axroute_unref(rp, 0);
	Ax_routes[i] = rp->next;
	free(rp);
	rp = Ax_routes[i];
      }
  fclose(fp);
  rename(axroute_tmpfilename, axroute_filename);
}

void axroute_savefile(void *arg)
{

  static struct timer timer;

  if (Debug) return;
  if (!main_exit) {
    switch (timer.state) {
    case TIMER_STOP:
      timer.func = axroute_savefile;
      timer.arg = 0;
      set_timer(&timer, AXROUTE_SAVETIME);
      start_timer(&timer);
      return;
    case TIMER_RUN:
      return;
    case TIMER_EXPIRE:
      timer.state = TIMER_STOP;
      break;
    }
  } else {
    if (timer.state != TIMER_RUN) return;
  }
  axroute_write();
}

/* The same write, now, without touching the timer.  Used by "ax25 route del"
 * and by "ax25 route add" for a learned entry, so that what the operator was
 * just told is also what the next start will find.
 */
void axroute_flush(void)
{
  axroute_write();
}

/*---------------------------------------------------------------------------*/

/*---------------------------------------------------------------------------*/

/* The port a route was learned on, as the file names it, to the port this node
 * actually has.  An empty name means "no port" and is not a loss.
 *
 * A name that does not resolve is worth a line on the console.  Two reasons,
 * and an operator can only tell them apart by being told both: the port is
 * gone, or this config file came up without the "attach" that makes it.
 * Either way the caller drops the entry - a route that names a port which is
 * not there would fall back to Axroute_default_ifp, and the fallback port is
 * the one thing it must not do without being asked.
 */
static struct iface *
axroute_iflookup(const char *ifname, const uint8 *call)
{
  struct iface *ifp;
  char callname[64];

  if (*ifname == '\0') return NULL;
  for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
  if (ifp == NULL) {
    pax25(callname, (uint8 *) call);
    printf("ax25 route: %s was learned on port %s, which this node does"
	   " not have - dropped.  Attach the port, or add the route again.\n",
	   callname, ifname);
  }
  return ifp;
}

/* Dasselbe fuer das alte Format 0, das statt eines NAMENS eine
 * GERAETENUMMER nennt.  Die Nummer loest genauso wenig auf wie ein Name, und
 * das Ergebnis muss dasselbe sein: der Eintrag faellt weg, und der Betreiber
 * hoert, welcher Port fehlt.
 *
 * Vorher stand hier nur
 *
 *     if (buf.dev >= 0 && buf.dev < 128) rp->ifp = ifptable[buf.dev];
 *
 * und ifptable[buf.dev] ist NULL, wenn es diese Nummer nicht gibt.  Der
 * Eintrag stand danach trotzdem in der Tabelle - ohne Port, und mit dem
 * Digipeater aus demselben Satz in der Kette.  Beim Senden nimmt axroute()
 * dann den Digipeater in den Kopf, das Port bleibt leer, und es landet auf
 * Axroute_default_ifp: der FANG schickt einen Frame hinaus, fuer den niemand
 * einen Digipeater bestellt hat.  Genau das ist im Labor passiert, und die
 * Kette laesst sich nur an ihrem Ende erkennen - der Eintrag selbst sah
 * vollstaendig normal aus.
 */
static struct iface *
axroute_devlookup(const uint8 *call, short dev,
		  struct iface *const ifptable[])
{
  struct iface *ifp;
  char callname[64];

  if (dev < 0 || dev >= 128) return NULL;
  if ((ifp = ifptable[dev]) != NULL) return ifp;
  pax25(callname, (uint8 *) call);
  printf("ax25 route: %s was learned on device %d, which this node does"
	 " not have - dropped.  Attach the port, or add the route again.\n",
	 callname, (int) dev);
  return NULL;
}

void axroute_loadfile(void)
{

  FILE *fp;
  int version;
  static int done;

  if (done) return;
  done = 1;
  axroute_paths();
  if (Debug || !(fp = fopen(axroute_filename, "r"))) return;

  switch (version = getc(fp)) {

  case EOF:
    break;

  default:
    {

      struct ax_route *rp;
      struct axroute_saverecord_0 buf;
      struct iface *ifp, *ifptable[128];

      ungetc(version, fp);
      memset(ifptable, 0, sizeof(ifptable));
      for (ifp = Ifaces; ifp; ifp = ifp->next)
	if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_remote_call(buf.call)) continue;
	/* DER PORT ZUERST.  Ein Eintrag, dessen Port es nicht gibt, wird
	 * verworfen und gemeldet - so wie es das aktuelle Format tut.  Ein
	 * Eintrag, der nur so tut als gaebe es den Port, waere der schlimmere
	 * Fehler: er waere nicht zu sehen und haette dennoch Wirkung. */
	if ((ifp = axroute_devlookup(buf.call, buf.dev, ifptable)) == NULL)
	  continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_remote_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
	rp->ifp = ifp;
	rp->time = buf.time;
      }
    }

  case 1:
    {

      char *cp;
      char ifname[1024];
      int c;
      struct ax_route *rp;
      struct axroute_saverecord_1 buf;
      struct iface *ifp;

      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
	cp = ifname;
	do {
	  if ((c = getc(fp)) == EOF) {
	    fclose(fp);
	    return;
	  }
	} 	while ((*cp++ = c));
	if (*ifname) {
	  if ((ifp = axroute_iflookup(ifname, buf.call)) == NULL)
	    continue;      /* named a port this node does not have */
	} else
	  ifp = 0;
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_remote_call(buf.call)) continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_remote_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
	rp->ifp = ifp;
	rp->time = buf.time;
      }
    }
#ifdef	AX25_VJCOMP
  case 2:
    {

      char *cp;
      char ifname[1024];
      int c;
      struct ax_route *rp;
      struct axroute_saverecord_2 buf;
      struct iface *ifp;

      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
        cp = ifname;
        do {
          if ((c = getc(fp)) == EOF) {
            fclose(fp);
            return;
          }
        } while ((*cp++ = c));
        if (*ifname) {
          if ((ifp = axroute_iflookup(ifname, buf.call)) == NULL)
            continue;    /* named a port this node does not have */
        } else
          ifp = 0;
        if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
        if (!valid_remote_call(buf.call)) continue;
        rp = ax_routeptr(buf.call, 1);
        if (valid_remote_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
        rp->ifp = ifp;
        rp->time = buf.time;
        rp->vjcomp = buf.vjcomp;
      }
    }
#endif

  }

  fclose(fp);
}
