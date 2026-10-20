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
#include "pktdrvr.h"
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
static int dodigikeepad(int argc,char *argv[],void *p);
static int dodigiout(int argc,char *argv[],void *p);
static int doax25v2(int argc,char *argv[],void *p);
static int doax25version(int argc,char *argv[],void *p);
static int doxidprobe(int argc,char *argv[],void *p);
static int doxidretries(int argc,char *argv[],void *p);
static int doxidexpire(int argc,char *argv[],void *p);
static int doxidinherit(int argc,char *argv[],void *p);
static int doxidteach(int argc,char *argv[],void *p);
static int domaxframe(int argc,char *argv[],void *p);
static int doemaxframe(int argc,char *argv[],void *p);
static int domycall(int argc,char *argv[],void *p);
static int don2(int argc,char *argv[],void *p);
static int dopaclen(int argc,char *argv[],void *p);
static int dopthresh(int argc,char *argv[],void *p);
static int dosegmentation(int argc,char *argv[],void *p);
static int dot1(int argc,char *argv[],void *p);
static int dot2(int argc,char *argv[],void *p);
static int dot3(int argc,char *argv[],void *p);
static int dot4(int argc,char *argv[],void *p);
static int dot5(int argc,char *argv[],void *p);
static int doaxloop(int argc,char *argv[],void *p);
static int dorouteadd(int argc,char *argv[],void *p);
static int doroutedel(int argc,char *argv[],void *p);
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
{ "digi-keep-path", dodigikeepad, 0, 0,
  "ax digi-keep-path on|off\n"
  "  on:  keep the whole digi path in header (path transparency, for APRS)\n"
  "  off: strip passed digis, add next-hop from routing table (classic behaviour)\n"
          "       in the header (the already-passed digis are NOT stripped) -\n"
          "       path transparency, for APRS and fill-in digis.\n"
          "  off: strip the passed digis, add next-hop digis from the routing\n"
          "       table (the classic behaviour).\n"
          "  This sets the global default. Defaults to 'normal'. You can fine-tune\n"
          "  it per interface, i.e. with 'ifconfig <iface> digi-keep-path on'.\n"
          "  The port the frame ARRIVED on does not matter: the decision is\n"
          "  taken on the outgoing port, so what leaves an APRS port stays\n"
          "  transparent wherever it came from.\n" },
        { "digiout",      dodigiout,      0, 0,
          "ax25 digiout [normal|same-iface]\n"
          "  normal:     choose outgoing interface & next-hop digis from routing tables\n"
          "              (classic behaviour)\n"
          "  same-iface: keep frame on arrived interface, path untouched - fill-in\n"
          "              digipeater\n"
          "  This sets the global default. Defaults to 'normal'. You can fine-tune it per\n"
          "  interface, i.e. with 'ifconfig <iface> digiout same-iface.'\n"
        },
  { "ax25-v2.2",    doax25v2,       0, 1,
	  "ax25 ax25-v2.2 [off|accept|initiate|always]\n"
	  "  Set the global factory default for AX.25-2.2 (inherited by new ax25\n"
	  "  ports; override per port with 'ifconfig <iface> ax25-v2.2').\n"
	  "  off      never send or invite XID; force pure SABM negotiation.\n"
	  "  accept   answer XID probes, do not send our own.\n"
	  "  initiate also send XID first (negotiate the best common denominator).\n"
	  "  always   like 'initiate', and do not refuse on a fresh silent 'no'.\n"
	},
	{ "xid-probe-time", doxidprobe,    0, 1,
	  "ax25 xid-probe-time <minutes>\n"
	  "  Set the global factory default for how long a station that\n"
	  "  did not answer an AX.25-2.2 XID (capabilities exchange)\n"
	  "  keeps its \"no XID\" verdict.  After that time, a new connect\n"
	  "  may probe again.  A fresh FRMR or SABM verdict resets it.\n"
	  "  Default: 60 minutes."
	},
	{ "xid-retries",   doxidretries,   0, 1,
	  "ax25 xid-retries <n>\n"
	  "  How many connection attempts in a row repeat an unanswered\n"
	  "  XID probe before only 'xid-probe-time' separates two probes.\n"
	  "  A lost question on HF should not cost a whole hour; after\n"
	  "  these tries the station is treated as silent.  Default: 3."
	},
	{ "xid-expire-time", doxidexpire,  0, 1,
	  "ax25 xid-expire-time <minutes>\n"
	  "  How long a station that ANSWERED an XID keeps that answer -\n"
	  "  its N1, k, SREJ and segmenter - before we negotiate afresh.\n"
	  "  A measured capability can change, but not every minute.\n"
	  "  Default: 2160 minutes (1.5 days)."
	},
	{ "xid-inherit-hop2hop", doxidinherit, 0, 1,
	  "ax25 xid-inherit-hop2hop [on|off]\n"
	  "  Whether a nexthop that acks the hop in its XID (the hop2hop\n"
	  "  bit, doc/EAX25.md) hands its channel parameters down to every\n"
	  "  route we hold THROUGH it: a connect down such a path then\n"
	  "  negotiates nothing (doc/AX25-2.2-GAP.md).  Only the\n"
	  "  capabilities are inherited, never the bit itself.\n"
	  "  Default: on."
	},
	{ "xid-teach-hop2hop", doxidteach, 0, 1,
	  "ax25 xid-teach-hop2hop [on|off]\n"
	  "  Whether a neighbour that asks THROUGH us - and so does not know\n"
	  "  us as a hop2hop nexthop - is told so at once, with our own XID,\n"
	  "  instead of waiting to be asked itself (doc/AX25-2.2-GAP.md).\n"
	  "  This is the node's only unsolicited frame; it goes out at\n"
	  "  digipeat 2 and only to a direct neighbour.  With off we lose\n"
	  "  the shortcut, not the ability.  Default: on."
	},
	{ "flush",        doaxflush,      0, 0, NULL },
	{ "heard",        doaxheard,      0, 0, NULL },
	{ "ignoretos",    doaxigntos,     0, 0, NULL },
	{ "jumpstart",    dojumpstart,    0, 2, "ax25 jumpstart <call> [ON|OFF]" },
	{ "kick",         doaxkick,       0, 2, "ax25 kick <axcb>" },
	{ "loop-protect", doaxloop,       0, 1,
	  "ax25 loop-protect [s]              window to catch our own echoed UI\n"
	  "       frames, in seconds (0 = off, default 5).  A frame whose bytes\n"
	  "       we just sent and that comes back identical on the same path is\n"
	  "       our echo (bridge, digi, datagram loop) and is dropped.  Only UI:\n"
	  "       connected-mode repeats are protocol timing and never touched." },
	{ "emaxframe",    doemaxframe,    0, 0,
	  "ax25 emaxframe [1..63]                the window on modulo-128 links\n"
	  "       (per port: \"ifconfig <iface> emaxframe\")" },
	{ "maxframe",     domaxframe,     0, 0, NULL },
	{ "mycall",       domycall,       0, 0, NULL },
	{ "paclen",       dopaclen,       0, 0, NULL },
	{ "pid-info",     pid_info,       0, 0,
	  "ax25 pid-info [<name>|<number>]       what a protocol id means\n"
	  "       With no argument the whole table.  These are the names that\n"
	  "       \"pid=\" and \"ifconfig <iface> pid-filter\" accept." },
	{ "pthresh",      dopthresh,      0, 0, NULL },
	{ "reset",        doaxreset,      0, 2, "ax25 reset <axcb>" },
	{ "retry",        don2,           0, 0, NULL },
	{ "route",        doaxroute,      0, 0, NULL },
	{ "segmentation", dosegmentation, 0, 0,
	  "ax25 segmentation off|on|exempt-l3\n"
	  "  Whether the node segments its OWN packets when the MTU is larger\n"
	  "  than paclen.  off: never.  on: every protocol.  exempt-l3: every\n"
	  "  protocol except plain text (PID 0xf0) - the default.\n"
	  "  Received segments are always reassembled, and a repeated frame is\n"
	  "  always segmented to fit the outgoing port; neither is configured\n"
	  "  here.  A port can override this with\n"
	  "  \"ifconfig <iface> segmentation\"." },
	{ "status",       doaxstat,       0, 0, NULL },
	{ "t1",           dot1,           0, 0, NULL },
	{ "t2",           dot2,           0, 0, NULL },
	{ "t3",           dot3,           0, 0, NULL },
	{ "t4",           dot4,           0, 0, NULL },
	{ "t5",           dot5,           0, 0, NULL },
	{ "version",      doax25version,  0, 0, NULL },
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

	/* The SEND counters, all three, and not only "tot" as it was before
	 * ax25sndcnt existed.  Zeroing the total on its own left the sum
	 * rawsndcnt == ipsndcnt + ax25sndcnt false from the next packet on,
	 * which is exactly the one moment a reader checks it - right after
	 * he cleared it and wants to see it grow again.
	 */
	ifp->rawsndcnt = 0;
	ifp->ipsndcnt = 0;
	ifp->ax25sndcnt = 0;
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
			printf("%08lx %5u%c%3u/%u%c %2d%6lu %3d %-13s  %s",
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
			/* Was der Peer in der XID nannte, kappt unser Senden -
			 * nur dann zeigen, wenn es das wirklich tut. */
			if(axp->peer_n1 || axp->peer_k)
				printf("  XID N1rx=%u k=%u",
				 axp->peer_n1, axp->peer_k);
			/* Und sein Segmenter-Bit, sobald es etwas bedeutet:
			 * yes heisst "er kann zusammensetzen", no heisst "ihm
			 * schicken wir keine Segmente" (peer_segment).  Keine
			 * Anzeige, solange er nichts dazu gesagt hat. */
			if(axp->peer_segment)
				printf("  SEG=%s",
				 axp->peer_segment > 0 ? "yes" : "no");
			printf("\n");
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
	if(axp->peer_n1 || axp->peer_k)
		printf("peer XID: N1rx %u bytes, k %u frames (clamp)\n",
		 axp->peer_n1, axp->peer_k);

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
	const char *port;

	/* Andersherum dieselbe ABLEHNUNG wie in ifdama(): ein DAMA-Master
	 * braucht digipeat 2, weil nur ein selbst aufgebauter Link das
	 * DAMA-Bit traegt.  Geprueft wird VOR setintrc(), damit der Wert gar
	 * nicht erst gesetzt und dann zurueckgenommen werden muss - und nur
	 * fuer 0 und 1, damit ein Tippfehler weiterhin die Bereichsmeldung von
	 * setintrc() bekommt und nicht diese hier.
	 */
	if (argc > 1 && (!strcmp(argv[1], "0") || !strcmp(argv[1], "1")) &&
	    (port = dama_master_port()) != NULL) {
		printf("refused - dama mode master needs digi mode 2, and %s is "
		       "a DAMA master\n  Repeated verbatim, the called station "
		       "gets an unmarked connect, answers\n  without DAMA, and "
		       "the session misses the time slots.\n", port);
		return 1;
	}
	return setintrc(&Digipeat,"Digipeat",argc,argv,0,2);
}

