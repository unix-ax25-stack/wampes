/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/sntp.c,v 1.5 1994/05/11 10:46:40 deyke Exp $ */

/* Simple Network Time Protocol (SNTP) (see RFC1361) */

#include <sys/types.h>

#include <netinet/in.h>
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

#define NTP_MIN_PACKET_SIZE 48
#define NTP_PACKET_SIZE 60

#define NTP_MAXSTRATUM 15

struct peer {
	struct socket fsocket;
	struct udp_cb *ucb;
	struct timer timer;
	double xmt;
	int sent;
	int rcvd;
	int accpt;
	int steps;
	int adjts;
	int stratum;
	double offset;
	double delay;
	double mindelay;
	struct peer *next;
};

struct pkt {
	int leap;
	int version;
	int mode;
	int stratum;
	int poll;
	int precision;
	double rootdelay;
	double rootdispersion;
	int refid;
	double reftime;
	double org;
	double rec;
	double xmt;
	int keyid;
	char check[8];
};

/* Do NOT convert to #define because of bugs in Sun's optimizer */
static const double FACTOR16 = 65536.0;
static const double FACTOR32 = 4294967296.0;
static const double MILLION = 1e6;
static const double TIMEBIAS = 2208988800.0;

static int Ntrace;
static int Step_threshold = 1;
static struct peer *Peers;
static struct udp_cb *Server_ucb;

/*---------------------------------------------------------------------------*/

static double getunsigned32(const unsigned long *buf)
{
	register unsigned long w = ntohl(*buf);
	return (w >> 16) + (w & 0xffff) / FACTOR16;
}

/*---------------------------------------------------------------------------*/

static double getunsigned64(const unsigned long *buf)
{
	return ((unsigned long) ntohl(buf[0])) +
	       ((unsigned long) ntohl(buf[1])) / FACTOR32;
}

/*---------------------------------------------------------------------------*/

static double getsigned32(const unsigned long *buf)
{

	register long i;
	register unsigned long f;

	i = ntohl(*buf);
	f = i & 0xffff;
	i >>= 16;
	if (i >= 0) return i + f / FACTOR16;
	if (!f) return i;
	return - ((~i) + (-f & 0xffff) / FACTOR16);
}

/*---------------------------------------------------------------------------*/

#if 0 /* Currently not needed */

static double getsigned64(const unsigned long *buf)
{

	register long i;
	register unsigned long f;

	i = ntohl(buf[0]);
	f = ntohl(buf[1]);
	if (i >= 0) return i + f / FACTOR32;
	if (!f) return i;
	return - ((~i) + (-f) / FACTOR32);
}

#endif

/*---------------------------------------------------------------------------*/

static void putsigned32(double d, unsigned long *buf)
{
	register unsigned long i, f;

	if (d >= 0) {
		i = d;
		f = (d - i) * FACTOR16;
	} else {
		d = -d;
		i = d;
		f = (d - i) * FACTOR16;
		if (!f) {
			i = -i;
		} else {
			i = ~i;
			f = -f;
		}
	}
	*buf = htonl((i << 16) | (f & 0xffff));
}

/*---------------------------------------------------------------------------*/

static void putsigned64(double d, unsigned long *buf)
{
	register unsigned long i, f;

	if (d >= 0) {
		i = d;
		f = (d - i) * FACTOR32;
	} else {
		d = -d;
		i = d;
		f = (d - i) * FACTOR32;
		if (!f) {
			i = -i;
		} else {
			i = ~i;
			f = -f;
		}
	}
	buf[0] = htonl(i);
	buf[1] = htonl(f);
}

/*---------------------------------------------------------------------------*/

static struct mbuf *htonntp(const struct pkt *pkt)
{

	struct mbuf *bp;
	unsigned long *p;

