#include <stdio.h>

#include "global.h"
#include "timer.h"
#include "arp.h"

extern int  debug;

#define ARP_FILE_VERSION   0
#define ARP_SAVETIME       (60*10)

struct arp_saverecord {
  int32 ip_addr;          /* IP address, host order */
  int16 hardware;         /* Hardware type */
  int16 hwalen;           /* Length of hardware address */
  char  pub;              /* Publish this entry? */
};

static char  arp_filename[] = "/tcp/arp_data";
static char  arp_tmpfilename[] = "/tcp/arp_tmp";

/*---------------------------------------------------------------------------*/

arp_savefile()
{

  FILE * fp;
  extern long  currtime;
  register int  i;
  register struct arp_tab *p;
  static long  nextsavetime;
  struct arp_saverecord buf;

  if (!nextsavetime) nextsavetime = currtime + ARP_SAVETIME;
  if (debug || nextsavetime > currtime) return;
  nextsavetime = currtime + ARP_SAVETIME;
  if (!(fp = fopen(arp_tmpfilename, "w"))) return;
  putc(ARP_FILE_VERSION, fp);
  for (i = 0; i < ARPSIZE; i++)
    for (p = arp_tab[i]; p; p = p->next)
      if (p->hw_addr && p->state == ARP_VALID) {
	buf.ip_addr = p->ip_addr;
	buf.hardware = p->hardware;
	buf.hwalen = p->hwalen;
	buf.pub = p->pub;
	fwrite((char *) & buf, sizeof(buf), 1, fp);
	fwrite(p->hw_addr, p->hwalen, 1, fp);
      }
  fclose(fp);
  rename(arp_tmpfilename, arp_filename);
}

/*---------------------------------------------------------------------------*/

arp_loadfile()
{

  FILE * fp;
  char  hw_addr[MAXHWALEN];
  register struct arp_tab *p;
  static int  done;
  struct arp_saverecord buf;

  if (done) return;
  done = 1;
  if (debug || !(fp = fopen(arp_filename, "r"))) return;
  if (getc(fp) == ARP_FILE_VERSION)
    while (fread((char *) & buf, sizeof(buf), 1, fp))
      if (fread(hw_addr, buf.hwalen, 1, fp))
	if (buf.hardware < NHWTYPES)
	  if (p = arp_add(buf.ip_addr, buf.hardware, hw_addr, buf.hwalen, buf.pub)) {
	    stop_timer(&p->timer);
	    p->timer.count = p->timer.start = 0;
	  }
  fclose(fp);
}

