/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/sntp.c,v 1.9 1994/10/09 08:22:58 deyke Exp $ */

/* Simple Network Time Protocol (SNTP) (see RFC1361) */

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "udp.h"
#include "timer.h"
#include "netuser.h"
#include "cmdparse.h"
#include "session.h"

#include "configure.h"

#if defined __hpux
int adjtime(struct timeval *delta, struct timeval *olddelta);
#endif

#define NTP_MIN_PACKET_SIZE     48
#define NTP_PACKET_SIZE         60

#define NTP_MAXSTRATUM  15

#define LEAP_NOWARNING  0       /* Normal, no leap second warning */
#define LEAP_ADDSECOND  1       /* Last minute of day has 61 seconds */
#define LEAP_DELSECOND  2       /* Last minute of day has 59 seconds */
#define LEAP_NOTINSYNC  3       /* Overload, clock is free running */

#define MODE_UNSPEC     0       /* Unspecified (probably old NTP version) */
#define MODE_ACTIVE     1       /* Symmetric active */
#define MODE_PASSIVE    2       /* Symmetric passive */
#define MODE_CLIENT     3       /* Client mode */
#define MODE_SERVER     4       /* Server mode */
#define MODE_BROADCAST  5       /* Broadcast mode */
#define MODE_CONTROL    6       /* Control mode packet */
#define MODE_PRIVATE    7       /* Implementation defined function */

#define TIMEBIAS        2208988800UL
#define USEC2F          4294.967296

struct fp {
	long i;
	unsigned long f;
};

struct sys {
	unsigned char leap;
	unsigned char stratum;
	signed char precision;
	struct fp rho;                  /* (1 << precision) */
	struct fp rootdelay;
	struct fp rootdispersion;
	int32 refid;
	struct fp reftime;
};

struct peer {
	struct socket fsocket;
	struct udp_cb *ucb;
	struct timer timer;
	struct fp xmt;
	int sent;
	int rcvd;
	int accpt;
	int steps;
	int adjts;
	unsigned char stratum;
	struct fp offset;
	struct fp delay;
	double mindelay;
	struct peer *next;
};

struct pkt {
	unsigned char leap;
	unsigned char version;
	unsigned char mode;
	unsigned char stratum;
	signed char poll;
	signed char precision;
	struct fp rootdelay;
	struct fp rootdispersion;
	int32 refid;
	struct fp reftime;
	struct fp org;
	struct fp rec;
	struct fp xmt;
	int keyid;
	char check[8];
};

static struct sys sys = {
	LEAP_NOWARNING,                         /* leap */
	1,                                      /* stratum */
	-10,                                    /* precision */
	{ 0x00000000, 0x00400000 },             /* rho */
	{ 0, 0 },                               /* rootdelay */
	{ 0, 0 },                               /* rootdispersion */
	('U'<<24)|('N'<<16)|('I'<<8)|'X',       /* refid */
	{ 0, 0 }                                /* reftime */
};

static const struct fp Zero = { 0, 0 };
static const struct fp One  = { 1, 0 };

/* Do NOT convert to #define because of bugs in Sun's optimizer */
static const double FACTOR32 = 4294967296.0;

static int Ntrace;
static int Step_threshold = 1;
static struct peer *Peers;
static struct udp_cb *Server_ucb;

/*---------------------------------------------------------------------------*/

#define fpiszero(fp) \
	((fp).i == 0 && (fp).f == 0)

/*---------------------------------------------------------------------------*/

#define fpiseq(fp1, fp2) \
	((fp1).i == (fp2).i && (fp1).f == (fp2).f)

/*---------------------------------------------------------------------------*/

#define fpisne(fp1, fp2) \
	((fp1).i != (fp2).i || (fp1).f != (fp2).f)

/*---------------------------------------------------------------------------*/

#define fpisge(fp1, fp2) \
	((fp1).i > (fp2).i || ((fp1).i == (fp2).i && (fp1).f >= (fp2).f))

/*---------------------------------------------------------------------------*/

