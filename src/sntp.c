/* @(#) $Id: sntp.c,v 1.19 2016/03/13 07:14:38 dl9sau Exp $ */

/* Simple Network Time Protocol (SNTP) (see RFC1361) */

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
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

#if defined __hpux && !HAS_ADJTIME
int adjtime(const struct timeval *delta, struct timeval *olddelta);
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

/* NTP timestamps and offsets are 32.32 signed fixed point.  These fields have
 * to be exactly 32 bits wide: fpadd()/fpsub() propagate the carry by hand in
 * 16-bit halves and rely on the store into .f dropping bit 32, fpshift() tests
 * bit 31 with 0x80000000, and fpneg()/fp2double() negate .f expecting the
 * result to be 2^32 - f.  With 'long' all of that silently broke on LP64.
 */
struct fp {
	int32 i;
	uint32 f;
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
	int served;                     /* requests answered */
	int refused;                    /* requests dropped, see sntp_server() */
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
	int panics;
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
	{ 0, 0 },                               /* reftime */
	0,                                      /* served */
	0                                       /* refused */
};

static const struct fp Zero = { 0, 0 };
static const struct fp One  = { 1, 0 };

/* Do NOT convert to #define because of bugs in Sun's optimizer */
static const double FACTOR32 = 4294967296.0;

static int Ntrace;
static int Step_threshold = 1;
/* NTP's panic threshold: refuse to set the clock outright by more than this
 * many seconds.  0 disables the check. */
static int Panic_threshold = 1000;
static struct peer *Peers;
static struct udp_cb *Server_ucb;
/* Shortest gap between two answers from the server, in ms */
#define SERVER_MIN_INTERVAL     20
static int32 Lastserved;

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
	uint8 *cp;
	uint32 w;

	/* Built with put32() on a byte pointer, like every other wire format
	 * in this tree.  The old version walked the mbuf with an
	 * "unsigned long *", which on LP64 strides 8 bytes through a protocol
	 * made of 32-bit words: it wrote 112 bytes into a buffer it then
	 * declared to be 60 long, and every field came out followed by four
	 * zero bytes.
	 */
	if ((bp = ambufw(NTP_PACKET_SIZE))) {
		bp->cnt = NTP_PACKET_SIZE;
		cp = bp->data;

		w  = ((uint32) (pkt->leap      & 0x03)) << 30;
		w |= ((uint32) (pkt->version   & 0x07)) << 27;
		w |= ((uint32) (pkt->mode      & 0x07)) << 24;
		w |= ((uint32) (pkt->stratum   & 0xff)) << 16;
		w |= ((uint32) (pkt->poll      & 0xff)) <<  8;
		w |=  (uint32) (pkt->precision & 0xff);
		cp = put32(cp, (int32) w);

		cp = put32(cp, (int32) ((((uint32) pkt->rootdelay.i) << 16) |
					(pkt->rootdelay.f >> 16)));
		cp = put32(cp, (int32) ((((uint32) pkt->rootdispersion.i) << 16) |
					(pkt->rootdispersion.f >> 16)));
		cp = put32(cp, pkt->refid);
		cp = put32(cp, pkt->reftime.i);
		cp = put32(cp, (int32) pkt->reftime.f);
		cp = put32(cp, pkt->org.i);
		cp = put32(cp, (int32) pkt->org.f);
		cp = put32(cp, pkt->rec.i);
		cp = put32(cp, (int32) pkt->rec.f);
		cp = put32(cp, pkt->xmt.i);
		cp = put32(cp, (int32) pkt->xmt.f);
		cp = put32(cp, pkt->keyid);
		memcpy(cp, pkt->check, sizeof(pkt->check));
	}
	return bp;
}

/*---------------------------------------------------------------------------*/

