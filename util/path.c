#include <ctype.h>
#include <memory.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "../src/global.h"
#include "../src/netuser.h"
#include "../src/mbuf.h"
#include "../src/timer.h"
#include "../src/iface.h"
#include "../src/ax25.h"
#include "../src/axproto.h"
#include "../src/cmdparse.h"
#include "../src/asy.h"

extern long  time();
extern void free();

#define AXROUTEHOLDTIME (90l*24l*60l*60l)
#define AXROUTESIZE     499

struct axroute_tab {
  struct ax25_addr call;
  struct axroute_tab *digi;
  struct interface *ifp;
  long  time;
  struct axroute_tab *next;
};

struct axroutesaverecord {
  struct ax25_addr call, digi;
  short  dev;
  long  time;
};

static char  axroutefile[] = "/tcp/axroute_data";
static long  currtime;
static struct axroute_tab *axroute_tab[AXROUTESIZE];
static struct interface *axroute_default_ifp;
static struct interface *ifaces;

/*---------------------------------------------------------------------------*/

static int  ax_output()
{
}

/*---------------------------------------------------------------------------*/

/* Convert encoded AX.25 address to printable string */
static pax25(e,addr)
char *e;
struct ax25_addr *addr;
{
	register int i;
	char c,*cp;

	cp = addr->call;
	for(i=6;i != 0;i--){
		c = (*cp++ >> 1) & 0x7f;
		if(c == ' ')
			break;
		*e++ = c;
	}
	if (i = (addr->ssid >> 1) & 0xf)        /* ssid */
		sprintf(e,"-%d", i);
	else
		*e = '\0';
}

/*---------------------------------------------------------------------------*/

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
static int setcall(out,call)
struct ax25_addr *out;
char *call;
{
	int csize;
	unsigned ssid;
	register int i;
	register char *cp,*dp;
	char c;

	if(out == (struct ax25_addr *)NULL || call == NULLCHAR || *call == '\0'){
		return -1;
	}
	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	dp = strchr(call,'-');
	if(dp == NULLCHAR)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > 6)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULLCHAR){
		dp++;   /* skip dash */
		ssid = atoi(dp);
		if(ssid > 15)
			return -1;
	} else
		ssid = 0;
	/* Copy upper-case callsign, left shifted one bit */
	cp = out->call;
	for(i=0;i<csize;i++){
		c = *call++;
		if(islower(c))
			c = toupper(c);
		*cp++ = c << 1;
	}
	/* Pad with shifted spaces if necessary */
	for(;i<6;i++)
		*cp++ = ' ' << 1;

	/* Insert substation ID field and set reserved bits */
	out->ssid = 0x60 | (ssid << 1);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int  addreq(a, b)
register long  *a, *b;
{
  return (*a == *b && (a[1] & 0xffff1e00) == (b[1] & 0xffff1e00));
}

/*---------------------------------------------------------------------------*/