static struct fp fpneg(struct fp fp)
{
	if (!fp.f) {
		fp.i = -fp.i;
	} else {
		fp.i = ~fp.i;
		fp.f = -fp.f;
	}
	return fp;
}

/*---------------------------------------------------------------------------*/

#define fpabs(fp) \
	(((fp).i < 0) ? fpneg(fp) : (fp))

/*---------------------------------------------------------------------------*/

static struct fp fpadd(struct fp fp1, struct fp fp2)
{

	unsigned long l;
	unsigned short s;

	s = (unsigned short) (l = (fp1.f & 0xffff) + (fp2.f & 0xffff));
	l = (l >> 16) + (fp1.f >> 16) + (fp2.f >> 16);
	fp1.f = (l << 16) | s;

	fp1.i = (l >> 16) + fp1.i + fp2.i;

	return fp1;
}

/*---------------------------------------------------------------------------*/

static struct fp fpsub(struct fp fp1, struct fp fp2)
{

	unsigned long l;
	unsigned short s;

	if (!fp2.f) {
		fp2.i = -fp2.i;
	} else {
		fp2.i = ~fp2.i;
		fp2.f = -fp2.f;
	}

	s = (unsigned short) (l = (fp1.f & 0xffff) + (fp2.f & 0xffff));
	l = (l >> 16) + (fp1.f >> 16) + (fp2.f >> 16);
	fp1.f = (l << 16) | s;

	fp1.i = (l >> 16) + fp1.i + fp2.i;

	return fp1;
}

/*---------------------------------------------------------------------------*/

static struct fp fpshift(struct fp fp, int n)
{
	while (n)
		if (n > 0) {
			fp.i <<= 1;
			if (fp.f & 0x80000000) fp.i |= 1;
			fp.f <<= 1;
			n--;
		} else {
			fp.f >>= 1;
			if (fp.i & 1) fp.f |= 0x80000000;
			fp.i >>= 1;
			n++;
		}
	return fp;
}

/*---------------------------------------------------------------------------*/

static struct fp double2fp(double d)
{
	struct fp fp;

	if (d >= 0) {
		fp.i = (long) d;
		fp.f = (unsigned long) ((d - fp.i) * FACTOR32);
	} else {
		d = -d;
		fp.i = (long) d;
		fp.f = (unsigned long) ((d - fp.i) * FACTOR32);
		if (!fp.f) {
			fp.i = -fp.i;
		} else {
			fp.i = ~fp.i;
			fp.f = -fp.f;
		}
	}
	return fp;
}

/*---------------------------------------------------------------------------*/

static double fp2double(struct fp fp)
{
	if (!fp.f) return fp.i;
	if (fp.i >= 0) return fp.i + fp.f / FACTOR32;
	return -((~fp.i) + (-fp.f) / FACTOR32);
}

/*---------------------------------------------------------------------------*/

static struct mbuf *htonntp(const struct pkt *pkt)
{

	struct mbuf *bp;
	unsigned long *p;

	if ((bp = ambufw(NTP_PACKET_SIZE))) {
		bp->cnt = NTP_PACKET_SIZE;
		p = (unsigned long *) bp->data;
		*p++ = htonl(((pkt->leap      & 0x03) << 30) |
			     ((pkt->version   & 0x07) << 27) |
			     ((pkt->mode      & 0x07) << 24) |
			     ((pkt->stratum   & 0xff) << 16) |
			     ((pkt->poll      & 0xff) <<  8) |
			      (pkt->precision & 0xff));
		*p++ = htonl((pkt->rootdelay.i << 16) |
			     (pkt->rootdelay.f >> 16));
		*p++ = htonl((pkt->rootdispersion.i << 16) |
			     (pkt->rootdispersion.f >> 16));
		*p++ = htonl(pkt->refid);
		*p++ = htonl(pkt->reftime.i);
		*p++ = htonl(pkt->reftime.f);
		*p++ = htonl(pkt->org.i);
		*p++ = htonl(pkt->org.f);
		*p++ = htonl(pkt->rec.i);
		*p++ = htonl(pkt->rec.f);
		*p++ = htonl(pkt->xmt.i);
		*p++ = htonl(pkt->xmt.f);
		*p++ = htonl(pkt->keyid);
		memcpy((char *) p, pkt->check, sizeof(pkt->check));
	}
	return bp;
}