static int ntohntp(struct pkt *pkt, struct mbuf **bpp)
{

	int n;
	uint8 *cp;
	uint8 buf[NTP_MIN_PACKET_SIZE];
	uint32 w;

	/* The old version declared "unsigned long buf[12]" and walked it with
	 * an "unsigned long *".  On LP64 that is 96 bytes read in 8-byte steps
	 * while pullup() only fills 48: every field after the first came from
	 * the wrong offset, and the last six - org, rec and xmt - came from
	 * uninitialised stack.  sntp_server() copies xmt into org of the reply,
	 * so eight bytes of that stack went back out onto the network with
	 * every answer.
	 */
	n = pullup(bpp, buf, NTP_MIN_PACKET_SIZE);
	free_p(bpp);
	if (n < NTP_MIN_PACKET_SIZE) return -1;
	cp = buf;

	w = (uint32) get32(cp); cp += 4;
	pkt->leap = (unsigned char) ((w >> 30) & 0x03);
	pkt->version = (unsigned char) ((w >> 27) & 0x07);
	pkt->mode = (unsigned char) ((w >> 24) & 0x07);
	pkt->stratum = (unsigned char) (w >> 16);
	pkt->poll = (signed char) (w >> 8);
	pkt->precision = (signed char) w;

	w = (uint32) get32(cp); cp += 4;
	pkt->rootdelay.i = ((int32) w) >> 16;   /* signed: rootdelay may be < 0 */
	pkt->rootdelay.f = w << 16;
	w = (uint32) get32(cp); cp += 4;
	pkt->rootdispersion.i = w >> 16;
	pkt->rootdispersion.f = w << 16;

	pkt->refid     = get32(cp); cp += 4;
	pkt->reftime.i = get32(cp); cp += 4;
	pkt->reftime.f = (uint32) get32(cp); cp += 4;
	pkt->org.i     = get32(cp); cp += 4;
	pkt->org.f     = (uint32) get32(cp); cp += 4;
	pkt->rec.i     = get32(cp); cp += 4;
	pkt->rec.f     = (uint32) get32(cp); cp += 4;
	pkt->xmt.i     = get32(cp); cp += 4;
	pkt->xmt.f     = (uint32) get32(cp);

	pkt->keyid = 0;
	memset(pkt->check, 0, sizeof(pkt->check));
	return 0;
}

/*---------------------------------------------------------------------------*/

/* At stratum 1 the reference id is four characters naming the clock source.
 * They come off the wire, so they are not necessarily characters, and they
 * used to go to the terminal one putchar() at a time - escape sequences and
 * all.
 */

static void print_refid(int32 refid)
{
	int i;
	int c;

	for (i = 24; i >= 0; i -= 8) {
		c = (int) ((refid >> i) & 0xff);
		if (c >= ' ' && c < 0x7f)
			putchar(c);
		else
			printf("\\%03o", c);
	}
	putchar('\n');
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
		print_refid(pkt->refid);
	} else
		printf("%s\n", resolve_a(pkt->refid, 0));
	/* NTP timestamps are unsigned seconds since 1900: widen through uint32
	 * so a value past 2038 does not print as a sign-extended mess. */
	printf("      ref %08lx.%08lx = %17.6f\n",
		(unsigned long) (uint32) pkt->reftime.i,
		(unsigned long) pkt->reftime.f,
		(double) (uint32) pkt->reftime.i + pkt->reftime.f / FACTOR32);
	printf("      org %08lx.%08lx = %17.6f\n",
		(unsigned long) (uint32) pkt->org.i,
		(unsigned long) pkt->org.f,
		(double) (uint32) pkt->org.i + pkt->org.f / FACTOR32);
	printf("      rec %08lx.%08lx = %17.6f\n",
		(unsigned long) (uint32) pkt->rec.i,
		(unsigned long) pkt->rec.f,
		(double) (uint32) pkt->rec.i + pkt->rec.f / FACTOR32);
	printf("      xmt %08lx.%08lx = %17.6f\n",
		(unsigned long) (uint32) pkt->xmt.i,
		(unsigned long) pkt->xmt.f,
		(double) (uint32) pkt->xmt.i + pkt->xmt.f / FACTOR32);
	printf("      keyid %d\n", pkt->keyid);
	fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static struct fp sys_clock(void)
{

	struct fp fp;
	struct timeval tv;

	if (gettimeofday(&tv, 0)) return Zero;
	/* time_t is 64 bits; the NTP seconds field is 32 and is meant to wrap
	 * at the era boundary, so do the arithmetic in uint32 explicitly. */
	fp.i = (int32) ((uint32) tv.tv_sec + (uint32) TIMEBIAS);
	fp.f = (uint32) (USEC2F * tv.tv_usec);
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
	if (pkt.version < 1 || pkt.version > 3) goto drop;
	/* Only a client request is answered, and the answer carries
	 * MODE_SERVER - which the test above drops.  Two of these cannot
	 * therefore be made to answer each other, whatever source port is
	 * claimed.  Note in particular that refusing source port 123 would be
	 * wrong here: ntpd in client mode sends from it.
	 */
	if (pkt.mode != MODE_CLIENT) goto drop;

	/* What is left is being used as a reflector for someone else's
	 * address.  It does not amplify - the answer is the same size as the
	 * question - but there is no reason to be a free packet source.
	 */
	if (Msclock - Lastserved < SERVER_MIN_INTERVAL) goto drop;
	Lastserved = Msclock;
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
		send_udp(&ucb->socket, &fsocket, LDELAY, 0, &bp, 0, 0, 0);
		sys.served++;
		if (Ntrace) {
			printf("sent: ");
			dumpntp(&pkt);
		}
	}
	return;

