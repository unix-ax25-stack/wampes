/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25cmd.c,v 1.7 1994/10/09 08:22:45 deyke Exp $ */

/* AX25 control commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "ax25.h"
#include "lapb.h"
#include "cmdparse.h"
#include "socket.h"
/* #include "mailbox.h" */
#include "session.h"
#include "tty.h"
/* #include "nr4.h" */
#include "commands.h"

static int axdest(struct iface *ifp);
static int axheard(struct iface *ifp);
static void axflush(struct iface *ifp);
static int doaxflush(int argc,char *argv[],void *p);
static int doaxkick(int argc,char *argv[],void *p);
static int doaxreset(int argc,char *argv[],void *p);
static int doaxroute(int argc,char *argv[],void *p);
static int doaxstat(int argc,char *argv[],void *p);
static int doaxwindow(int argc,char *argv[],void *p);
static int doblimit(int argc,char *argv[],void *p);
static int dodigipeat(int argc,char *argv[],void *p);
static int domaxframe(int argc,char *argv[],void *p);
static int domycall(int argc,char *argv[],void *p);
static int don2(int argc,char *argv[],void *p);
static int dopaclen(int argc,char *argv[],void *p);
static int dopthresh(int argc,char *argv[],void *p);
static int dot1(int argc,char *argv[],void *p);
static int dot3(int argc,char *argv[],void *p);
static int dot4(int argc,char *argv[],void *p);
static int doversion(int argc,char *argv[],void *p);
static int dorouteadd(int argc,char *argv[],void *p);
static void doroutelistentry(struct ax_route *rp);
static int doroutelist(int argc,char *argv[],void *p);
static int doroutestat(int argc,char *argv[],void *p);
static int dojumpstart(int argc,char *argv[],void *p);

char *Ax25states[] = {
	"",
	"Disconn",
	"Listening",
	"Conn pend",
	"Disc pend",
	"Connected",
	"Recovery",
};

/* Ascii explanations for the disconnect reasons listed in lapb.h under
 * "reason" in ax25_cb
 */
char *Axreasons[] = {
	"Normal",
	"DM received",
	"Timeout"
};

static struct cmds Axcmds[] = {
	"blimit",       doblimit,       0, 0, NULLCHAR,
	"destlist",     doaxdest,       0, 0, NULLCHAR,
	"digipeat",     dodigipeat,     0, 0, NULLCHAR,
	"flush",        doaxflush,      0, 0, NULLCHAR,
	"heard",        doaxheard,      0, 0, NULLCHAR,
	"jumpstart",    dojumpstart,    0, 2, "ax25 jumpstart <call> [ON|OFF]",
	"kick",         doaxkick,       0, 2, "ax25 kick <axcb>",
	"maxframe",     domaxframe,     0, 0, NULLCHAR,
	"mycall",       domycall,       0, 0, NULLCHAR,
	"paclen",       dopaclen,       0, 0, NULLCHAR,
	"pthresh",      dopthresh,      0, 0, NULLCHAR,
	"reset",        doaxreset,      0, 2, "ax25 reset <axcb>",
	"retry",        don2,           0, 0, NULLCHAR,
	"route",        doaxroute,      0, 0, NULLCHAR,
	"status",       doaxstat,       0, 0, NULLCHAR,
	"t1",           dot1,           0, 0, NULLCHAR,
	"t3",           dot3,           0, 0, NULLCHAR,
	"t4",           dot4,           0, 0, NULLCHAR,
	"version",      doversion,      0, 0, NULLCHAR,
	"window",       doaxwindow,     0, 0, NULLCHAR,
	NULLCHAR,
};
/* Multiplexer for top-level ax25 command */
int
doax25(
int argc,
char *argv[],
void *p)
{
	return subcmd(Axcmds,argc,argv,p);
}

