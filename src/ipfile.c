/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipfile.c,v 1.17 1996/01/04 19:11:42 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "main.h"

#define ROUTE_FILE_VERSION   3
#define ROUTE_SAVETIME       (10L*60L*1000L)

struct route_saverecord_1 {
  int32 target;         /* Target IP address */
  unsigned int bits;    /* Number of significant bits */
  int32 gateway;        /* IP address of local gateway for this target */
  int32 metric;         /* Hop count or whatever */
  int flags;
};

struct route_saverecord_2 {
  int32 target;         /* Target IP address */
  unsigned int bits;    /* Number of significant bits */
  int32 gateway;        /* IP address of local gateway for this target */
  int32 metric;         /* Hop count or whatever */
  int flags;
  int32 expires;
};

struct route_saverecord_3 {
  int32 target;         /* Target IP address */
  unsigned int bits;    /* Number of significant bits */
  int32 gateway;        /* IP address of local gateway for this target */
  int32 metric;         /* Hop count or whatever */
  unsigned int rtprivate:1; /* Don't advertise this route */
  int32 expires;
};

static const char route_filename[] = "/tcp/route_data";
static const char route_tmpfilename[] = "/tcp/route_tmp";

/*---------------------------------------------------------------------------*/

void route_savefile(void)
{

  FILE *fp;
  int bits;
  int i;
  static struct timer timer;
  struct route *p;
  struct route_saverecord_3 buf;

  if (Debug) return;
  if (!main_exit) {
    switch (timer.state) {
    case TIMER_STOP:
      timer.func = (void (*)(void *)) route_savefile;
      timer.arg = 0;
      set_timer(&timer, ROUTE_SAVETIME);
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
  if (!(fp = fopen(route_tmpfilename, "w"))) return;
  putc(ROUTE_FILE_VERSION, fp);
  rt_merge(0);
  for (bits = 1; bits <= 32; bits++)
    for (i = 0; i < HASHMOD; i++)
      for (p = Routes[bits-1][i]; p; p = p->next)
	if (run_timer(&p->timer)) {
	  memset(&buf, 0, sizeof(buf));
	  buf.target = p->target;
	  buf.bits = p->bits;
	  buf.gateway = p->gateway;
	  buf.metric = p->metric;
	  buf.rtprivate = p->flags.rtprivate;
	  buf.expires = secclock() + read_timer(&p->timer) / 1000;
	  fwrite((char *) &buf, sizeof(buf), 1, fp);
	  fwrite(p->iface->name, strlen(p->iface->name) + 1, 1, fp);
	}
  fclose(fp);
  rename(route_tmpfilename, route_filename);
}

/*---------------------------------------------------------------------------*/

void route_loadfile(void)
{

  char ifname[1024];
  char *cp;
  FILE *fp;
  int c;
  int32 ttl;
  static int done;
  struct iface *ifp;

  if (done) return;
  done = 1;
  if (Debug || !(fp = fopen(route_filename, "r"))) return;
  switch (getc(fp)) {
  case 1:
    {
      struct route_saverecord_1 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
	cp = ifname;
	do {
	  if ((c = getc(fp)) == EOF) {
	    fclose(fp);
	    return;
	  }
	} while ((*cp++ = c));
	for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
	if (ifp)
	  rt_add(buf.target, buf.bits, buf.gateway, ifp, buf.metric,
		 0x7fffffff / 1000, buf.flags & 1);
      }
    }
    break;
  case 2:
    {
      struct route_saverecord_2 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
	cp = ifname;
	do {
	  if ((c = getc(fp)) == EOF) {
	    fclose(fp);
	    return;
	  }
	} while ((*cp++ = c));
	for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
	if (ifp && (ttl = buf.expires - secclock()) > 0)
	  rt_add(buf.target, buf.bits, buf.gateway, ifp, buf.metric,
		 ttl, buf.flags & 1);
      }
    }
    break;
  case 3:
    {
      struct route_saverecord_3 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp)) {
	cp = ifname;
	do {
	  if ((c = getc(fp)) == EOF) {
	    fclose(fp);
	    return;
	  }
	} while ((*cp++ = c));
	for (ifp = Ifaces; ifp && strcmp(ifp->name, ifname); ifp = ifp->next) ;
	if (ifp && (ttl = buf.expires - secclock()) > 0)
	  rt_add(buf.target, buf.bits, buf.gateway, ifp, buf.metric,
		 ttl, buf.rtprivate);
      }
    }
    break;
  }
  fclose(fp);
}