drop:
	sys.refused++;
}

/*---------------------------------------------------------------------------*/

int sntp0(int argc, char **argv, void *p)
{
	if (Server_ucb) {
		del_udp(&Server_ucb);
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

	/* The client socket is not connected - it is opened on INADDR_ANY with
	 * a port straight out of the Lport counter, so anything that reaches
	 * that port lands here.  Take answers from the peer we asked, only.
	 */
	if (fsocket.address != peer->fsocket.address ||
	    fsocket.port != peer->fsocket.port) {
		free_p(&bp);
		return;
	}

	if (ntohntp(&pkt, &bp)) return;
	if (Ntrace) {
		printf("recv: ");
		dumpntp(&pkt);
	}
	if (pkt.leap == LEAP_NOTINSYNC) return;
	if (!pkt.stratum || pkt.stratum > NTP_MAXSTRATUM) return;

	/* The origin timestamp has to be the transmit timestamp we sent.  It is
	 * the only thing tying an answer to our request, and without crypto it
	 * is all SNTP has: an off-path forger has to guess a 64-bit value
	 * rather than just reach the port.
	 *
	 * The old code accepted an origin of zero and filled in our own xmt
	 * instead, which handed that away for free - send org = 0 and the reply
	 * was taken.  That is the NTP "zero origin timestamp" bypass,
	 * CVE-2015-8138.  xmt was cleared above, so a second answer to the same
	 * request is refused as well.
	 */
	if (fpiszero(xmt)) return;              /* nothing outstanding */
	if (fpisne(pkt.org, xmt)) return;       /* not the answer to it */

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
		/* An offset, not a point in time: negative values arrive as
		 * tv_sec = -2, tv_usec = 500000 for -1.5s, which is what
		 * adjtime() wants. */
		tv.tv_sec = (time_t) peer->offset.i;
		tv.tv_usec = (long) (peer->offset.f / USEC2F);
		if (!adjtime(&tv, 0)) {
			peer->adjts++;
			if (Ntrace) printf("Clock adjusted\n");
		} else {
			if (Ntrace) perror("adjtime()");
		}
#endif
		return;
	}

	/* Beyond this the clock is not slewed but set outright, so put a ceiling
	 * on it.  NTP calls this the panic threshold and refuses to act above
	 * it, on the grounds that an offset that large is far more likely to be
	 * a lie or a broken server than a real correction.  Refuse and log,
	 * rather than exit the way ntpd does - a radio node should not go down
	 * because a peer misbehaved.
	 */
	if (Panic_threshold > 0 && abs_offset.i >= Panic_threshold) {
		char msg[128];

		peer->panics++;
		snprintf(msg, sizeof(msg),
			 "sntp: %s offset %.0fs over panic threshold %ds, ignored",
			 inet_ntoa(peer->fsocket.address),
			 fp2double(peer->offset), Panic_threshold);
		logmsg(NULL, "%s", msg);
		return;
	}

	if (gettimeofday(&tv, 0)) return;
	now = fpadd(sys_clock(), peer->offset);
	/* time_t is 64 bits; the old (int) truncated the result and would have
	 * gone negative past 2038.  The NTP seconds field is unsigned, so do
	 * the subtraction in uint32 - which is correct for era 0, i.e. until
	 * 2036, after which the era would have to be tracked separately.
	 */
	tv.tv_sec = (time_t) (uint32) ((uint32) now.i - (uint32) TIMEBIAS);
	tv.tv_usec = (long) (now.f / USEC2F);
	if (!settimeofday(&tv, 0)) {
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
	memset(&pkt, 0, sizeof(pkt));
	pkt.leap = LEAP_NOTINSYNC;
	pkt.version = 1;
	pkt.mode = MODE_CLIENT;
	pkt.poll = 6;
	pkt.precision = -6;
	pkt.rootdelay = One;
	pkt.rootdispersion = One;
	pkt.xmt = peer->xmt = sys_clock();
	if ((bp = htonntp(&pkt))) {
		send_udp(&peer->ucb->socket, &peer->fsocket, LDELAY, 0, &bp, 0, 0, 0);
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
	if (interval <= 0)
		interval = 3333;

	for (peer = Peers; peer; peer = peer->next) {
		if (peer->fsocket.address == addr) {
			set_timer(&peer->timer, interval * 1000L);
			sntp_client_send(peer);
			return 0;
		}
	}

	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	peer = (struct peer *) calloc(1, sizeof(struct peer));
	if (!peer) {
		printf("%s", Nospace);
		return 1;
	}
	peer->fsocket.address = addr;
	peer->fsocket.port = IPPORT_NTP;
	peer->ucb = open_udp(&lsocket, sntp_client_recv);
	if (!peer->ucb) {
		free(peer);
		return 1;
	}
	peer->ucb->user = (long) peer;
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
			del_udp(&peer->ucb);
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

	printf("Server            St Poll  Sent  Rcvd Accpt Steps Adjts Panic    Delay   Offset\n");
	for (peer = Peers; peer; peer = peer->next)
		printf("%-17s %2d %4ld %5d %5d %5d %5d %5d %5d %8.3f %8.3f\n",
			resolve_a(peer->fsocket.address, 0),
			peer->stratum,
			dur_timer(&peer->timer) / 1000L,
			peer->sent,
			peer->rcvd,
			peer->accpt,
			peer->steps,
			peer->adjts,
			peer->panics,
			fp2double(peer->delay),
			fp2double(peer->offset));
	if (Server_ucb)
		printf("Server: %d request%s answered, %d dropped\n",
			sys.served, sys.served == 1 ? "" : "s", sys.refused);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpstep_threshold(int argc, char **argv, void *p)
{
	return setint(&Step_threshold, "sntp step_threshold", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int dosntppanic_threshold(int argc, char **argv, void *p)
{
	return setint(&Panic_threshold, "sntp panic_threshold", argc, argv);
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
	uint32 refid;

	if (argc < 2) {
		printf("sntp sys refid: ");
		if (sys.stratum == 1) {
			print_refid(sys.refid);
		} else
			printf("%s\n", resolve_a(sys.refid, 0));
		return 0;
	}

	if (sys.stratum == 1) {
		cp = argv[1];
		refid = 0;
		for (i = 0; i < 4; i++) {
			refid = (refid << 8) | (*cp & 0xff);
			if (*cp) cp++;
		}
		sys.refid = (int32) refid;
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
		(unsigned long) (uint32) sys.reftime.i,
		(unsigned long) sys.reftime.f,
		(double) (uint32) sys.reftime.i + sys.reftime.f / FACTOR32);
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

		{ "leap", dosntpsysleap, 0, 0, NULL },
		{ "precision", dosntpsysprecision, 0, 0, NULL },
		{ "refid", dosntpsysrefid, 0, 0, NULL },
		{ "reftime", dosntpsysreftime, 0, 0, NULL },
		{ "rootdelay", dosntpsysrootdelay, 0, 0, NULL },
		{ "rootdispersion", dosntpsysrootdispersion, 0, 0, NULL },
		{ "stratum", dosntpsysstratum, 0, 0, NULL },

		{ NULL, NULL, 0, 0, NULL }
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

		{ "add", dosntpadd, 0, 2, "sntp add <server> [<interval>]" },
		{ "drop", dosntpdrop, 0, 2, "sntp drop <server>" },
		{ "status", dosntpstat, 0, 0, NULL },
		{ "panic_threshold", dosntppanic_threshold, 0, 0, NULL },
		{ "step_threshold", dosntpstep_threshold, 0, 0, NULL },
		{ "sys", dosntpsys, 0, 0, NULL },
		{ "trace", dosntptrace, 0, 0, NULL },

		{ NULL, NULL, 0, 0, NULL }
	};

	return subcmd(sntpcmds, argc, argv, p);
}