int
doaxheard(
int argc,
char *argv[],
void *p)
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULLIF){
			printf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			printf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axheard(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;       /* Not an ax.25 interface */
		if(axheard(ifp) == EOF)
			break;
	}
	return 0;
}
static int
axheard(
struct iface *ifp)
{
	struct lq *lp;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULLCHAR)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last heard           Pkts\n");
	for(lp = Lq;lp != NULLLQ;lp = lp->next){
		if(lp->iface != ifp)
			continue;
		if(printf("%-10s%-17s%8lu\n",pax25(tmp,lp->addr),
		 tformat(secclock() - lp->time),lp->currxcnt) == EOF)
			return EOF;
	}
	return 0;
}
int
doaxdest(
int argc,
char *argv[],
void *p)
{
	struct iface *ifp;

	if(argc > 1){
		if((ifp = if_lookup(argv[1])) == NULLIF){
			printf("Interface %s unknown\n",argv[1]);
			return 1;
		}
		if(ifp->output != ax_output){
			printf("Interface %s not AX.25\n",argv[1]);
			return 1;
		}
		axdest(ifp);
		return 0;
	}
	for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;       /* Not an ax.25 interface */
		if(axdest(ifp) == EOF)
			break;
	}
	return 0;
}
static int
axdest(
struct iface *ifp)
{
	struct ld *lp;
	struct lq *lq;
	char tmp[AXBUF];

	if(ifp->hwaddr == NULLCHAR)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last ref         Last heard           Pkts\n");
	for(lp = Ld;lp != NULLLD;lp = lp->next){
		if(lp->iface != ifp)
			continue;

		printf("%-10s%-17s",
		 pax25(tmp,lp->addr),tformat(secclock() - lp->time));

		if(addreq(lp->addr,ifp->hwaddr)){
			/* Special case; it's our address */
			printf("%-17s",tformat(secclock() - ifp->lastsent));
		} else if((lq = al_lookup(ifp,lp->addr,0)) == NULLLQ){
			printf("%-17s","");
		} else {
			printf("%-17s",tformat(secclock() - lq->time));
		}
		if(printf("%8lu\n",lp->currxcnt) == EOF)
			return EOF;
	}
	return 0;
}
static int
doaxflush(
int argc,
char *argv[],
void *p)
{
	struct iface *ifp;

	for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(ifp->output != ax_output)
			continue;       /* Not an ax.25 interface */
		axflush(ifp);
	}
	return 0;
}
static void
axflush(
struct iface *ifp)
{
	struct lq *lp,*lp1;
	struct ld *ld,*ld1;

	ifp->rawsndcnt = 0;
	for(lp = Lq;lp != NULLLQ;lp = lp1){
		lp1 = lp->next;
		free((char *)lp);
	}
	Lq = NULLLQ;
	for(ld = Ld;ld != NULLLD;ld = ld1){
		ld1 = ld->next;
		free((char *)ld);
	}
	Ld = NULLLD;
}

static int
doaxreset(
int argc,
char *argv[],
void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	reset_ax25(axp);
	return 0;
}

/* Display AX.25 link level control blocks */
static int
doaxstat(
int argc,
char *argv[],
void *p)
{
	register struct ax25_cb *axp;

	if(argc < 2){
		printf("   &AXCB Rcv-Q Unack  Rt  Srtt  State          Remote\n");
		for(axp = Ax25_cb;axp != NULLAX25; axp = axp->next){
			if(printf("%8lx %5u%c%3u/%u%c %2d%6lu  %-13s  %s\n",
				ptol(axp),
				len_p(axp->rxq),
				axp->flags.rnrsent ? '*' : ' ',
				axp->unack,
				axp->maxframe,
				axp->flags.remotebusy ? '*' : ' ',
				axp->retries,
				axp->srt,
				Ax25states[axp->state],
				ax25hdr_to_string(&axp->hdr)) == EOF)
					return 0;
		}
		if (Axserver_enabled)
			printf("                                Listening      *\n");
		return 0;
	}
	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	st_ax25(axp);
	return 0;
}
/* Dump one control block */
void
st_ax25(
register struct ax25_cb *axp)
{
	char tmp[AXBUF];

	if(axp == NULLAX25)
		return;
	printf("    &AXB Remote   RB V(S) V(R) Unack P Retry State\n");

	printf("%8lx %-9s%c%c",ptol(axp),pax25(tmp,axp->hdr.dest),
	 axp->flags.rejsent ? 'R' : ' ',
	 axp->flags.remotebusy ? 'B' : ' ');
	printf(" %4d %4d",axp->vs,axp->vr);
	printf(" %02u/%02u %u",axp->unack,axp->maxframe,axp->proto);
	printf(" %02u/%02u",axp->retries,axp->n2);
	printf(" %s\n",Ax25states[axp->state]);

	printf("srtt = %lu mdev = %lu ",axp->srt,axp->mdev);
	printf("T1: ");
	if(run_timer(&axp->t1))
		printf("%lu",read_timer(&axp->t1));
	else
		printf("stop");
	printf("/%lu ms; ",dur_timer(&axp->t1));

	printf("T3: ");
	if(run_timer(&axp->t3))
		printf("%lu",read_timer(&axp->t3));
	else
		printf("stop");
	printf("/%lu ms\n",dur_timer(&axp->t3));

}

