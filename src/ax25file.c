/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25file.c,v 1.14 1994/10/06 16:15:20 deyke Exp $ */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"

#define AXROUTE_FILE_VERSION    1
#define AXROUTE_HOLDTIME        (0x7fffffff / 1000)
#define AXROUTE_SAVETIME        (10L*60L*1000L)

struct axroute_saverecord_0 {
  char call[AXALEN];
  char pad1;
  char digi[AXALEN];
  char pad2;
  short dev;
  long time;
};

struct axroute_saverecord_1 {
  char call[AXALEN];
  char digi[AXALEN];
  long time;
/*char ifname[]; */
};

static const char axroute_filename[] = "/tcp/axroute_data";
static const char axroute_tmpfilename[] = "/tcp/axroute_tmp";

/*---------------------------------------------------------------------------*/

void axroute_savefile(void)
{

  FILE * fp;
  int i;
  static struct timer timer;
  struct ax_route *rp, *lp;
  struct axroute_saverecord_1 buf;

  switch (timer.state) {
  case TIMER_STOP:
    if (!Debug) {
      timer.func = (void (*)(void *)) axroute_savefile;
      timer.arg = 0;
      set_timer(&timer, AXROUTE_SAVETIME);
      start_timer(&timer);
    }
    return;
  case TIMER_RUN:
    return;
  case TIMER_EXPIRE:
    timer.state = TIMER_STOP;
    break;
  }
  if (!(fp = fopen(axroute_tmpfilename, "w"))) return;
  putc(AXROUTE_FILE_VERSION, fp);
  for (i = 0; i < AXROUTESIZE; i++)
    for (lp = 0, rp = Ax_routes[i]; rp; )
      if (rp->perm || rp->jumpstart ||
	  rp->time + AXROUTE_HOLDTIME >= secclock()) {
	addrcp(buf.call, rp->target);
	if (rp->digi)
	  addrcp(buf.digi, rp->digi->target);
	else
	  *buf.digi = '\0';
	buf.time = rp->time;
	fwrite((char *) & buf, sizeof(buf), 1, fp);
	if (rp->ifp)
	  fwrite(rp->ifp->name, strlen(rp->ifp->name) + 1, 1, fp);
	else
	  putc(0, fp);
	lp = rp;
	rp = rp->next;
      } else if (lp) {
	lp->next = rp->next;
	free(rp);
	rp = lp->next;
      } else {
	Ax_routes[i] = rp->next;
	free(rp);
	rp = Ax_routes[i];
      }
  fclose(fp);
  rename(axroute_tmpfilename, axroute_filename);
}

/*---------------------------------------------------------------------------*/

void axroute_loadfile(void)
{

  FILE * fp;
  int version;
  static int done;

  if (done) return;
  done = 1;
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
      memset((char *) ifptable, 0, sizeof(ifptable));
      for (ifp = Ifaces; ifp; ifp = ifp->next)
	if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
      while (fread((char *) & buf, sizeof(buf), 1, fp)) {
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_remote_call(buf.call)) continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_remote_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
	if (buf.dev >= 0 && buf.dev < 128) rp->ifp = ifptable[buf.dev];
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

      while (fread((char *) & buf, sizeof(buf), 1, fp)) {
	cp = ifname;
	do {
	  if ((c = getc(fp)) == EOF) {
	    fclose(fp);
	    return;
	  }
	} while ((*cp++ = c));
	if (*ifname)
	  for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
	else
	  ifp = 0;
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_remote_call(buf.call)) continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_remote_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
	rp->ifp = ifp;
	rp->time = buf.time;
      }
    }

  }

  fclose(fp);
}