static int  axroute_hash(call)
register char  *call;
{
  register long  hashval;

  hashval =                  (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | (*call++ & 0xf);
  hashval = (hashval << 4) | ((*call >> 1) & 0xf);
  return hashval % AXROUTESIZE;
}

/*---------------------------------------------------------------------------*/

static struct axroute_tab *axroute_tabptr(call, create)
struct ax25_addr *call;
int create;
{

  int  hashval;
  register struct axroute_tab *rp;

  hashval = axroute_hash(call);
  for (rp = axroute_tab[hashval]; rp && !addreq(&rp->call, call); rp = rp->next) ;
  if (create && !rp) {
    rp = (struct axroute_tab *) calloc(1, sizeof(struct axroute_tab ));
    rp->call = *call;
    rp->next = axroute_tab[hashval];
    axroute_tab[hashval] = rp;
  }
  return rp;
}

/*---------------------------------------------------------------------------*/

static void axroute_loadfile()
{

  FILE * fp;
  struct axroute_tab *rp;
  struct axroutesaverecord buf;
  struct interface *ifp, *ifptable[ASY_MAX];

  if (!(fp = fopen(axroutefile, "r"))) return;
  memset((char *) ifptable, 0, sizeof(ifptable));
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
  while (fread((char *) &buf, sizeof(buf), 1, fp)) {
    if (buf.time + AXROUTEHOLDTIME < currtime) continue;
    rp = axroute_tabptr(&buf.call, 1);
    if (buf.digi.call[0]) rp->digi = axroute_tabptr(&buf.digi, 1);
    if (buf.dev >= 0 && buf.dev < ASY_MAX) rp->ifp = ifptable[buf.dev];
    rp->time = buf.time;
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void doroutelistentry(rp)
struct axroute_tab *rp;
{

  char  *cp, buf[1024];
  int  i, n;
  struct axroute_tab *rp_stack[20];
  struct interface *ifp;
  struct tm *tm;

  tm = gmtime(&rp->time);
  pax25(cp = buf, &rp->call);
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    ifp = rp->ifp;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, &(rp_stack[i]->call));
  }
  printf("%2d-%.3s  %-9s  %s\n", tm->tm_mday, "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon, ifp ? ifp->name : "???", buf);
}

/*---------------------------------------------------------------------------*/

static int  doroutelist(argc, argv)
int  argc;
char  *argv[];
{

  int  i;
  struct ax25_addr call;
  struct axroute_tab *rp;

  puts("Date    Interface  Path");
  if (argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = axroute_tab[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if (setcall(&call, *argv) || !(rp = axroute_tabptr(&call, 0)))
      printf("** Not in table ** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  doroutestat()
{

  int  count[ASY_MAX], total;
  register int  i, dev;
  register struct axroute_tab *rp, *dp;
  struct interface *ifp, *ifptable[ASY_MAX];

  memset((char *) ifptable, 0, sizeof(ifptable));
  memset((char *) count, 0, sizeof(count));
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->output == ax_output) ifptable[ifp->dev] = ifp;
  for (i = 0; i < AXROUTESIZE; i++)
    for (rp = axroute_tab[i]; rp; rp = rp->next) {
      for (dp = rp; dp->digi; dp = dp->digi) ;
      if (dp->ifp) count[dp->ifp->dev]++;
    }
  puts("Interface  Count");
  total = 0;
  for (dev = 0; dev < ASY_MAX && ifptable[dev]; dev++) {
    if (ifptable[dev] == axroute_default_ifp || count[dev])
      printf("%c %-7s  %5d\n", ifptable[dev] == axroute_default_ifp ? '*' : ' ', ifptable[dev]->name, count[dev]);
    total += count[dev];
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void attach(ifname, dev)
char  *ifname;
int16 dev;
{
  int  ax_output();
  struct interface *ifp;

  ifp = (struct interface *) calloc(1, sizeof(struct interface ));
  ifp->name = ifname;
  ifp->dev = dev;
  ifp->output = ax_output;
  ifp->next = ifaces;
  ifaces = ifp;
}

/*---------------------------------------------------------------------------*/

static void hash_performance()
{

  register int  i, len;
  register struct axroute_tab *rp;

  puts("Index  Length");
  for (i = 0; i < AXROUTESIZE; i++) {
    len = 0;
    for (rp = axroute_tab[i]; rp; rp = rp->next) len++;
    printf("%5d  %6d  ", i, len);
    while (len--) putchar('*');
    putchar('\n');
  }
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{
  currtime = time(0);
  attach("tnc0", 0);
  attach("tnc1", 1);
  attach("tnc2", 2);
  attach("tnc3", 3);
  attach("tnc4", 4);
  attach("tnc5", 5);
  attach("tnc6", 6);
  attach("tnc7", 7);
  axroute_loadfile();
  if (!strcmp(argv[1], "hash"))
    hash_performance();
  else if (!strcmp(argv[1], "stat"))
    doroutestat();
  else
    doroutelist(argc, argv);
  return 0;
}