/* Display or change our AX.25 address */
static int
domycall(
int argc,
char *argv[],
void *p)
{
	char tmp[AXBUF];

	if(argc < 2){
		printf("%s\n",pax25(tmp,Mycall));
		return 0;
	}
	if(setcall(Mycall,argv[1]) == -1)
		return -1;
	return 0;
}

/* Control AX.25 digipeating */
static int
dodigipeat(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Digipeat,"Digipeat",argc,argv,0,2);
}
/* Set limit on retransmission backoff */
static int
doblimit(
int argc,
char *argv[],
void *p)
{
	return setlong(&Blimit,"blimit",argc,argv);
}
static int
doversion(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Axversion,"AX25 version",argc,argv,1,2);
}

static int
dot1(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T1init,"T1 (ms)",argc,argv,1,0x7fffffff);
}

/* Set idle timer */
static int
dot3(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T3init,"Idle poll timer (ms)",argc,argv,0,0x7fffffff);
}

/* Set busy timer */
static int
dot4(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T4init,"Busy timer (ms)",argc,argv,1,0x7fffffff);
}

/* Set retry limit count */
static int
don2(
int argc,
char *argv[],
void *p)
{
	return setintrc(&N2,"Retry limit",argc,argv,0,MAXINT16);
}
/* Force a retransmission */
static int
doaxkick(
int argc,
char *argv[],
void *p)
{
	struct ax25_cb *axp;

	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf(Notval);
		return 1;
	}
	kick_ax25(axp);
	return 0;
}
/* Set maximum number of frames that will be allowed in flight */
static int
domaxframe(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Maxframe,"Window size (frames)",argc,argv,1,7);
}

/* Set maximum length of I-frame data field */
static int
dopaclen(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Paclen,"Max frame length (bytes)",argc,argv,1,MAXINT16);
}
/* Set size of I-frame above which polls will be sent after a timeout */
static int
dopthresh(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Pthresh,"Poll threshold (bytes)",argc,argv,0,MAXINT16);
}

/* Set high water mark on receive queue that triggers RNR */
static int
doaxwindow(
int argc,
char *argv[],
void *p)
{
	return setintrc(&Axwindow,"AX25 receive window (bytes)",argc,argv,1,MAXINT16);
}
/* End of ax25 subcommands */

/* Display and modify AX.25 routing table */
static int
doaxroute(
int argc,
char *argv[],
void *p)
{

  static struct cmds routecmds[] = {

    "add",  dorouteadd,  0, 3, "ax25 route add [permanent] <interface> default|<path>",
    "list", doroutelist, 0, 0, NULLCHAR,
    "stat", doroutestat, 0, 0, NULLCHAR,

    NULLCHAR, NULLFP,    0, 0, NULLCHAR
  };

  axroute_loadfile();
  if(argc >= 2) return subcmd(routecmds, argc, argv, p);
  doroutestat(argc, argv, p);
  return 0;
}

