/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arpfile.c,v 1.11 1994/10/06 16:15:19 deyke Exp $ */

#include <stdio.h>

#include "global.h"
#include "timer.h"
#include "arp.h"

#define ARP_FILE_VERSION   2
#define ARP_SAVETIME       (10L*60L*1000L)

struct arp_saverecord_0 {
  int32 ip_addr;        /* IP address, host order */
  uint16 hardware;      /* Hardware type */
  uint16 hwalen;        /* Length of hardware address */
  char pub;             /* Publish this entry? */
};

struct arp_saverecord_1 {
  int32 ip_addr;        /* IP address, host order */
  unsigned char hardware;/* Hardware type */
  char pub;             /* Publish this entry? */
};

struct arp_saverecord_2 {
  int32 ip_addr;        /* IP address, host order */
  unsigned char hardware;/* Hardware type */
  char pub;             /* Publish this entry? */
  int32 expires;
};

static const char arp_filename[] = "/tcp/arp_data";
static const char arp_tmpfilename[] = "/tcp/arp_tmp";

/*---------------------------------------------------------------------------*/

void arp_savefile(void)
{

  FILE * fp;
  int i;
  static struct timer timer;
  struct arp_saverecord_2 buf;
  struct arp_tab *p;

  switch (timer.state) {
  case TIMER_STOP:
    if (!Debug) {
      timer.func = (void (*)(void *)) arp_savefile;
      timer.arg = 0;
      set_timer(&timer, ARP_SAVETIME);
      start_timer(&timer);
    }
    return;
  case TIMER_RUN:
    return;
  case TIMER_EXPIRE:
    timer.state = TIMER_STOP;
    break;
  }
  if (!(fp = fopen(arp_tmpfilename, "w"))) return;
  putc(ARP_FILE_VERSION, fp);
  for (i = 0; i < HASHMOD; i++)
    for (p = Arp_tab[i]; p; p = p->next)
      if (p->hw_addr && p->state == ARP_VALID && run_timer(&p->timer)) {
	buf.ip_addr = p->ip_addr;
	buf.hardware = p->hardware;
	buf.pub = p->pub;
	buf.expires = secclock() + read_timer(&p->timer) / 1000;
	fwrite((char *) &buf, sizeof(buf), 1, fp);
	fwrite(p->hw_addr, Arp_type[p->hardware].hwalen, 1, fp);
      }
  fclose(fp);
  rename(arp_tmpfilename, arp_filename);
}

/*---------------------------------------------------------------------------*/

void arp_loadfile(void)
{

  FILE * fp;
  char hw_addr[MAXHWALEN];
  int32 ttl;
  static int done;
  struct arp_tab *p;

  if (done) return;
  done = 1;
  if (Debug || !(fp = fopen(arp_filename, "r"))) return;
  switch (getc(fp)) {
  case 0:
    {
      struct arp_saverecord_0 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp) &&
	     buf.hardware < NHWTYPES &&
	     fread(hw_addr, buf.hwalen, 1, fp))
	arp_add(buf.ip_addr, buf.hardware, hw_addr, buf.pub);
    }
    break;
  case 1:
    {
      struct arp_saverecord_1 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp) &&
	     buf.hardware < NHWTYPES &&
	     fread(hw_addr, Arp_type[buf.hardware].hwalen, 1, fp))
	arp_add(buf.ip_addr, buf.hardware, hw_addr, buf.pub);
    }
    break;
  case 2:
    {
      struct arp_saverecord_2 buf;
      while (fread((char *) &buf, sizeof(buf), 1, fp) &&
	     buf.hardware < NHWTYPES &&
	     fread(hw_addr, Arp_type[buf.hardware].hwalen, 1, fp))
	if ((ttl = buf.expires - secclock()) > 0 &&
	    (p = arp_add(buf.ip_addr, buf.hardware, hw_addr, buf.pub))) {
	  set_timer(&p->timer, ttl * 1000);
	  start_timer(&p->timer);
	}
    }
    break;
  }
  fclose(fp);
}

