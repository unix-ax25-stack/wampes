/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arpfile.c,v 1.4 1991/02/24 20:16:28 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "timer.h"
#include "arp.h"

extern int  debug;

#define ARP_FILE_VERSION   1
#define ARP_SAVETIME       (60*10)

struct arp_saverecord {
  int32 ip_addr;        /* IP address, host order */
  char  hardware;       /* Hardware type */
  char  pub;            /* Publish this entry? */
};

struct arp_saverecord_0 {
  int32 ip_addr;        /* IP address, host order */
  int16 hardware;       /* Hardware type */
  int16 hwalen;         /* Length of hardware address */
  char  pub;            /* Publish this entry? */
};

static char  arp_filename[] = "/tcp/arp_data";
static char  arp_tmpfilename[] = "/tcp/arp_tmp";

/*---------------------------------------------------------------------------*/

void arp_savefile()
{

  FILE * fp;
  int  i;
  static long  nextsavetime;
  struct arp_saverecord buf;
  struct arp_tab *p;

  if (!nextsavetime) nextsavetime = secclock() + ARP_SAVETIME;
  if (debug || nextsavetime > secclock()) return;
  nextsavetime = secclock() + ARP_SAVETIME;
  if (!(fp = fopen(arp_tmpfilename, "w"))) return;
  putc(ARP_FILE_VERSION, fp);
  for (i = 0; i < HASHMOD; i++)
    for (p = Arp_tab[i]; p; p = p->next)
      if (p->hw_addr && p->state == ARP_VALID) {
	buf.ip_addr = p->ip_addr;
	buf.hardware = p->hardware;
	buf.pub = p->pub;
	fwrite((char *) & buf, sizeof(buf), 1, fp);
	fwrite(p->hw_addr, Arp_type[p->hardware].hwalen, 1, fp);
      }
  fclose(fp);
  rename(arp_tmpfilename, arp_filename);
}

/*---------------------------------------------------------------------------*/

void arp_loadfile()
{

  FILE * fp;
  char  hw_addr[MAXHWALEN];
  static int  done;
  struct arp_saverecord buf;
  struct arp_saverecord_0 buf_0;
  struct arp_tab *p;

  if (done) return;
  done = 1;
  if (debug || !(fp = fopen(arp_filename, "r"))) return;
  switch (getc(fp)) {
  case 0:
    while (fread((char *) & buf_0, sizeof(buf_0), 1, fp) &&
	   buf_0.hardware < NHWTYPES &&
	   fread(hw_addr, buf_0.hwalen, 1, fp))
      if (p = arp_add(buf_0.ip_addr, buf_0.hardware, hw_addr, buf_0.pub)) {
	stop_timer(&p->timer);
	set_timer(&p->timer, 0);
      }
    break;
  case ARP_FILE_VERSION:
    while (fread((char *) & buf, sizeof(buf), 1, fp) &&
	   buf.hardware < NHWTYPES &&
	   fread(hw_addr, Arp_type[buf.hardware].hwalen, 1, fp))
      if (p = arp_add(buf.ip_addr, buf.hardware, hw_addr, buf.pub)) {
	stop_timer(&p->timer);
	set_timer(&p->timer, 0);
      }
    break;
  }
  fclose(fp);
}

