/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipfile.c,v 1.4 1991/02/24 20:17:03 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "iface.h"
#include "ip.h"

extern int  debug;

#define ROUTE_FILE_VERSION   1
#define ROUTE_SAVETIME       (60*10)

struct route_saverecord {
  int32 target;         /* Target IP address */
  unsigned int  bits;   /* Number of significant bits */
  int32 gateway;        /* IP address of local gateway for this target */
  int32 metric;         /* Hop count or whatever */
  int  flags;
};

static char  route_filename[] = "/tcp/route_data";
static char  route_tmpfilename[] = "/tcp/route_tmp";

/*---------------------------------------------------------------------------*/

route_savefile()
{

  FILE * fp;
  register int  bits;
  register int  i;
  register struct route *p;
  static long  nextsavetime;
  struct route_saverecord buf;

  if (!nextsavetime) nextsavetime = secclock() + ROUTE_SAVETIME;
  if (debug || nextsavetime > secclock()) return;
  nextsavetime = secclock() + ROUTE_SAVETIME;
  if (!(fp = fopen(route_tmpfilename, "w"))) return;
  putc(ROUTE_FILE_VERSION, fp);
  for (bits = 1; bits <= 32; bits++)
    for (i = 0; i < HASHMOD; i++)
      for (p = Routes[bits-1][i]; p; p = p->next) {
	buf.target = p->target;
	buf.bits = p->bits;
	buf.gateway = p->gateway;
	buf.metric = p->metric;
	buf.flags = p->flags;
	fwrite((char *) &buf, sizeof(buf), 1, fp);
	fwrite(p->iface->name, strlen(p->iface->name) + 1, 1, fp);
      }
  fclose(fp);
  rename(route_tmpfilename, route_filename);
}

/*---------------------------------------------------------------------------*/

route_loadfile()
{

  FILE * fp;
  char  ifname[1024];
  register char  *cp;
  register int  c;
  register struct iface *ifp;
  static int  done;
  struct route_saverecord buf;

  if (done) return;
  done = 1;
  if (debug || !(fp = fopen(route_filename, "r"))) return;
  if (getc(fp) == ROUTE_FILE_VERSION)
    while (fread((char *) &buf, sizeof(buf), 1, fp)) {
      cp = ifname;
      do {
	if ((c = getc(fp)) == EOF) {
	  fclose(fp);
	  return;
	}
      } while (*cp++ = c);
      for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
      if (ifp)
	rt_add(buf.target, buf.bits, buf.gateway, ifp, buf.metric, 0, buf.flags & RTPRIVATE);
    }
  fclose(fp);
}