/* Control whether a digipeated frame keeps the whole digi path (1) or has
 * the already-passed digipeaters stripped out of it (0, the classic
 * behaviour).  Per port: "ifconfig <iface> digi-keep-path".
 */
static int
dodigikeepad(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("Digi-keep-path: %s\n",
		 Digi_keep_path ? "on" : "off");
		return 0;
	}
	if(!stricmp(argv[1],"on")){
		Digi_keep_path = 1;
		return 0;
	}
	if(!stricmp(argv[1],"off")){
		Digi_keep_path = 0;
		return 0;
	}
	printf("Valid options: on off\n");
	return 1;
}

/* Choose how a digipeated frame finds its way out.  "normal" (the default)
 * consults the AX.25 routing table for both interface and next-hop digis;
 * "same-iface" keeps the frame on the interface it arrived on, path untouched
 * - a fill-in digipeater.  Per port: "ifconfig <iface> digiout".
 */
static int
dodigiout(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("Digiout: %s\n",
		 Digiout == DIGIOUT_SAME ? "same-iface" : "normal");
		return 0;
	}
	if(!stricmp(argv[1],"normal")){
		Digiout = DIGIOUT_ROUTE;
		return 0;
	}
	if(!stricmp(argv[1],"same-iface")){
		Digiout = DIGIOUT_SAME;
		return 0;
	}
	printf("Valid options: normal same-iface\n");
	return 1;
}
/* Set the global factory default of the AX.25 v2.2 knob that new AX.25
 * ports inherit when they are attached.  "ifconfig <iface> ax25-v2.2"
 * overrides it per port; the words mirror the ifconfig syntax so they
 * mean the same everywhere.  Printing it with no argument shows the
 * current default.  "off" means pure AX.25 2.0 behaviour (never send or
 * invite XID).  "accept" answers XID for calls not privately marked
 * AX25V2_OFF; "initiate" also sends XID first.  "always" is like
 * "initiate", but new sessions of a stranger that once answered take the
 * negotiation as given for that route within its lifetime.
 *
 * ES GILT AUCH FUER DIE LAUFENDEN PORTS.  The command changes the default
 * and also pushes it to ports that have no explicit setting (ax25_v2_set
 * in iface.h), so running the global command affects live ports unless
 * the operator explicitly set them per-port.
 */