static int
dorouteadd(
int argc,
char *argv[],
void *p)
{

  int i, j, perm;
  struct ax25 hdr, hdr1;
  struct iface *iface;

  argc--;
  argv++;

  if ((perm = !strcmp(*argv, "permanent"))) {
    argc--;
    argv++;
  }

  if (!(iface = if_lookup(*argv))) {
    printf("Interface \"%s\" unknown\n", *argv);
    return 1;
  }
  if (iface->output != ax_output) {
    printf("Interface \"%s\" not kiss\n", *argv);
    return 1;
  }
  argc--;
  argv++;

  if (argc <= 0) {
    printf("Usage: ax25 route add [permanent] <interface> default|<path>\n");
    return 1;
  }

  if (!strcmp(*argv, "default")) {
    Axroute_default_ifp = iface;
    return 0;
  }

  if (ax25args_to_hdr(argc, argv, &hdr))
    return 1;

  hdr1.nextdigi = hdr1.ndigis = hdr.ndigis;
  addrcp(hdr1.source, hdr.dest);
  for (i = 0, j = hdr.ndigis - 1; j >= 0; i++, j--)
    addrcp(hdr1.digis[i], hdr.digis[j]);

  axroute_add(iface, &hdr1, perm);
  return 0;
}

static void
doroutelistentry(
struct ax_route *rp)
{

	char *cp;
	char buf[1024];
	int i;
	int jumpstart;
	int n;
	int perm;
	struct ax_route *rp_stack[20];
	struct iface *ifp = 0;
	struct tm *tm;

	tm = localtime(&rp->time);
	pax25(cp = buf, rp->target);
	perm = rp->perm;
	jumpstart = rp->jumpstart;
	for (n = 0; rp; rp = rp->digi) {
		rp_stack[++n] = rp;
		ifp = rp->ifp;
	}
	for (i = n; i > 1; i--) {
		strcat(cp, i == n ? " via " : ",");
		while (*cp)
			cp++;
		pax25(cp, rp_stack[i]->target);
	}
	printf("%2d-%.3s  %02d:%02d  %-9s  %c%c %s\n",
	       tm->tm_mday,
	       "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	       tm->tm_hour,
	       tm->tm_min,
	       ifp ? ifp->name : "???",
	       perm ? 'P' : ' ',
	       jumpstart ? 'J' : ' ',
	       buf);
}

static int
doroutelist(
int argc,
char *argv[],
void *p)
{

  char call[AXALEN];
  int i;
  struct ax_route *rp;

  puts("Date    Time   Interface  PJ Path");
  if(argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = Ax_routes[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if(setcall(call, *argv) || !(rp = ax_routeptr(call, 0)))
      printf("*** Not in table *** %s\n", *argv);
    else
      doroutelistentry(rp);
  return 0;
}

static int
doroutestat(
int argc,
char *argv[],
void *p)
{

#define NIFACES 128

  struct ifptable_t {
    struct iface *ifp;
    int count;
  };

  int dev;
  int i;
  int total;
  struct ax_route *dp;
  struct ax_route *rp;
  struct iface *ifp;
  struct ifptable_t ifptable[NIFACES];

  memset((char *) ifptable, 0, sizeof(ifptable));
  for (dev = 0, ifp = Ifaces; ifp; dev++, ifp = ifp->next)
    ifptable[dev].ifp = ifp;
  for (i = 0; i < AXROUTESIZE; i++)
    for (rp = Ax_routes[i]; rp; rp = rp->next) {
      for (dp = rp; dp->digi; dp = dp->digi) ;
      if(dp->ifp)
	for (dev = 0; dev < NIFACES; dev++)
	  if(ifptable[dev].ifp == dp->ifp) {
	    ifptable[dev].count++;
	    break;
	  }
    }
  puts("Interface  Count");
  total = 0;
  for (dev = 0; dev < NIFACES; dev++) {
    if(ifptable[dev].count ||
	(Axroute_default_ifp && Axroute_default_ifp == ifptable[dev].ifp))
      printf("%c %-7s  %5d\n",
	     ifptable[dev].ifp == Axroute_default_ifp ? '*' : ' ',
	     ifptable[dev].ifp->name,
	     ifptable[dev].count);
    total += ifptable[dev].count;
  }
  puts("---------  -----");
  printf("  total    %5d\n", total);
  return 0;
}
static int
dojumpstart(
int argc,
char *argv[],
void *p)
{

	char *cp;
	char buf[32];
	char tmp[AXBUF];
	struct ax_route *axr;

	if(setcall(tmp,argv[1]) == -1)
		return -1;
	axr = ax_routeptr(tmp,1);
	strcpy(cp = buf,"Jumpstart ");
	while(*cp)
		cp++;
	pax25(cp,tmp);
	return setbool(&axr->jumpstart,buf,argc - 1,argv + 1);
}