/*---------------------------------------------------------------------------*/

static int ntohntp(struct pkt *pkt, struct mbuf **bpp)
{

	int n;
	unsigned long *p;
	unsigned long buf[12];
	unsigned long w;

	n = pullup(bpp, (char *) buf, NTP_MIN_PACKET_SIZE);
	free_p(*bpp);
	*bpp = NULLBUF;
	if (n < NTP_MIN_PACKET_SIZE) return (-1);
	p = buf;
	w = ntohl(*p++);
	pkt->leap = (unsigned char) ((w >> 30) & 0x03);
	pkt->version = (unsigned char) ((w >> 27) & 0x07);
	pkt->mode = (unsigned char) ((w >> 24) & 0x07);
	pkt->stratum = (unsigned char) (w >> 16);
	pkt->poll = (signed char) (w >> 8);
	pkt->precision = (signed char) w;
	w = ntohl(*p++);
	pkt->rootdelay.i = ((signed long) w) >> 16;
	pkt->rootdelay.f = w << 16;
	w = ntohl(*p++);
	pkt->rootdispersion.i = w >> 16;
	pkt->rootdispersion.f = w << 16;
	pkt->refid = ntohl(*p++);
	pkt->reftime.i = ntohl(*p++);
	pkt->reftime.f = ntohl(*p++);
	pkt->org.i = ntohl(*p++);
	pkt->org.f = ntohl(*p++);
	pkt->rec.i = ntohl(*p++);
	pkt->rec.f = ntohl(*p++);
	pkt->xmt.i = ntohl(*p++);
	pkt->xmt.f = ntohl(*p++);
	pkt->keyid = 0;
	memset(pkt->check, 0, sizeof(pkt->check));
	return 0;
}

/*---------------------------------------------------------------------------*/