static int
doax25v2(
int argc,
char *argv[],
void *p)
{
	struct iface *ifp;
	const char *word;
	int value;
	int changed = 0, kept = 0;

	if(argc < 2){
		printf("AX.25 v2.2 factory default: %s\n",
		 Ax25_v2_default == AX25V2_OFF ? "off" :
		 Ax25_v2_default == AX25V2_ACCEPT ? "accept" :
		 Ax25_v2_default == AX25V2_INITIATE ? "initiate" : "always");
		return 0;
	}
	if(!stricmp(argv[1],"off")){
		value = AX25V2_OFF; word = "off";
	} else if(!stricmp(argv[1],"accept")){
		value = AX25V2_ACCEPT; word = "accept";
	} else if(!stricmp(argv[1],"initiate")){
		value = AX25V2_INITIATE; word = "initiate";
	} else if(!stricmp(argv[1],"always")){
		value = AX25V2_ALWAYS; word = "always";
	} else {
		printf("Valid options: off accept initiate always\n");
		return 1;
	}
	Ax25_v2_default = value;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next){
		if(ifp->iftype == NULL || ifp->iftype->type != CL_AX25)
			continue;
		if(ifp->ax25_v2_set){
			kept++;
			continue;
		}
		ifp->ax25_v2 = value;
		changed++;
		printf("ax25: port %s follows the new default: ax25-v2.2 %s\n",
		       ifp->name, word);
	}
	if(changed == 0 && kept != 0)
		printf("ax25: every AX.25 port has its own setting;"
		       " nothing was changed\n");
	return 0;
}
/* Set the global factory default of the XID re-probe interval (in
 * minutes).  A route that did not answer an AX.25-2.2 XID, or whose
 * FRMR verdict says "never ask again", has its knob re-checked when a
 * new connect wants the v2.2 service; this is how often that re-check
 * may happen.  Fresh FRMR or SABM verdicts and per-port and per-route
 * settings reset the clock before it runs out.  The value lives in
 * seconds; 60 minutes of it is the historic default of one probe per
 * hour.
 */
