/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25cmd.c,v 1.1 1993/01/29 06:51:43 deyke Exp $ */

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

static int doaxkick __ARGS((int argc,char *argv[],void *p));
static int doaxreset __ARGS((int argc,char *argv[],void *p));
static int doaxroute __ARGS((int argc,char *argv[],void *p));
static int doaxstat __ARGS((int argc,char *argv[],void *p));
static int doaxwindow __ARGS((int argc,char *argv[],void *p));
static int dodigipeat __ARGS((int argc,char *argv[],void *p));
static int domaxframe __ARGS((int argc,char *argv[],void *p));
static int domycall __ARGS((int argc,char *argv[],void *p));
static int don2 __ARGS((int argc,char *argv[],void *p));
static int dopaclen __ARGS((int argc,char *argv[],void *p));
static int dopthresh __ARGS((int argc,char *argv[],void *p));
static int dot1 __ARGS((int argc,char *argv[],void *p));
static int dot2 __ARGS((int argc,char *argv[],void *p));
static int dot3 __ARGS((int argc,char *argv[],void *p));
static int dot4 __ARGS((int argc,char *argv[],void *p));
static int dot5 __ARGS((int argc,char *argv[],void *p));
static int dorouteadd __ARGS((int argc,char *argv[],void *p));
static void doroutelistentry __ARGS((struct ax_route *rp));
static int doroutelist __ARGS((int argc,char *argv[],void *p));
static int doroutestat __ARGS((int argc,char *argv[],void *p));

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
	"digipeat",     dodigipeat,     0, 0, NULLCHAR,
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
	"t2",           dot2,           0, 0, NULLCHAR,
	"t3",           dot3,           0, 0, NULLCHAR,
	"t4",           dot4,           0, 0, NULLCHAR,
	"t5",           dot5,           0, 0, NULLCHAR,
	"window",       doaxwindow,     0, 0, NULLCHAR,
	NULLCHAR,
};
/* Multiplexer for top-level ax25 command */
int
doax25(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Axcmds,argc,argv,p);
}

static
doaxreset(argc,argv,p)
int argc;
char *argv[];
void *p;
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
static
doaxstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct ax25_cb *axp;

	if(argc < 2){
		printf("   &AXCB Rcv-Q Unack  Rt  Srtt  State          Remote\n");
		for(axp = Ax25_cb;axp != NULLAX25; axp = axp->next){
			if(printf("%8lx %5u%c%3u/%u%c %2d%6lu  %-13s  %s\n",
				ptol(axp),
				axp->rcvcnt,
				axp->flags.rnrsent ? '*' : ' ',
				axp->unack,
				axp->cwind,
				axp->flags.remotebusy ? '*' : ' ',
				axp->retries,
				axp->srt,
				Ax25states[axp->state],
				ax25hdr_to_string(&axp->hdr)) == EOF)
					return 0;
		}
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
st_ax25(axp)
register struct ax25_cb *axp;
{

	int i;
	struct mbuf *bp;

	if(axp == NULLAX25)
		return;

#define next_seq(n)  (((n) + 1) & 7)

	printf("Path:         %s\n", ax25hdr_to_string(&axp->hdr));
	printf("Interface:    %s\n", axp->iface ? axp->iface->name : "---");
	printf("State:        %s\n", Ax25states[axp->state]);
	if(axp->reason)
		printf("Reason:       %s\n", Axreasons[axp->reason]);
	printf("Mode:         %s\n", (axp->mode == STREAM) ? "Stream" : "Dgram");
	printf("Closed:       %s\n", axp->flags.closed ? "Yes" : "No");
	printf("Polling:      %s\n", axp->flags.polling ? "Yes" : "No");
	printf("RNRsent:      %s\n", axp->flags.rnrsent ? "Yes" : "No");
	printf("REJsent:      %s\n", axp->flags.rejsent ? "Yes" : "No");
	if(axp->flags.remotebusy)
		printf("Remote_busy:  %lu ms\n", msclock() - axp->flags.remotebusy);
	else
		printf("Remote_busy:  No\n");
	printf("CWind:        %d\n", axp->cwind);
	printf("Retry:        %d\n", axp->retries);
	printf("Srtt:         %ld ms\n", axp->srt);
	printf("Mean dev:     %ld ms\n", axp->mdev);
	printf("Timer T1:     ");
	if(run_timer(&axp->t1))
		printf("%lu", read_timer(&axp->t1));
	else
		printf("stop");
	printf("/%lu ms\n", dur_timer(&axp->t1));
	printf("Timer T2:     ");
	if(run_timer(&axp->t2))
		printf("%lu", read_timer(&axp->t2));
	else
		printf("stop");
	printf("/%lu ms\n", dur_timer(&axp->t2));
	printf("Timer T3:     ");
	if(run_timer(&axp->t3))
		printf("%lu", read_timer(&axp->t3));
	else
		printf("stop");
	printf("/%lu ms\n", dur_timer(&axp->t3));
	printf("Timer T4:     ");
	if(run_timer(&axp->t4))
		printf("%lu", read_timer(&axp->t4));
	else
		printf("stop");
	printf("/%lu ms\n", dur_timer(&axp->t4));
	printf("Timer T5:     ");
	if(run_timer(&axp->t5))
		printf("%lu", read_timer(&axp->t5));
	else
		printf("stop");
	printf("/%lu ms\n", dur_timer(&axp->t5));
	printf("Rcv queue:    %d\n", axp->rcvcnt);
	if(axp->reseq[0].bp || axp->reseq[1].bp ||
	   axp->reseq[2].bp || axp->reseq[3].bp ||
	   axp->reseq[4].bp || axp->reseq[5].bp ||
	   axp->reseq[6].bp || axp->reseq[7].bp) {
		printf("Reassembly queue:\n");
		for (i = next_seq(axp->vr); i != axp->vr; i = next_seq(i))
			if(axp->reseq[i].bp)
				printf("              Seq %3d: %3d bytes\n",
				       i, len_p(axp->reseq[i].bp));
	}
	printf("Snd queue:    %d\n", len_p(axp->sndq));
	if(axp->txq) {
		printf("Tx queue:\n");
		for (i = 0, bp = axp->txq; bp; i++, bp = bp->anext)
		printf("              Seq %3d: %3d bytes\n",
		       (axp->vs - axp->unack + i) & 7, len_p(bp));
	}
}

/* Display or change our AX.25 address */
static
domycall(argc,argv,p)
int argc;
char *argv[];
void *p;
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
static
dodigipeat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&Digipeat,"Digipeat",argc,argv,0,2);
}

