/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25file.c,v 1.6 1991/07/16 17:54:55 deyke Exp $ */

#include <stdio.h>
#include <string.h>

#include "global.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"

#define AXROUTE_FILE_VERSION    1
#define AXROUTE_HOLDTIME        (0x7fffffff / 1000)
#define AXROUTE_SAVETIME        (60L*10)

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

static char axroute_filename[] = "/tcp/axroute_data";
static char axroute_tmpfilename[] = "/tcp/axroute_tmp";

static int valid_call __ARGS((char *call));

/*---------------------------------------------------------------------------*/

void axroute_savefile()
{

  FILE * fp;
  int i;
  static long nextsavetime;
  struct ax_route *rp, *lp;
  struct axroute_saverecord_1 buf;

  if (!nextsavetime) nextsavetime = secclock() + AXROUTE_SAVETIME;
  if (Debug || nextsavetime > secclock()) return;
  nextsavetime = secclock() + AXROUTE_SAVETIME;
  if (!(fp = fopen(axroute_tmpfilename, "w"))) return;
  putc(AXROUTE_FILE_VERSION, fp);
  for (i = 0; i < AXROUTESIZE; i++)
    for (lp = 0, rp = Ax_routes[i]; rp; )
      if (rp->perm || rp->time + AXROUTE_HOLDTIME >= secclock()) {
	addrcp(buf.call, rp->call);
	if (rp->digi)
	  addrcp(buf.digi, rp->digi->call);
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

static int valid_call(call)
char *call;
{
  char **mpp;

  if (!*call || ismyax25addr(call)) return 0;
  for (mpp = Axmulti; *mpp; mpp++)
    if (addreq(call, *mpp)) return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

void axroute_loadfile()
{

  FILE * fp;
  int version;
  static int done;

  if (done) return;
  done = 1;
  if (Debug || !(fp = fopen(axroute_filename, "r"))) return;

  switch (version = getc(fp)) {

  default:
    {

      struct ax_route *rp;
      struct axroute_saverecord_0 buf;
      struct iface *ifp, *ifptable[128];

      ungetc(version, fp);
      memset(ifptable, 0, sizeof(ifptable));
      for (ifp = Ifaces; ifp; ifp = ifp->next)
	if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
      while (fread((char *) & buf, sizeof(buf), 1, fp)) {
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_call(buf.call)) continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
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
	} while (*cp++ = c);
	if (*ifname)
	  for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
	else
	  ifp = 0;
	if (buf.time + AXROUTE_HOLDTIME < secclock()) continue;
	if (!valid_call(buf.call)) continue;
	rp = ax_routeptr(buf.call, 1);
	if (valid_call(buf.digi)) rp->digi = ax_routeptr(buf.digi, 1);
	rp->ifp = ifp;
	rp->time = buf.time;
      }
    }

  }

  fclose(fp);
}

