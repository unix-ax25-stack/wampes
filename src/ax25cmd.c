/* @(#) $Id: ax25cmd.c,v 1.19 2002/09/18 18:56:07 dl9sau Exp $ */

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
#include "dama.h"
#include "lapb.h"
#include "slhc.h"
#include "pidfilter.h"
#include "cmdparse.h"
#include "socket.h"
#include "session.h"
#include "tty.h"
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
static int doemaxframe(int argc,char *argv[],void *p);
static int domycall(int argc,char *argv[],void *p);
static int don2(int argc,char *argv[],void *p);
static int dopaclen(int argc,char *argv[],void *p);
static int dopthresh(int argc,char *argv[],void *p);
static int dot1(int argc,char *argv[],void *p);
static int dot2(int argc,char *argv[],void *p);
static int dot3(int argc,char *argv[],void *p);
static int dot4(int argc,char *argv[],void *p);
static int dot5(int argc,char *argv[],void *p);
static int doversion(int argc,char *argv[],void *p);
static int dorouteadd(int argc,char *argv[],void *p);
static void doroutelistentry(struct ax_route *rp);
static int doroutelist(int argc,char *argv[],void *p);
static int doroutestat(int argc,char *argv[],void *p);
static int doaxigntos(int argc,char *argv[],void *p);
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
	{ "blimit",       doblimit,       0, 0, NULL },
	{ "destlist",     doaxdest,       0, 0, NULL },
	{ "digipeat",     dodigipeat,     0, 0, NULL },
	{ "flush",        doaxflush,      0, 0, NULL },
	{ "heard",        doaxheard,      0, 0, NULL },
	{ "ignoretos",    doaxigntos,     0, 0, NULL },
	{ "jumpstart",    dojumpstart,    0, 2, "ax25 jumpstart <call> [ON|OFF]" },
	{ "kick",         doaxkick,       0, 2, "ax25 kick <axcb>" },
	{ "emaxframe",    doemaxframe,    0, 0,
	  "ax25 emaxframe [1..63]                the window on modulo-128 links\n"
	  "       (per port: \"ifconfig <iface> emaxframe\")" },
	{ "maxframe",     domaxframe,     0, 0, NULL },
	{ "mycall",       domycall,       0, 0, NULL },
	{ "paclen",       dopaclen,       0, 0, NULL },
	{ "pid-info",     pid_info,       0, 0,
	  "ax25 pid-info [<name>|<number>]       what a protocol id means\n"
	  "       With no argument the whole table.  These are the names that\n"
	  "       \"pid=\" and \"ifconfig <iface> pid\" accept." },
	{ "pthresh",      dopthresh,      0, 0, NULL },
	{ "reset",        doaxreset,      0, 2, "ax25 reset <axcb>" },
	{ "retry",        don2,           0, 0, NULL },
	{ "route",        doaxroute,      0, 0, NULL },
	{ "status",       doaxstat,       0, 0, NULL },
	{ "t1",           dot1,           0, 0, NULL },
	{ "t2",           dot2,           0, 0, NULL },
	{ "t3",           dot3,           0, 0, NULL },
	{ "t4",           dot4,           0, 0, NULL },
	{ "t5",           dot5,           0, 0, NULL },
	{ "version",      doversion,      0, 0, NULL },
	{ "window",       doaxwindow,     0, 0, NULL },
	{ NULL }
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
		if((ifp = if_lookup(argv[1])) == NULL){
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
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
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

	if(ifp->hwaddr == NULL)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last heard           Pkts\n");
	for(lp = Lq;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;
		printf("%-10s%-17s%8lu\n",pax25(tmp,lp->addr),
		 tformat(secclock() - lp->time),(unsigned long)lp->currxcnt);
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
		if((ifp = if_lookup(argv[1])) == NULL){
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
	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
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

	if(ifp->hwaddr == NULL)
		return 0;
	printf("%s:\n",ifp->name);
	printf("Station   Last ref         Last heard           Pkts\n");
	for(lp = Ld;lp != NULL;lp = lp->next){
		if(lp->iface != ifp)
			continue;

		printf("%-10s%-17s",
		 pax25(tmp,lp->addr),tformat(secclock() - lp->time));

		if(addreq(lp->addr,ifp->hwaddr)){
			/* Special case; it's our address */
			printf("%-17s",tformat(secclock() - ifp->lastsent));
		} else if((lq = al_lookup(ifp,lp->addr,0)) == NULL){
			printf("%-17s","");
		} else {
			printf("%-17s",tformat(secclock() - lq->time));
		}
		printf("%8lu\n",(unsigned long)lp->currxcnt);
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

	for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
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
	for(lp = Lq;lp != NULL;lp = lp1){
		lp1 = lp->next;
		free(lp);
	}
	Lq = NULL;
	for(ld = Ld;ld != NULL;ld = ld1){
		ld1 = ld->next;
		free(ld);
	}
	Ld = NULL;
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
		printf("%s", Notval);
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
	struct ax25_cb *axp;

	if(argc < 2){
		printf("   &AXCB Rcv-Q Unack  Rt  Srtt Mod State          Remote\n");
		for(axp = Ax25_cb;axp != NULL; axp = axp->next){
			printf("%08lx %5u%c%3u/%u%c %2d%6lu %3d %-13s  %s\n",
			 (long) axp,
			 axservice_pending(axp),
			 axp->flags.rnrsent ? '*' : ' ',
			 axp->unack,
			 axp->maxframe,
			 axp->flags.remotebusy ? '*' : ' ',
			 axp->retries,
			 (unsigned long)axp->srt,
			 axp->mmask + 1,
			 Ax25states[axp->state],
			 ax25hdr_to_string(&axp->hdr));
			/* A session that never leaves the node is shown, and
			 * shown as what it is: it has no port, no timers and
			 * no sequence numbers, so the figures on its line say
			 * nothing.  What matters is that it is here at all -
			 * a sysop looking for something that is stuck should
			 * not have to guess that a whole class of sessions is
			 * invisible.
			 */
			if(axp->loop != NULL)
				printf("%*s(local, no port)\n", 47, "");
		}
		if (Axserver_enabled)
			printf("                                Listening      *\n");
		return 0;
	}
	axp = (struct ax25_cb *)ltop(htol(argv[1]));
	if(!ax25val(axp)){
		printf("%s", Notval);
		return 1;
	}
	st_ax25(axp);
	return 0;
}
/* Dump one control block */
void
st_ax25(
struct ax25_cb *axp)
{
	char tmp[AXBUF];

	if(axp == NULL)
		return;
	printf("    &AXB Remote   RB V(S) V(R) Unack P Retry State\n");

	printf("%08lx %-9s%c%c",(long)axp,pax25(tmp,axp->hdr.dest),
	 axp->flags.rejsent ? 'R' : ' ',
	 axp->flags.remotebusy ? 'B' : ' ');
	printf(" %4d %4d",axp->vs,axp->vr);
	printf(" %02u/%02u %u",axp->unack,axp->maxframe,axp->proto);
	printf(" %02u/%02u",axp->retries,axp->n2);
	printf(" %s\n",Ax25states[axp->state]);
	printf("modulo %u\n",axp->mmask + 1);

	printf("srtt = %lu mdev = %lu ",(unsigned long)axp->srt,
	 (unsigned long)axp->mdev);
	printf("T1: ");
	if(run_timer(&axp->t1))
		printf("%lu",(unsigned long)read_timer(&axp->t1));
	else
		printf("stop");
	printf("/%lu ms; ",(unsigned long)dur_timer(&axp->t1));

	printf("T3: ");
	if(run_timer(&axp->t3))
		printf("%lu",(unsigned long)read_timer(&axp->t3));
	else
		printf("stop");
	printf("/%lu ms\n",(unsigned long)dur_timer(&axp->t3));

	printf("T4: ");
	if(run_timer(&axp->t4))
		printf("%lu",(unsigned long)read_timer(&axp->t4));
	else
		printf("stop");
	printf("/%lu ms; ",(unsigned long)dur_timer(&axp->t4));

	printf("T5: ");
	if(run_timer(&axp->t5))
		printf("%lu",(unsigned long)read_timer(&axp->t5));
	else
		printf("stop");
	printf("/%lu ms\n",(unsigned long)dur_timer(&axp->t5));
#ifdef	AX25_VJCOMP
        /* MW: dump VJ statistics if any */
        if (axp->slcomp) {
                printf("VJ input: ");
                slhc_i_status(axp->slcomp);
                printf("VJ outpt: ");
                slhc_o_status(axp->slcomp);
        }
#endif
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
	int r = setintrc(&Digipeat,"Digipeat",argc,argv,0,2);
	const char *port;

	/* Andersherum dieselbe Warnung wie in ifdama(): ein DAMA-Master braucht
	 * digipeat 2, weil nur ein selbst aufgebauter Link das DAMA-Bit traegt.
	 */
	if (!r && argc > 1 && Digipeat != 2 &&
	    (port = dama_master_port()) != NULL)
		printf("warning - %s is a DAMA master and needs digipeat 2\n"
		       "  Repeated verbatim, the called station gets an unmarked "
		       "connect, answers\n  without DAMA, and the session misses "
		       "the time slots.\n", port);
	return r;
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
	int i;
	int r;

	i = (int) Axversion;
	r = setintrc(&i,"AX25 version",argc,argv,1,2);
	Axversion = (enum lapb_version) i;
	return r;
}

static int
dot1(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T1init,"Retry timer (ms)",argc,argv,1,0x7fffffff);
}

static int
dot2(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T2init,"Acknowledge timer (ms)",argc,argv,1,0x7fffffff);
}

/* Set idle poll timer */
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

/* Set idle disconnect timer */
static int
dot5(
int argc,
char *argv[],
void *p)
{
	return setintrc(&T5init,"Idle disconnect timer (ms)",argc,argv,0,0x7fffffff);
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
		printf("%s", Notval);
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

/* The window for modulo-128 links.  Its own setting rather than a wider range
 * on "maxframe", because the two bound different links and raising one should
 * not quietly raise the other.  63 is what the seven-bit sequence number
 * allows with room to tell a full window from an empty one.
 */
static int
doemaxframe(
int argc,
char *argv[],
void *p)
{
	return setintrc(&EMaxframe,"Window size, modulo-128 (frames)",argc,argv,1,63);
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

#ifdef	AX25_VJCOMP
    { "add",  dorouteadd,  0, 3, "ax25 route add [permanent] [vj] <interface> default|<path>" },
#else
    { "add",  dorouteadd,  0, 3, "ax25 route add [permanent] <interface> default|<path>" },
#endif
    /* KEINE Usage-Zeile: der Parser faengt "?" ab, sobald eine da ist, und
     * dann kommt doroutelist() gar nicht mehr zum Zug - "?" soll aber die
     * Legende zeigen, nicht eine Zeile ueber sich selbst.
     */
    { "list", doroutelist, 0, 0, NULL },
    { "stat", doroutestat, 0, 0, NULL },

    { NULL,   NULL,        0, 0, NULL }
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
#ifdef	AX25_VJCOMP
  int vj;
  struct ax_route *axrp;
#endif
  struct ax25 hdr, hdr1;
  struct iface *iface;

  argc--;
  argv++;

  if ((perm = !strcmp(*argv, "permanent"))) {
    argc--;
    argv++;
  }

#ifdef	AX25_VJCOMP
  if ((vj = !strcmp(*argv, "vj"))) {
    argc--;
    argv++;
  }
#endif

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
#ifdef	AX25_VJCOMP
    printf("Usage: ax25 route add [permanent] <interface> [vj] default|<path>\n");
#else
    printf("Usage: ax25 route add [permanent] <interface> default|<path>\n");
#endif
    return 1;
  }

  if (!strcmp(*argv, "default")) {
    /* "default" names an INTERFACE and nothing else - there is no default
     * path, and there never was.  Anything written after it used to be
     * dropped without a word, so "ax25 route add axudp default igate" read
     * as though it had set a digipeater for everything and had set none.
     * Refused rather than half done: what was meant is a different line.
     */
    if (argc > 1) {
      printf("\"default\" is an interface, not a path - there is no default\n"
	     "digipeater.  Leave \"%s\" out, or write the path instead of\n"
	     "\"default\" to route one destination that way\n", argv[1]);
      return 1;
    }
    Axroute_default_ifp = iface;
    return 0;
  }

  if (ax25args_to_hdr(argc, argv, &hdr))
    return 1;

  memset(&hdr1, 0, sizeof(struct ax25));
  hdr1.nextdigi = hdr1.ndigis = hdr.ndigis;
  addrcp(hdr1.source, hdr.dest);
  for (i = 0, j = hdr.ndigis - 1; j >= 0; i++, j--)
    addrcp(hdr1.digis[i], hdr.digis[j]);

  axroute_add(iface, &hdr1, perm);
#ifdef	AX25_VJCOMP
  axrp = ax_routeptr(hdr.dest, 1);
			    // ^ dl9sau bugfix: 1, not 0
  axrp->vjcomp = vj;
#endif
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
	int eax25;
	const uint8 *mac;
#ifdef	AX25_VJCOMP
	int vjcomp;
#endif
	struct ax_route *rp_stack[20];
	struct iface *ifp = 0;
	struct tm *tm;

	tm = localtime((time_t *) &rp->time);
	pax25(cp = buf, rp->target);
	perm = rp->perm;
	jumpstart = rp->jumpstart;
	eax25 = rp->eax25;
	mac = rp->mac_ifp ? rp->mac : NULL;
#ifdef	AX25_VJCOMP
	vjcomp = rp->vjcomp;
#endif
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
	/* E: modulo-128 worked with him.  e: it did not, and we will not ask
	 * again until his route ages out or he calls us with a SABME himself.
	 * Blank: never tried.
	 */
#ifdef	AX25_VJCOMP
	printf("%2d-%.3s  %02d:%02d  %-9s  %c%c%c%c %s\n",
#else
	printf("%2d-%.3s  %02d:%02d  %-9s  %c%c%c %s\n",
#endif
	       tm->tm_mday,
	       &"JanFebMarAprMayJunJulAugSepOctNovDec"[3 * tm->tm_mon],
	       tm->tm_hour,
	       tm->tm_min,
	       ifp ? ifp->name : "???",
	       perm ? 'P' : ' ',
	       jumpstart ? 'J' : ' ',
	       eax25 == AXR_EAX25_YES ? 'E' :
	       eax25 == AXR_EAX25_NO  ? 'e' : ' ',
#ifdef	AX25_VJCOMP
               vjcomp ? 'C' : ' ',
#endif
	       buf);
	/* The ethernet card we learned for him on a BPQether port.  BEHIND
	 * the path and only when there is one: the columns in front are read
	 * by position - the flags sit at 27 to 30 - and most routes have no
	 * card to show, so a column of its own would be empty nearly always.
	 */
	if (mac)
		printf("%*s(%02x:%02x:%02x:%02x:%02x:%02x)\n", 31, "",
		       mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* Was die Buchstaben ueber der Liste bedeuten.  "ax25 route list ?" sagte
 * bisher "*** Not in table *** ?" - richtig, aber niemand fragt einen Knoten
 * nach einer Station namens Fragezeichen.
 */

static void
doroutelegend(void)
{
  puts("");
  puts("Header description:");
  puts("  P  permanent - entered by hand, and not replaced by what we hear");
  puts("  J  jumpstart - the service behind the call starts as soon as the link");
  puts("     stands, rather than when the caller first types something.  A");
  puts("     mailbox wants to greet, and cannot if it is not running yet.");
  puts("  E  modulo 128 (EAX25) worked with him");
  puts("  e  he refused it, and we do not ask again until the route ages out or");
  puts("     he calls us with a SABME himself.  Neither letter: never tried.");
#ifdef	AX25_VJCOMP
  puts("  C  VJ TCP header compression is on for this route");
#endif
}

static int
doroutelist(
int argc,
char *argv[],
void *p)
{

  uint8 call[AXALEN];
  int i;
  struct ax_route *rp;

#ifdef	AX25_VJCOMP
  puts("Date    Time   Interface  PJEC Path");
#else
  puts("Date    Time   Interface  PJE Path");
#endif
  if(argc < 2) {
    for (i = 0; i < AXROUTESIZE; i++)
      for (rp = Ax_routes[i]; rp; rp = rp->next) doroutelistentry(rp);
    return 0;
  }
  argc--;
  argv++;
  for (; argc > 0; argc--, argv++)
    if(!strcmp(*argv, "?"))
      doroutelegend();
    else if(setcall(call, *argv) || !(rp = ax_routeptr(call, 0)))
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

  memset(ifptable, 0, sizeof(ifptable));
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
doaxigntos(
int argc,
char *argv[],
void *p)
{
	return setbool(&Axigntos,"Ignore TOS",argc,argv);
}
static int
dojumpstart(
int argc,
char *argv[],
void *p)
{

	char *cp;
	char buf[32];
	uint8 tmp[AXBUF];
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
