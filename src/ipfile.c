#include <stdio.h>

#include "global.h"
#include "iface.h"
#include "timer.h"
#include "ip.h"

extern int  debug;

#define ROUTE_FILE_VERSION   0
#define ROUTE_SAVETIME       (60*10)

struct route_saverecord {
  int32 dest;           /* Dest IP address */
  int  bits;            /* Number of significant bits */
  int32 gateway;        /* IP address of local gateway for this target */
  int  metric;          /* Hop count, whatever */
};

static char  route_filename[] = "/tcp/route_data";
static char  route_tmpfilename[] = "/tcp/route_tmp";

/*---------------------------------------------------------------------------*/

route_savefile()
{

  FILE * fp;
  extern long  currtime;
  register int  bits;
  register int  i;
  register struct route *p;
  static long  nextsavetime;
  struct route_saverecord buf;

  if (!nextsavetime) nextsavetime = currtime + ROUTE_SAVETIME;
  if (debug || nextsavetime > currtime) return;
  nextsavetime = currtime + ROUTE_SAVETIME;
  if (!(fp = fopen(route_tmpfilename, "w"))) return;
  putc(ROUTE_FILE_VERSION, fp);
  for (bits = 1; bits <= 32; bits++)
    for (i = 0; i < NROUTE; i++)
      for (p = routes[bits-1][i]; p; p = p->next) {
	buf.dest = p->target;
	buf.bits = bits;
	buf.gateway = p->gateway;
	buf.metric = p->metric;
	fwrite((char *) & buf, sizeof(buf), 1, fp);
	fwrite(p->interface->name, strlen(p->interface->name) + 1, 1, fp);
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
  register struct interface *ifp;
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
      for (ifp = ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
      if (ifp) rt_add(buf.dest, buf.bits, buf.gateway, buf.metric, ifp);
    }
  fclose(fp);
}

