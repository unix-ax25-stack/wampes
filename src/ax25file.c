/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25file.c,v 1.2 1991/03/28 19:39:10 deyke Exp $ */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "asy.h"

extern int  debug;

#define AXROUTEHOLDTIME (60l*60*24*90)
#define AXROUTESAVETIME (60l*10)

struct axroutesaverecord {
  char  call[AXALEN];
  char  pad1;
  char  digi[AXALEN];
  char  pad2;
  short  dev;
  long  time;
};

static char  axroutefile[] = "/tcp/axroute_data";
static char  axroutetmpfile[] = "/tcp/axroute_tmp";

/*---------------------------------------------------------------------------*/

void axroute_savefile()
{

  FILE * fp;
  int  i;
  static long  nextsavetime;
  struct ax_route *rp, *lp;
  struct axroutesaverecord buf;

  if (!nextsavetime) nextsavetime = secclock() + AXROUTESAVETIME;
  if (debug || nextsavetime > secclock()) return;
  nextsavetime = secclock() + AXROUTESAVETIME;
  if (!(fp = fopen(axroutetmpfile, "w"))) return;
  for (i = 0; i < AXROUTESIZE; i++)
    for (lp = 0, rp = Ax_routes[i]; rp; )
      if (rp->time + AXROUTEHOLDTIME >= secclock()) {
	addrcp(buf.call, rp->call);
	if (rp->digi)
	  addrcp(buf.digi, rp->digi->call);
	else
	  *buf.digi = '\0';
	buf.dev = rp->ifp ? rp->ifp->dev : -1;
	buf.time = rp->time;
	fwrite((char *) &buf, sizeof(buf), 1, fp);
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
  rename(axroutetmpfile, axroutefile);
}

/*---------------------------------------------------------------------------*/

void axroute_loadfile()
{

  FILE * fp;
  char  **mpp;
  static int  done;
  struct ax_route *rp;
  struct axroutesaverecord buf;
  struct iface *ifp, *ifptable[ASY_MAX];

  if (done) return;
  done = 1;
  if (debug || !(fp = fopen(axroutefile, "r"))) return;
  memset(ifptable, 0, sizeof(ifptable));
  for (ifp = Ifaces; ifp; ifp = ifp->next)
    if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
  while (fread((char *) &buf, sizeof(buf), 1, fp)) {
    if (buf.time + AXROUTEHOLDTIME < secclock()) goto next;
    if (!*buf.call || ismycall(buf.call)) goto next;
    for (mpp = Axmulti; *mpp; mpp++)
      if (addreq(buf.call, *mpp)) goto next;
    rp = ax_routeptr(buf.call, 1);
    if (*buf.digi) {
      if (ismycall(buf.digi)) goto next;
      for (mpp = Axmulti; *mpp; mpp++)
	if (addreq(buf.digi, *mpp)) goto next;
      rp->digi = ax_routeptr(buf.digi, 1);
    }
    if (buf.dev >= 0 && buf.dev < ASY_MAX) rp->ifp = ifptable[buf.dev];
    rp->time = buf.time;
next:
    ;
  }
  fclose(fp);
}