static int
doxidprobe(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("XID re-probe interval: %ld minute%s\n",
		 Xid_probe_time / 60, Xid_probe_time == 60 ? "" : "s");
		return 0;
	}
	{
		long minutes = atol(argv[1]);
		if(minutes < 1){
			printf("XID re-probe time must be at least 1 minute\n");
			return 1;
		}
		Xid_probe_time = minutes * 60;
	}
	return 0;
}
/* Wie oft eine unbeantwortete XID-Frage in Folge wiederholt wird - siehe
 * ax25.h.  Innerhalb der ersten Xid_retries Aufbauten nach einem
 * Verstummen fragt jeder Aufbau von selbst wieder; danach liegt die
 * Frage nur noch Xid_probe_time auseinander.
 */
static int
doxidretries(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("XID probe retries: %d\n", Xid_retries);
		return 0;
	}
	{
		int n = atoi(argv[1]);
		if(n < 0){
			printf("XID retries must not be negative\n");
			return 1;
		}
		Xid_retries = n;
	}
	return 0;
}
/* Wie lange eine BEANTWORTETE XID gilt - siehe ax25.h.  Innerhalb dieser
 * Frist wird der Peer nicht wieder gefragt und seine Grenzen aus der
 * gemerkten Antwort angewandt.
 */