	if (bp = ambufw(NTP_PACKET_SIZE)) {
		bp->cnt = NTP_PACKET_SIZE;
		p = (unsigned long *) bp->data;
		*p++ = htonl(((pkt->leap      & 0x03) << 30) |
			     ((pkt->version   & 0x07) << 27) |
			     ((pkt->mode      & 0x07) << 24) |
			     ((pkt->stratum   & 0xff) << 16) |
			     ((pkt->poll      & 0xff) <<  8) |
			      (pkt->precision & 0xff));
		putsigned32(pkt->rootdelay, p++);
		putsigned32(pkt->rootdispersion, p++);
		*p++ = htonl(pkt->refid);
		putsigned64(pkt->reftime, p); p += 2;
		putsigned64(pkt->org, p); p += 2;
		putsigned64(pkt->rec, p); p += 2;
		putsigned64(pkt->xmt, p); p += 2;
		*p++ = htonl(pkt->keyid);
		memcpy((char *) p, pkt->check, sizeof(pkt->check));
	}
	return bp;
}

/*---------------------------------------------------------------------------*/

static int ntohntp(struct pkt *pkt, struct mbuf **bpp)
{

	int n;
	signed char c;
	unsigned long buf[12];
	unsigned long w;

	n = pullup(bpp, (char *) buf, NTP_MIN_PACKET_SIZE);
	free_p(*bpp);
	*bpp = NULLBUF;
	if (n < NTP_MIN_PACKET_SIZE) return (-1);
	w = ntohl(buf[0]);
	pkt->leap = (w >> 30) & 0x03;
	pkt->version = (w >> 27) & 0x07;
	pkt->mode = (w >> 24) & 0x07;
	pkt->stratum = (w >> 16) & 0xff;
	c = w >> 8;
	pkt->poll = c;
	c = w;
	pkt->precision = c;
	pkt->rootdelay = getsigned32(buf + 1);
	pkt->rootdispersion = getunsigned32(buf + 2);
	pkt->refid = ntohl(buf[3]);
	pkt->reftime = getunsigned64(buf + 4);
	pkt->org = getunsigned64(buf + 6);
	pkt->rec = getunsigned64(buf + 8);
	pkt->xmt = getunsigned64(buf + 10);
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
		pkt->rootdelay, pkt->rootdispersion);
	if (pkt->stratum == 1) {
		putchar(pkt->refid >> 24);
		putchar(pkt->refid >> 16);
		putchar(pkt->refid >>  8);
		putchar(pkt->refid      );
		putchar('\n');
	} else
		printf("%s\n", resolve_a(pkt->refid, 0));
	printf("      ref %17.6f\n", pkt->reftime);
	printf("      org %17.6f\n", pkt->org);
	printf("      rec %17.6f\n", pkt->rec);
	printf("      xmt %17.6f\n", pkt->xmt);
	printf("      keyid %d\n", pkt->keyid);
	fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static double sys_clock(void)
{

	struct timeval tv;
	struct timezone tz;

	if (gettimeofday(&tv, &tz)) return 0.0;
	return TIMEBIAS + tv.tv_sec + tv.tv_usec / MILLION;
}

/*---------------------------------------------------------------------------*/