static void dumpntp(const struct pkt *pkt)
{
	printf("leap %d version %d mode %d stratum %d poll %d precision %d\n",
		pkt->leap, pkt->version, pkt->mode, pkt->stratum, pkt->poll,
		pkt->precision);
	printf("      rootdelay %.3f rootdispersion %.3f refid ",
		fp2double(pkt->rootdelay), fp2double(pkt->rootdispersion));
	if (pkt->stratum == 1) {
		putchar(pkt->refid >> 24);
		putchar(pkt->refid >> 16);
		putchar(pkt->refid >>  8);
		putchar(pkt->refid      );
		putchar('\n');
	} else
		printf("%s\n", resolve_a(pkt->refid, 0));
	printf("      ref %08lx.%08lx = %17.6f\n",
		pkt->reftime.i,
		pkt->reftime.f,
		(unsigned long) pkt->reftime.i + pkt->reftime.f / FACTOR32);
	printf("      org %08lx.%08lx = %17.6f\n",
		pkt->org.i,
		pkt->org.f,
		(unsigned long) pkt->org.i + pkt->org.f / FACTOR32);
	printf("      rec %08lx.%08lx = %17.6f\n",
		pkt->rec.i,
		pkt->rec.f,
		(unsigned long) pkt->rec.i + pkt->rec.f / FACTOR32);
	printf("      xmt %08lx.%08lx = %17.6f\n",
		pkt->xmt.i,
		pkt->xmt.f,
		(unsigned long) pkt->xmt.i + pkt->xmt.f / FACTOR32);
	printf("      keyid %d\n", pkt->keyid);
	fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static struct fp sys_clock(void)
{

	struct fp fp;
	struct timeval tv;

	if (gettimeofday(&tv, (struct timezone *) 0)) return Zero;
	fp.i = TIMEBIAS + tv.tv_sec;
	fp.f = (unsigned long) (USEC2F * tv.tv_usec);
	return fp;
}

/*---------------------------------------------------------------------------*/

static void sntp_server(struct iface *iface, struct udp_cb *ucb, int cnt)
{

	struct fp rec;
	struct mbuf *bp;
	struct pkt pkt;
	struct socket fsocket;

	rec = sys_clock();
	sys.reftime = rec;
	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	if (ntohntp(&pkt, &bp)) return;
	if (Ntrace) {
		printf("recv: ");
		dumpntp(&pkt);
	}
	pkt.leap = sys.leap;
	if (pkt.version < 1 || pkt.version > 3) return;
	if (pkt.mode != MODE_CLIENT) return;
	pkt.mode = MODE_SERVER;
	pkt.stratum = sys.stratum;
	pkt.precision = sys.precision;
	pkt.rootdelay = sys.rootdelay;
	pkt.rootdispersion = fpadd(sys.rootdispersion, sys.rho);
	pkt.refid = sys.refid;
	pkt.reftime = sys.reftime;
	pkt.org = pkt.xmt;
	pkt.rec = rec;
	pkt.keyid = 0;
	memset(pkt.check, 0, sizeof(pkt.check));
	pkt.xmt = sys_clock();
	if ((bp = htonntp(&pkt))) {
		send_udp(&ucb->socket, &fsocket, DELAY, 0, bp, 0, 0, 0);
		if (Ntrace) {
			printf("sent: ");
			dumpntp(&pkt);
		}
	}
}

/*---------------------------------------------------------------------------*/

int sntp0(int argc, char **argv, void *p)
{
	if (Server_ucb) {
		del_udp(Server_ucb);
		Server_ucb = 0;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

int sntp1(int argc, char **argv, void *p)
{
	struct socket lsocket;

	if (!Server_ucb) {
		lsocket.address = INADDR_ANY;
		lsocket.port = IPPORT_NTP;
		Server_ucb = open_udp(&lsocket, sntp_server);
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static void sntp_client_recv(struct iface *iface, struct udp_cb *ucb, int cnt)
{

	double pdelay;
	double poffset;
	struct fp abs_offset;
	struct fp now;
	struct fp rec;
	struct fp xmt;
	struct mbuf *bp;
	struct peer *peer;
	struct pkt pkt;
	struct socket fsocket;
	struct timeval tv;

	rec = sys_clock();
	peer = (struct peer *) ucb->user;
	xmt = peer->xmt;
	peer->xmt = Zero;
	peer->rcvd++;
	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	if (ntohntp(&pkt, &bp)) return;
	if (Ntrace) {
		printf("recv: ");
		dumpntp(&pkt);
	}
	if (pkt.leap == LEAP_NOTINSYNC) return;
	if (!pkt.stratum || pkt.stratum > NTP_MAXSTRATUM) return;
	if (fpiszero(pkt.org)) {
		if (fpiszero(xmt)) return;
		pkt.org = xmt;
	}
	if (fpiszero(pkt.rec)) pkt.rec = pkt.xmt;
	if (fpiszero(pkt.xmt)) return;

	peer->stratum = pkt.stratum;
	peer->delay = fpsub(fpadd(rec, pkt.rec), fpadd(pkt.org, pkt.xmt));
	peer->offset = fpshift(fpsub(fpadd(pkt.rec, pkt.xmt), fpadd(pkt.org, rec)), -1);
	peer->accpt++;

	pdelay = fp2double(peer->delay);
	poffset = fp2double(peer->offset);

	if (Ntrace)
		printf("Delay = %.3f  Offset = %.3f\n", pdelay, poffset);

	peer->mindelay = (peer->mindelay * 15.0 + pdelay) / 16.0;
	if (fpisge(pkt.rec, pkt.org) &&
	    fpisge(rec,     pkt.xmt) &&
	    pdelay >= peer->mindelay) return;
	peer->mindelay = pdelay;

	abs_offset = fpabs(peer->offset);
	if (abs_offset.i < Step_threshold) {
#if HAS_ADJTIME || defined __hpux
		tv.tv_sec = peer->offset.i;
		tv.tv_usec = (long) (peer->offset.f / USEC2F);
		if (!adjtime(&tv, (struct timeval *) 0)) {
			peer->adjts++;
			if (Ntrace) printf("Clock adjusted\n");
		} else {
			if (Ntrace) perror("adjtime()");
		}
#endif
		return;
	}
	if (gettimeofday(&tv, (struct timezone *) 0)) return;
	now = fpadd(sys_clock(), peer->offset);
	tv.tv_sec = now.i - TIMEBIAS;
	tv.tv_usec = (long) (now.f / USEC2F);
	if (!settimeofday(&tv, (struct timezone *) 0)) {
		peer->steps++;
		if (Ntrace) printf("Clock stepped\n");
	} else {
		if (Ntrace) perror("settimeofday()");
	}
}

/*---------------------------------------------------------------------------*/

static void sntp_client_send(void *arg)
{

	struct mbuf *bp;
	struct peer *peer;
	struct pkt pkt;

	peer = (struct peer *) arg;
	start_timer(&peer->timer);
	memset((char *) & pkt, 0, sizeof(pkt));
	pkt.leap = LEAP_NOTINSYNC;
	pkt.version = 1;
	pkt.mode = MODE_CLIENT;
	pkt.poll = 6;
	pkt.precision = -6;
	pkt.rootdelay = One;
	pkt.rootdispersion = One;
	pkt.xmt = peer->xmt = sys_clock();
	if ((bp = htonntp(&pkt))) {
		send_udp(&peer->ucb->socket, &peer->fsocket, DELAY, 0, bp, 0, 0, 0);
		peer->sent++;
		if (Ntrace) {
			printf("sent: ");
			dumpntp(&pkt);
		}
	}
}

/*---------------------------------------------------------------------------*/

static int dosntpadd(int argc, char **argv, void *p)
{

	int interval;
	int32 addr;
	struct peer *peer;
	struct socket lsocket;

	if (!(addr = resolve(argv[1]))) {
		printf(Badhost, argv[1]);
		return 1;
	}
	interval = (argc < 3) ? 3333 : atoi(argv[2]);
	if (interval <= 0) interval = 3333;
	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	peer = (struct peer *) calloc(1, sizeof(struct peer));
	if (!peer) {
		printf(Nospace);
		return 1;
	}
	peer->fsocket.address = addr;
	peer->fsocket.port = IPPORT_NTP;
	peer->ucb = open_udp(&lsocket, sntp_client_recv);
	if (!peer->ucb) {
		free(peer);
		return 1;
	}
	peer->ucb->user = (int) peer;
	peer->timer.func = sntp_client_send;
	peer->timer.arg = peer;
	set_timer(&peer->timer, interval * 1000L);
	peer->next = Peers;
	Peers = peer;
	sntp_client_send(peer);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpdrop(int argc, char **argv, void *p)
{

	int32 addr;
	struct peer **pp;
	struct peer *peer;

	if (!(addr = resolve(argv[1]))) {
		printf(Badhost, argv[1]);
		return 1;
	}
	for (pp = &Peers; (peer = *pp); pp = &peer->next)
		if (peer->fsocket.address == addr) {
			*pp = peer->next;
			del_udp(peer->ucb);
			stop_timer(&peer->timer);
			free(peer);
			break;
		}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpstat(int argc, char **argv, void *p)
{
	struct peer *peer;

	printf("Server            St Poll  Sent  Rcvd Accpt Steps Adjts    Delay   Offset\n");
	for (peer = Peers; peer; peer = peer->next)
		printf("%-17s %2d %4ld %5d %5d %5d %5d %5d %8.3f %8.3f\n",
			resolve_a(peer->fsocket.address, 0),
			peer->stratum,
			dur_timer(&peer->timer) / 1000L,
			peer->sent,
			peer->rcvd,
			peer->accpt,
			peer->steps,
			peer->adjts,
			fp2double(peer->delay),
			fp2double(peer->offset));
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpstep_threshold(int argc, char **argv, void *p)
{
	return setint(&Step_threshold, "sntp step_threshold", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int dosntpsysleap(int argc, char **argv, void *p)
{
	int i;

	i = sys.leap;
	setint(&i, "sntp sys leap", argc, argv);
	sys.leap = i & 3;
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysprecision(int argc, char **argv, void *p)
{
	int i;

	i = sys.precision;
	setint(&i, "sntp sys precision", argc, argv);
	sys.precision = (signed char) i;
	sys.rho = fpshift(One, sys.precision);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysrefid(int argc, char **argv, void *p)
{

	char *cp;
	int i;
	int32 addr;

	if (argc < 2) {
		printf("sntp sys refid: ");
		if (sys.stratum == 1) {
			putchar(sys.refid >> 24);
			putchar(sys.refid >> 16);
			putchar(sys.refid >>  8);
			putchar(sys.refid      );
			putchar('\n');
		} else
			printf("%s\n", resolve_a(sys.refid, 0));
		return 0;
	}

	if (sys.stratum == 1) {
		cp = argv[1];
		for (i = 0; i < 4; i++) {
			sys.refid = (sys.refid << 8) | (*cp & 0xff);
			if (*cp) cp++;
		}
	} else {
		if (!(addr = resolve(argv[1]))) {
			printf(Badhost, argv[1]);
			return 1;
		}
		sys.refid = addr;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysreftime(int argc, char **argv, void *p)
{
	sys.reftime = sys_clock();
	printf("sntp sys reftime: %08lx.%08lx = %.6f\n",
		sys.reftime.i,
		sys.reftime.f,
		(unsigned long) sys.reftime.i + sys.reftime.f / FACTOR32);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysrootdelay(int argc, char **argv, void *p)
{
	double d;

	if (argc < 2) {
		printf("sntp sys rootdelay: %.6f\n", fp2double(sys.rootdelay));
		return 0;
	}

	if (sscanf(argv[1], "%lf", &d) == 1)
		sys.rootdelay = double2fp(d);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysrootdispersion(int argc, char **argv, void *p)
{
	double d;

	if (argc < 2) {
		printf("sntp sys rootdispersion: %.6f\n", fp2double(sys.rootdispersion));
		return 0;
	}

	if (sscanf(argv[1], "%lf", &d) == 1)
		sys.rootdispersion = double2fp(d);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsysstratum(int argc, char **argv, void *p)
{
	int i;

	i = sys.stratum;
	setint(&i, "sntp sys stratum", argc, argv);
	sys.stratum = i;
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpsys(int argc, char **argv, void *p)
{

	static struct cmds sntpsyscmds[] = {

		"leap", dosntpsysleap, 0, 0, NULLCHAR,
		"precision", dosntpsysprecision, 0, 0, NULLCHAR,
		"refid", dosntpsysrefid, 0, 0, NULLCHAR,
		"reftime", dosntpsysreftime, 0, 0, NULLCHAR,
		"rootdelay", dosntpsysrootdelay, 0, 0, NULLCHAR,
		"rootdispersion", dosntpsysrootdispersion, 0, 0, NULLCHAR,
		"stratum", dosntpsysstratum, 0, 0, NULLCHAR,

		NULLCHAR, NULLFP, 0, 0, NULLCHAR
	};

	int i;

	if (argc < 2) {
		for (i = 0; sntpsyscmds[i].func; i++)
			sntpsyscmds[i].func(0, 0, 0);
		return 0;
	}

	return subcmd(sntpsyscmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int dosntptrace(int argc, char **argv, void *p)
{
	return setbool(&Ntrace, "sntp trace", argc, argv);
}

/*---------------------------------------------------------------------------*/

int dosntp(int argc, char **argv, void *p)
{
	static struct cmds sntpcmds[] = {

		"add", dosntpadd, 0, 2, "sntp add <server> [<interval>]",
		"drop", dosntpdrop, 0, 2, "sntp drop <server>",
		"status", dosntpstat, 0, 0, NULLCHAR,
		"step_threshold", dosntpstep_threshold, 0, 0, NULLCHAR,
		"sys", dosntpsys, 0, 0, NULLCHAR,
		"trace", dosntptrace, 0, 0, NULLCHAR,

		NULLCHAR, NULLFP, 0, 0, NULLCHAR
	};

	return subcmd(sntpcmds, argc, argv, p);
}