static int
doxidexpire(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("XID expiry time: %ld minute%s\n",
		 Xid_expire_time / 60, Xid_expire_time == 60 ? "" : "s");
		return 0;
	}
	{
		long minutes = atol(argv[1]);
		if(minutes < 1){
			printf("XID expiry time must be at least 1 minute\n");
			return 1;
		}
		Xid_expire_time = minutes * 60;
	}
	return 0;
}
/* Ob ein hop2hop-Nexthop seine Parameter an die Wege durch ihn vererbt -
 * siehe ax25.h und doc/AX25-2.2-GAP.md.  Nur ein Schalter: eine Station
 * verlaesst sich entweder darauf, dass der Nexthop den Hop terminiert,
 * oder sie handelt jeden Fernweg selbst aus (wie vor dieser Erweiterung). */
static int
doxidinherit(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("XID hop2hop inheritance: %s\n",
		 Xid_inherit_hop2hop ? "on" : "off");
		return 0;
	}
	if(!stricmp(argv[1],"on")){
		Xid_inherit_hop2hop = 1;
	} else if(!stricmp(argv[1],"off")){
		Xid_inherit_hop2hop = 0;
	} else {
		printf("Valid options: on off\n");
		return 1;
	}
	return 0;
}
/* Ob wir einem Nachbarn, der DURCH uns hindurch fragt, unser eigenes XID
 * schicken - siehe ax25.h und doc/AX25-2.2-GAP.md.  Das ist der einzige
 * Rahmen, den dieser Knoten ungefragt in die Welt setzt; aus heisst: wir
 * warten, bis er uns selbst anspricht.  Die Faehigkeit selbst haengt nicht
 * daran - die Route merkt sich sein Bit aus seinem XID trotzdem. */
static int
doxidteach(
int argc,
char *argv[],
void *p)
{
	if(argc < 2){
		printf("XID re-teach of a neighbour: %s\n",
		 Xid_teach_hop2hop ? "on" : "off");
		return 0;
	}
	if(!stricmp(argv[1],"on")){
		Xid_teach_hop2hop = 1;
	} else if(!stricmp(argv[1],"off")){
		Xid_teach_hop2hop = 0;
	} else {
		printf("Valid options: on off\n");
		return 1;
	}
	return 0;
}
/* Whether the node segments its own packets - see ax25.h.  off: never;
 * on: every protocol; exempt-l3: every protocol except plain text (PID
 * 0xf0), which is the default.  This is the node default; a port can
 * override it with "ifconfig <iface> segmentation".
 */