static void sntp_server(struct iface *iface, struct udp_cb *ucb, int cnt)
{

	double rec;
	struct mbuf *bp;
	struct pkt pkt;
	struct socket fsocket;

	rec = sys_clock();
	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	if (ntohntp(&pkt, &bp)) return;
	if (Ntrace) {
		printf("recv: ");
		dumpntp(&pkt);
	}
	pkt.leap = 0;
	if (pkt.version < 1 || pkt.version > 3) return;
	if (pkt.mode != 3) return;
	pkt.mode = 4;
	pkt.stratum = 1;
	pkt.precision = -6;
	pkt.rootdelay = 0.0;
	pkt.rootdispersion = 0.0;
	pkt.refid = ('U' << 24) | ('N' << 16) | ('I' << 8) | 'X';
	pkt.reftime = rec;
	pkt.org = pkt.xmt;
	pkt.rec = rec;
	pkt.keyid = 0;
	memset(pkt.check, 0, sizeof(pkt.check));
	pkt.xmt = sys_clock();
	if (bp = htonntp(&pkt)) {
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

	double now;
	double rec;
	double xmt;
	struct mbuf *bp;
	struct peer *peer;
	struct pkt pkt;
	struct socket fsocket;
	struct timeval tv;
	struct timezone tz;

	rec = sys_clock();
	peer = (struct peer *) ucb->user;
	xmt = peer->xmt;
	peer->xmt = 0;
	peer->rcvd++;
	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	if (ntohntp(&pkt, &bp)) return;
	if (Ntrace) {
		printf("recv: ");
		dumpntp(&pkt);
	}
	if (pkt.leap == 3) return;
	if (!pkt.stratum || pkt.stratum > NTP_MAXSTRATUM) return;
	if (!pkt.org) {
		if (!xmt) return;
		pkt.org = xmt;
	}
	if (!pkt.rec) pkt.rec = pkt.xmt;
	if (!pkt.xmt) return;

	peer->stratum = pkt.stratum;
	peer->delay = (rec - pkt.org) - (pkt.xmt - pkt.rec);
	peer->offset = 0.5 * (pkt.rec - pkt.org + pkt.xmt - rec);
	peer->accpt++;

	if (Ntrace)
		printf("Delay = %.3f  Offset = %.3f\n", peer->delay, peer->offset);

	peer->mindelay = (peer->mindelay * 15.0 + peer->delay) / 16.0;
	if (pkt.rec >= pkt.org && pkt.xmt <= rec && peer->delay >= peer->mindelay)
		return;
	peer->mindelay = peer->delay;

	if (peer->offset > -Step_threshold && peer->offset < Step_threshold) {
#if HAS_ADJTIME || defined __hpux
		tv.tv_sec = peer->offset;
		tv.tv_usec = (peer->offset - (signed long) tv.tv_sec) * MILLION;
		if (!adjtime(&tv, (struct timeval *) 0)) {
			peer->adjts++;
			if (Ntrace) printf("Clock adjusted\n");
		} else {
			if (Ntrace) perror("adjtime()");
		}
#endif
		return;
	}
	if (gettimeofday(&tv, &tz)) return;
	now = tv.tv_sec + tv.tv_usec / MILLION + peer->offset;
	tv.tv_sec = now;
	tv.tv_usec = (now - tv.tv_sec) * MILLION;
	if (!settimeofday(&tv, &tz)) {
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

	peer = arg;
	start_timer(&peer->timer);
	memset((char *) & pkt, 0, sizeof(pkt));
	pkt.leap = 3;
	pkt.version = 1;
	pkt.mode = 3;
	pkt.poll = 6;
	pkt.precision = -6;
	pkt.rootdelay = 1.0;
	pkt.rootdispersion = 1.0;
	pkt.xmt = peer->xmt = sys_clock();
	if (bp = htonntp(&pkt)) {
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

	int addr;
	int interval;
	struct peer *peer;
	struct socket lsocket;

	if (!(addr = resolve(argv[1]))) {
		printf(Badhost, argv[1]);
		return 1;
	}
	interval = (argc < 3) ? 3333 : atoi(argv[2]);
	if (interval <= 0)
		interval = 3333;
	lsocket.address = INADDR_ANY;
	lsocket.port = Lport++;
	peer = (struct peer *) calloc(1, sizeof(*peer));
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

	int addr;
	struct peer *curr;
	struct peer *prev;

	if (!(addr = resolve(argv[1]))) {
		printf(Badhost, argv[1]);
		return 1;
	}
	for (prev = 0, curr = Peers; curr; prev = curr, curr = curr->next)
		if (curr->fsocket.address == addr) {
			if (prev)
				prev->next = curr->next;
			else
				Peers = curr->next;
			del_udp(curr->ucb);
			stop_timer(&curr->timer);
			free(curr);
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
			peer->delay,
			peer->offset);
	return 0;
}

/*---------------------------------------------------------------------------*/

static int dosntpstep_threshold(int argc, char **argv, void *p)
{
	return setint(&Step_threshold, "sntp step_threshold", argc, argv);
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
		"trace", dosntptrace, 0, 0, NULLCHAR,
		NULLCHAR, NULLFP, 0, 0, NULLCHAR
	};

	return subcmd(sntpcmds, argc, argv, p);
}