/* Set retransmission timer */
static
dot1(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&T1init,"T1 (ms)",argc,argv,1,0x7fffffff);
}

/* Set acknowledgement delay timer */
static
dot2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&T2init,"T2 (ms)",argc,argv,1,0x7fffffff);
}

/* Set idle timer */
static
dot3(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&T3init,"Idle poll timer (ms)",argc,argv,0,0x7fffffff);
}

/* Set busy timer */
static
dot4(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&T4init,"T4 (ms)",argc,argv,1,0x7fffffff);
}

/* Set packet assembly timer */
static
dot5(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&T5init,"T5 (ms)",argc,argv,1,0x7fffffff);
}

/* Set retry limit count */
static
don2(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&N2,"Retry limit",argc,argv,0,MAXINT16);
}
/* Force a retransmission */
static
doaxkick(argc,argv,p)
int argc;
char *argv[];
void *p;
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
static
domaxframe(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&Maxframe,"Window size (frames)",argc,argv,1,7);
}

/* Set maximum length of I-frame data field */
static
dopaclen(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&Paclen,"Max frame length (bytes)",argc,argv,1,MAXINT16);
}
/* Set size of I-frame above which polls will be sent after a timeout */
static
dopthresh(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&Pthresh,"Poll threshold (bytes)",argc,argv,0,MAXINT16);
}

/* Set high water mark on receive queue that triggers RNR */
static
doaxwindow(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return setintrc(&Axwindow,"AX25 receive window (bytes)",argc,argv,1,MAXINT16);
}
/* End of ax25 subcommands */

/* Display and modify AX.25 routing table */
static int
doaxroute(argc,argv,p)
int argc;
char *argv[];
void *p;
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
dorouteadd(argc, argv, p)
int argc;
char *argv[];
void *p;
{

  int i, j, perm;
  struct ax25 hdr, hdr1;
  struct iface *iface;

  argc--;
  argv++;

  if (perm = !strcmp(*argv, "permanent")) {
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
doroutelistentry(rp)
struct ax_route *rp;
{

  char *cp, buf[1024];
  int i, n;
  int perm;
  struct ax_route *rp_stack[20];
  struct iface *ifp;
  struct tm *tm;

  tm = gmtime(&rp->time);
  pax25(cp = buf, rp->target);
  perm = rp->perm;
  for (n = 0; rp; rp = rp->digi) {
    rp_stack[++n] = rp;
    ifp = rp->ifp;
  }
  for (i = n; i > 1; i--) {
    strcat(cp, i == n ? " via " : ",");
    while (*cp) cp++;
    pax25(cp, rp_stack[i]->target);
  }
  printf("%2d-%.3s  %-9s  %c %s\n",
	 tm->tm_mday,
	 "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	 ifp ? ifp->name : "???",
	 perm ? '*' : ' ',
	 buf);
}

static int
doroutelist(argc,argv,p)
int argc;
char *argv[];
void *p;
{

  char call[AXALEN];
  int i;
  struct ax_route *rp;

  puts("Date    Interface  P Path");
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
doroutestat(argc,argv,p)
int argc;
char *argv[];
void *p;
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
	Axroute_default_ifp && Axroute_default_ifp == ifptable[dev].ifp)
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