static int
dosegmentation(
int argc,
char *argv[],
void *p)
{
	int mode;

	if(argc < 2){
		printf("Segmentation: %s\n", axseg_name(Ax25_segmentation));
		return 0;
	}
	if((mode = axseg_word(argv[1])) == 0){
		printf("Valid options: off on exempt-l3\n");
		return 1;
	}
	Ax25_segmentation = mode;
	return 0;
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
doax25version(
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
    { "del",  doroutedel,  0, 1, "ax25 route del all|<call> [<digis>...]" },
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
  /* UND DIE DATEI MIT, SOFORT.  Der Grund ist derselbe wie bei
   * "ax25 route del": was dem Betreiber eben gesagt wurde, soll beim
   * naechsten Start auch dastehen.  Ohne das schreibt die Datei erst der
   * naechste axroute_savefile()-Anlauf, und ein Eintrag, den jemand
   * gerade mit der Hand gesetzt hat, ist nach einem Neustart einfach
   * weg - bei einem Eintrag, den man sich aus der Konfiguration
   * sparen will, weil der naechste Start sie ohnehin liest, faellt
   * das nicht auf, bei einem nicht auf, wenn man es nicht erwartet.
   * (axroute_flush() ist derselbe Aufruf, den del macht; die
   * Bildunterschrift in ax25file.c sagt genau das und tat es nicht.)
   */
	axroute_flush();
  return 0;
}

/* "ax25 route del all", or "ax25 route del <call> [<digis>...]".
 *
 * OHNE Rueckfrage, und die Datei ist danach SOFORT neu.  Beides aus demselben
 * Grund: die Befehlskanal- und Skript-Antwort von wampes ist das, woran
 * jemand merkt, ob eine Aenderung gewirkt hat - wenn danach "ax25 route list"
 * noch den alten Eintrag zeigt, ist die Aenderung nicht erfolgt, egal was die
 * Meldung eben gesagt hat.  Und eine Datei, die den geloeschten Eintrag noch
 * traegt, holt ihn beim naechsten Start zurueck: der Aufrufer hat ihn
 * vergessen, der Aufrufer der Datei nicht.
 */
static int
doroutedel(
int argc,
char *argv[],
void *p)
{
  struct ax25 hdr;
  const uint8 *digis[MAXDIGIS];
  uint8 call[AXALEN];
  char buf[1024];
  int i, n;

  (void) p;

  argc--;
  argv++;
  if (argc < 1) {
    printf("Usage: ax25 route del all|<call> [<digis>...] | default\n");
    return 1;
  }

  /* DER FANG IST EIN BEFEHL, KEIN RUFZEICHEN.
   *
   * Er lebte bisher nur in Axroute_default_ifp, ohne Weg hinein oder hinaus:
   * "ax25 route add <port> default" setzte ihn, und danach liess er sich
   * weder zeigen ("ax25 route list" kennt nur Ax_routes[]) noch loeschen -
   * "default" fiel durch bis ax25args_to_hdr() und kam als
   * "Invalid call \"default\"" zurueck.  Wer dem Knoten den Fang
   * weggenommen hat, ohne ihn zu nennen, konnte die Aenderung nicht
   * rueckgaengig machen.
   */
  if (!strcmp(*argv, "default")) {
    if (argc > 1) {
      printf("Usage: ax25 route del default\n");
      return 1;
    }
    if (Axroute_default_ifp == NULL) {
      printf("No default route set.\n");
      return 1;
    }
    Axroute_default_ifp = NULL;
    /* Die Datei kennt nur Ax_routes[]; der Fang wurde nie mitgeschrieben,
     * und ihn hier zu flushen wuerde nur andere Eintraege neu schreiben. */
    printf("default route forgotten\n");
    return 0;
  }

  if (!strcmp(*argv, "all")) {
    n = axroute_delall();
    axroute_flush();
    printf("%d route%s forgotten.\n", n, n == 1 ? "" : "s");
    return 0;
  }

  if (ax25args_to_hdr(argc, argv, &hdr))
    return 1;
  addrcp(call, hdr.dest);
  for (i = 0; i < hdr.ndigis; i++)
    digis[i] = hdr.digis[i];

  if (!axroute_del(call, digis, hdr.ndigis)) {
    pax25(buf, call);
    printf("*** Not in table *** %s", buf);
    for (i = 0; i < hdr.ndigis; i++) {
      pax25(buf, digis[i]);
      printf(" via %s", buf);
    }
    printf("\n");
    return 1;
  }

  axroute_flush();
  pax25(buf, call);
  printf("%s forgotten", buf);
  for (i = 0; i < hdr.ndigis; i++) {
    pax25(buf, digis[i]);
    printf(" via %s", buf);
  }
  printf("\n");
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
  /* DER FANG STEHT NICHT IN Ax_routes[].
   *
   * "ax25 route add <port> default" setzt Axroute_default_ifp, eine eigene
   * Variable; die Liste lief bisher nur ueber Ax_routes[] und sagte damit
   * nichts ueber ihn.  Wer wissen wollte, wohin ein unbekanntes Ziel geht,
   * musste es aus dem Kopf wissen.  Er steht deshalb als eigene Zeile unter
   * dem Kopf - mit Datum und Zeit leer, damit die Spalten stimmen.  Das Wort
   * sitzt in der Path-Spalte (dort, wo sonst das Ziel steht, Spalte 31), die
   * Flag-Spalte bleibt leer: "default" in der PJEC-Spalte sah aus wie ein
   * Flag.
   */
  if (Axroute_default_ifp)
    printf("%6s  %5s  %-9s       default\n",
	   "", "", Axroute_default_ifp->name);
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
/* Loop-Schutz-Fenster anzeigen/setzen (Sekunden).  Ax_dup_window ist in
 * Millisekunden, die Angabe hier in Sekunden.
 */
static int
doaxloop(
int argc,
char *argv[],
void *p)
{
	long val;

	if(argc < 2){
		printf("Loop protect: %d s", Ax_dup_window / 1000);
		if(Ax_dup_window % 1000)
			printf(" (%d ms)", Ax_dup_window);
		putchar('\n');
		return 0;
	}
	if(cmd_getnum(argv[1],&val) || val < 0 || val > 3600){
		printf("Bad value %s (seconds, 0 = off)\n",argv[1]);
		return 1;
	}
	Ax_dup_window = (int)(val * 1000L);
	printf("Loop protect: %d s\n",Ax_dup_window / 1000);
	return 0;
}
