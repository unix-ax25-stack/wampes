/* @(#) $Id: n8250.c,v 1.45 1996/08/19 16:30:14 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef ibm032
#include <sgtty.h>
typedef long speed_t;
#else
#include <termios.h>
#endif

#ifndef MAXIOV
#if defined IOV_MAX
#define MAXIOV          IOV_MAX
#else
#define MAXIOV          16
#endif
#endif

/* Transmit progress watchdog (see asy_wd() below): how often to check and
 * how many consecutive checks with a non-empty queue trigger a port reset.
 * The reset closes the port, holds DTR low for ASY_WD_DELAY ticks and then
 * reopens it, which is what wakes the device.  It must not be inline: a
 * freshly closed USB-CDC device that is opened again in the same tick may
 * still be tearing its pipes down and the reopen would fail.
 */
#define ASY_WD_PERIOD   1000            /* ms between progress checks */
#define ASY_WD_STALLS    2              /* consecutive non-empty-queue checks */
#define ASY_WD_DELAY     6              /* ticks DTR stays low in a reset */
#define ASY_WD_FAULT_DELAY 2            /* DTR-low hold after a post-reopen fault */

#ifndef O_NOCTTY
#define O_NOCTTY        0
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK      O_NDELAY
#endif

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "n8250.h"
#include "asy.h"
#include "devparam.h"
#include "hpux.h"
#include "timer.h"

static int find_speed(long speed);
static void pasy(struct asy *asyp);
static void asy_tx(void *arg);
static void asy_wd(void *arg);
static long sndq_len(struct mbuf *bp);

struct asy Asy[ASY_MAX];

/* Modem-line bits for TIOCMBIS/TIOCMBIC/TIOCMGET/TIOCMSET.  The headers name
 * them on Linux and macOS; keep the numbers for the strays that do not.
 */
#ifndef TIOCM_DTR
#define TIOCM_DTR 0x002
#endif
#ifndef TIOCM_RTS
#define TIOCM_RTS 0x004
#endif

/*---------------------------------------------------------------------------*/

static struct {
	long speed;
	speed_t flags;
} speed_table[] = {
#ifdef B50
	{ 50, B50 },
#endif
#ifdef B75
	{ 75, B75 },
#endif
#ifdef B110
	{ 110, B110 },
#endif
#ifdef B134
	{ 134, B134 },
#endif
#ifdef B150
	{ 150, B150 },
#endif
#ifdef B200
	{ 200, B200 },
#endif
#ifdef B300
	{ 300, B300 },
#endif
#ifdef B600
	{ 600, B600 },
#endif
#ifdef B900
	{ 900, B900 },
#endif
#ifdef B1200
	{ 1200, B1200 },
#endif
#ifdef B1800
	{ 1800, B1800 },
#endif
#ifdef B2400
	{ 2400, B2400 },
#endif
#ifdef B3600
	{ 3600, B3600 },
#endif
#ifdef B4800
	{ 4800, B4800 },
#endif
#ifdef B7200
	{ 7200, B7200 },
#endif
#ifdef B9600
	{ 9600, B9600 },
#endif
#ifdef B19200
	{ 19200, B19200 },
#endif
#ifdef B38400
	{ 38400, B38400 },
#endif
#ifdef B57600
	{ 57600, B57600 },
#endif
#ifdef B115200
	{ 115200, B115200 },
#endif
#ifdef B230400
	{ 230400, B230400 },
#endif
#ifdef B460800
	{ 460800, B460800 },
#endif
	{ -1, 0 }
};

/*---------------------------------------------------------------------------*/

static int
find_speed(
long speed)
{
	int i;

	i = 0;
	while (speed_table[i].speed < speed && speed_table[i+1].speed > 0)
		i++;
	return i;
}

/*---------------------------------------------------------------------------*/

static int
asy_up(struct asy *ap)
{
	int sp;
	int dtr = TIOCM_DTR;
	int rts = TIOCM_RTS;

	if (ap->fd >= 0)        /* Already UP */
		return 0;

	if (ap->addr && ap->vec) {      /* Hide TCP connections in here */
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl((unsigned long) ap->addr);
		addr.sin_port = htons((unsigned short) ap->vec);
		if ((ap->fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
			goto Fail;
		if (connect(ap->fd, (struct sockaddr *) &addr, sizeof(addr)) < 0)
			goto Fail;
		sp = find_speed(ap->speed);
		ap->speed = speed_table[sp].speed;
	} else {
		char filename[80];
		const char *path = ap->devfile;

		/* asy_attach() caches the real device node in ap->devfile, so a
		 * watchdog reopen always finds it, no matter what iface->name
		 * has become in between.  Fall back to building the path from
		 * the name for the plain "/dev/xxx" and legacy "xxx" forms.
		 */
		if (path == NULL) {
			*filename = 0;
			if (*ap->iface->name != '/')
				strcpy(filename, "/dev/");
			snprintf(filename + strlen(filename), sizeof(filename),
				"%s", ap->iface->name);
			path = filename;
		}
		if ((ap->fd = open(path, O_RDWR|O_NONBLOCK|O_NOCTTY)) < 0) {
			fprintf(stderr, "%s: failed to open %s (%s)\n",
				ap->iface->name, path, strerror(errno));
			goto Fail;
		}
		/* A freshly opened USB-CDC port may carry stale KISS bytes from
		 * a previous session (left in the device's firmware buffer by a
		 * re-enumeration that happened while data was in flight).  Flush
		 * the input side so the TNC's framing parser starts clean.
		 */
		tcflush(ap->fd, TCIFLUSH);
#ifdef ibm032
		fcntl(ap->fd, F_SETFL, O_NONBLOCK | fcntl(ap->fd, F_GETFL, 0));
#endif
		sp = find_speed(ap->speed);
		ap->speed = speed_table[sp].speed;
		{
#ifdef ibm032
			struct sgttyb sgttyb;
			memset(&sgttyb, 0, sizeof(sgttyb));
			sgttyb.sg_ispeed = speed_table[sp].flags;
			sgttyb.sg_ospeed = speed_table[sp].flags;
			sgttyb.sg_flags = RAW | ANYP | LPASS8 | LNOHANG;
			if (ioctl(ap->fd, TIOCSETP, &sgttyb))
				goto Fail;
#else
			struct termios termios;
			memset(&termios, 0, sizeof(termios));
			termios.c_iflag = IGNBRK | IGNPAR;
			termios.c_cflag = CS8 | CREAD | CLOCAL | (ap->flow & ASY_F_CTS ? CRTSCTS : 0);
			if (ap->flow & ASY_F_RLSD)
				/* "r": carrier-gated, the legacy RLSD/DCD option.
				 * On a USB-CDC backend the modem lines are usually
				 * reported asserted, so it lives on without doing
				 * much there - and opening such a line without
				 * CLOCAL stays non-blocking here anyway.
				 */
				termios.c_cflag &= ~CLOCAL;
			if (cfsetispeed(&termios, speed_table[sp].flags))
				goto Fail;
			if (cfsetospeed(&termios, speed_table[sp].flags))
				goto Fail;
			if (tcsetattr(ap->fd, TCSANOW, &termios))
				goto Fail;
			if (ap->flow & ASY_F_CTS) {
				/* The sysop asked for CTS flow - make sure the
				 * driver really applies it.  Some backends
				 * accept the request without honoring the bit,
				 * which would silently run a link without the
				 * flow control it was brought up with.
				 */
				struct termios applied;
				if (tcgetattr(ap->fd, &applied) < 0) {
					fprintf(stderr, "%s: tcgetattr after TCSETS failed (%s)\n",
						ap->iface->name, strerror(errno));
					goto Fail;
				}
				if (!(applied.c_cflag & CRTSCTS)) {
					fprintf(stderr, "%s: CTS flow requested (c) but "
						"this device cannot provide it\n",
						ap->iface->name);
					goto Fail;
				}
			}
			/* Assert the modem control lines.  A TH-D75-style USB-CDC
			 * backend activates its KISS TNC only while DTR is high -
			 * open() alone left one of them deaf after a device
			 * restart, with the host reply queue backing up (write
			 * blocked, read had stopped past ~680 bytes).  The done
			 * thing is to say it here on every open: the watchdog
			 * reopen then re-asserts DTR as well.  With CTS flow the
			 * driver owns RTS; without it, raise RTS too, the way
			 * the AX25Toolkit does (RTS free for PTT).
			 *
			 * ENOTTY/EINVAL mean the device has no modem lines at
			 * all (a pty, or a driver without modem control) - such
			 * a backend works without them, so tolerate that.
			 * Anything else is a real failure: a backend that needs
			 * DTR and cannot raise it must not come up pretending
			 * otherwise, or it sits there silently deaf again.
			 */
			if (ioctl(ap->fd, TIOCMBIS, &dtr) < 0
			 && errno != ENOTTY && errno != EINVAL) {
				fprintf(stderr, "%s: failed to assert DTR (%s)\n",
					ap->iface->name, strerror(errno));
				goto Fail;
			}
			if (!(ap->flow & ASY_F_CTS)
			 && ioctl(ap->fd, TIOCMBIS, &rts) < 0
			 && errno != ENOTTY && errno != EINVAL) {
				fprintf(stderr, "%s: failed to assert RTS (%s)\n",
					ap->iface->name, strerror(errno));
				goto Fail;
			}
#endif
		}
	}
	tncinit_quick(ap->iface);
	on_read(ap->fd, ap->iface->rxproc, ap->iface);
	/* A reopen can find a queue parked across the reset - asy_wd(), the
	 * read-error path and the write-error path all park ap->sndq there.
	 * Whoever brings the port up, those bytes must go out again, so arm
	 * the writer HERE, in the one place every reopen passes through.
	 * Relying on each caller to re-arm on_write() is how a parked tail
	 * sits in the queue forever - port "UP", txchar stuck, no BUSY burst,
	 * until the next real asy_send() happens to call on_write.  The
	 * select side is fine (on_write grows maxfd again), the arm was lost.
	 */
	if (ap->sndq != NULL)
		on_write(ap->fd, asy_tx, ap);
	if (ap->addr == 0 && ap->vec == 0) {
		/* A serial device, whose endpoint can wedge, is worth the
		 * watchdog; a TCP backend has nothing to reopen.
		 */
		if (ap->wd.duration == 0)
			set_timer(&ap->wd, ASY_WD_PERIOD);
		ap->lasttx = ap->txchar;
		ap->wdstall = 0;
		ap->wdreopen = 0;
		ap->wd.func = asy_wd;
		ap->wd.arg = ap;
		start_timer(&ap->wd);
	}
	ap->rxenx = 0;          /* frisches Open - alte Restschuld zaehlt nicht */
	return 0;

Fail:
	if (ap->fd >= 0) {
		close(ap->fd);
		ap->fd = -1;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

static int asy_down(struct asy *ap, const char *who)
{
	if (ap->fd < 0)         /* Already DOWN */
		return 0;

	off_read(ap->fd);
	off_write(ap->fd);
	free_q(&ap->sndq);
	close(ap->fd);
	ap->fd = -1;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Initialize asynch port "dev" */
int
asy_init(
int dev,
struct iface *ifp,
int base,
int irq,
uint bufsize,
int trigchar,
long speed,
int cts,                /* Use CTS flow control */
int rlsd,               /* Use Received Line Signal Detect (aka CD) */
int chain)              /* Chain interrupts */
{
	register struct asy *ap;

	ap = &Asy[dev];
	ap->fd = -1;
	ap->iface = ifp;
	ap->addr = base;
	ap->vec = irq;
	ap->speed = speed;
	ap->flow = (cts ? ASY_F_CTS : 0) | (rlsd ? ASY_F_RLSD : 0);
	/* "chain" is the legacy 8250 interrupt-chaining flag; nothing left to
	 * hook in a descriptor-based driver, deliberately not remembered.
	 */
	return asy_up(ap);
}

/*---------------------------------------------------------------------------*/

int
asy_stop(
struct iface *ifp)
{
	register struct asy *ap;

	ap = &Asy[ifp->dev];

	if(ap->iface == NULL)
		return -1;      /* Not allocated */
	asy_down(ap,"asy_stop (detach)");
	stop_timer(&ap->wd);
	if (ap->devfile != NULL) {
		free(ap->devfile);
		ap->devfile = NULL;
	}
	ap->iface = NULL;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Set asynch line speed */
int
asy_speed(
int dev,
long bps)
{

	struct asy *asyp;
	int sp;

	if(bps <= 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if(asyp->iface == NULL)
		return -1;

	if(bps == 0)
		return -1;
	sp = find_speed(bps);

	if (asyp->fd >= 0 && (asyp->addr == 0 || asyp->vec == 0)) {
#ifdef ibm032
		struct sgttyb sgttyb;
		if (ioctl(asyp->fd, TIOCGETP, &sgttyb))
			return -1;
		sgttyb.sg_ispeed = speed_table[sp].flags;
		sgttyb.sg_ospeed = speed_table[sp].flags;
		if (ioctl(asyp->fd, TIOCSETP, &sgttyb))
			return -1;
#else
		struct termios termios;
		if (tcgetattr(asyp->fd, &termios))
			return -1;
		if (cfsetispeed(&termios, speed_table[sp].flags))
			return -1;
		if (cfsetospeed(&termios, speed_table[sp].flags))
			return -1;
		if (tcsetattr(asyp->fd, TCSANOW, &termios))
			return -1;

#endif
	}
	asyp->speed = speed_table[sp].speed;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Read, or set/clear, one modem control line.  Both directions report the
 * bit as the driver sees it, so "param <if> DTR" echoes the line back.
 */
static int
asy_modem(
struct iface *ifp,
int bit,
int set,
int32 val)
{
	struct asy *ap = &Asy[ifp->dev];
	int modem;

	if (ap->fd < 0 || ap->iface == NULL)
		return -1;
	if (ap->addr && ap->vec)
		return -1;      /* a TCP backend has no modem lines */
	if (ioctl(ap->fd, TIOCMGET, &modem) < 0)
		return -1;
	if (set) {
		if (ioctl(ap->fd, val ? TIOCMBIS : TIOCMBIC, &bit) < 0)
			return -1;
		if (ioctl(ap->fd, TIOCMGET, &modem) < 0)
			return -1;
	}
	return (modem & bit) ? 1 : 0;
}

/*---------------------------------------------------------------------------*/

/* Asynchronous line I/O control */
int32
asy_ioctl(
struct iface *ifp,
int cmd,
int set,
int32 val)
{
	struct asy *ap = &Asy[ifp->dev];

	switch(cmd){
	case PARAM_SPEED:
		if(set)
			asy_speed(ifp->dev,val);
		return ap->speed;
	case PARAM_DTR:
		return asy_modem(ifp,TIOCM_DTR,set,val);
	case PARAM_RTS:
		return asy_modem(ifp,TIOCM_RTS,set,val);
	case PARAM_DOWN:
		ap->wdreopen = 0;        /* manual down beats a pending reopen */
		asy_down(ap,"asy_ioctl PARAM_DOWN");
		return 0;
	case PARAM_UP:
		return asy_up(ap) ? 0 : 1;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

int
get_asy(
int dev,
uint8 *buf,
int cnt)
{
	struct asy *ap;

	ap = &Asy[dev];
	if(ap->iface == NULL)
		return 0;
	cnt = read(ap->fd,buf,cnt);
	ap->rxints++;
	if (cnt > 0) {
		ap->rxchar += cnt;
		if (ap->rxhiwat < cnt)
			ap->rxhiwat = cnt;
		ap->rxenx = 0;          /* Daten - die Leitung lebt */
		return cnt;
	}
	/* No data this round.  What the caller needs to know is whether the
	 * line is still alive.
	 */
	if (cnt < 0 && (errno == EINTR || errno == EAGAIN)) {
		ap->rxenx = 0;          /* Transient - nothing to deliver, line lives */
		return 0;
	}
	if (ap->addr && ap->vec) {
		/* TCP backend: EOF (0) or a real error is the peer going away. */
		asy_down(ap,"get_asy TCP eof/error");
	} else if (cnt < 0 && errno == ENXIO) {
		/* USB-CDC transient: the TH-D75 (and similar) accepts write()
		 * but the next read() returns ENXIO while the chip is still
		 * busy on the USB bus.  The device is not gone: a subsequent
		 * write will succeed if it is alive, or fail and bring the
		 * port down through the write-error path.  ENXIO (6) is the
		 * signal the TH-D75 uses; ENODEV would be a real disappearance.
		 *
		 * Returning 0 alone is not enough: the descriptor stays
		 * readable for select(), and a chip that keeps answering ENXIO
		 * would spin this function at full speed, rxints climbing into
		 * the hundred thousands within seconds.  So every ENXIO
		 * withdraws read interest again - the one-second watchdog
		 * (asy_wd()) re-arms it on its next tick, which gives a stubborn
		 * chip one fresh read per second and nothing more.  Data or
		 * EAGAIN on the way resets the streak and read interest rides
		 * along: any new traffic needs no re-arm, on_read is simply
		 * already armed.  (The withdrawal must happen on EVERY ENXIO,
		 * not just the first: once the watchdog has re-armed the fd, a
		 * missed withdrawal would leave it in chkread permanently and
		 * the spin is back.)
		 *
		 * An ENXIO here is deliberately NOT turned into a port reset.
		 * Measured on the TH-D75: after the first transmission the TNC
		 * wedges while the device is still on the bus - read() says
		 * ENXIO forever, the DTR-pulse reopen revives the fd only, the
		 * radio still does not transmit again.  A reset would therefore
		 * only add downtime without fixing anything, and enough queued
		 * data against the wedged 3 kB KISS buffer can even crash the
		 * whole USB bridge (device node vanishes, ENODEV on reopen).
		 * So the read side just idles at one probe per second until the
		 * TNC comes back on its own or the line is taken down for a
		 * real reason.
		 */
		ap->rxenx++;
		off_read(ap->fd);
		return 0;
	} else if (cnt < 0) {
		/* A local serial device truly vanishing brings it down.  The
		 * reset cycle this arms reopens the device once the
		 * re-enumeration has settled.  Like the stall reset, anything
		 * still queued is parked across the down.
		 */
		{
			struct mbuf *bp = ap->sndq;

			fprintf(stderr, "%s: read error, resetting (fd %d, errno %d, %lu bytes queued)\n",
				ap->iface->name, ap->fd, errno, sndq_len(bp));
			ap->sndq = NULL;
			asy_down(ap,"get_asy serial read error");
			ap->sndq = bp;
		}
		ap->wdreset++;
		ap->wdreopen = ASY_WD_FAULT_DELAY;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

int
doasystat(
int argc,
char *argv[],
void *p)
{
	register struct asy *asyp;
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface != NULL)
				pasy(asyp);
		}
		return 0;
	}
	for(i=1;i<argc;i++){
		if((ifp = if_lookup(argv[i])) == NULL){
			printf("Interface %s unknown\n",argv[i]);
			continue;
		}
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface == ifp){
				pasy(asyp);
				break;
			}
		}
		if(asyp == &Asy[ASY_MAX])
			printf("Interface %s not asy\n",argv[i]);
	}

	return 0;
}

/*---------------------------------------------------------------------------*/

static void
pasy(
struct asy *asyp)
{

	printf("%s:",asyp->iface->name);
	if(asyp->fd < 0)
		printf(" [DOWN]");
	else
		printf(" [UP]");

	printf(" %lu bps\n",asyp->speed);

	printf(" RX: int %lu chars %lu hw hi %lu enxio %u\n",
	 asyp->rxints,asyp->rxchar,asyp->rxhiwat,asyp->rxenx);
	asyp->rxhiwat = 0;

	printf(" TX: int %lu chars %lu%s",
	 asyp->txints,asyp->txchar,
	 asyp->sndq ? " BUSY" : "");
	if(asyp->txqueued > asyp->txchar)
		printf(" (%lu%% of %lu queued)",
		 asyp->txchar * 100 / asyp->txqueued,asyp->txqueued);
	if(asyp->wdreset)
		printf(" (%lu serial resets)",asyp->wdreset);
	printf("\n");
}

/*---------------------------------------------------------------------------*/

/* Serial transmit process, common to all protocols */
static void
asy_tx(void *arg)
{
	struct asy *asyp = (struct asy *) arg;
	int n;

	if (asyp->sndq != NULL) {
		struct iovec iov[MAXIOV];
		struct mbuf *bp;
		n = 0;
		for (bp = asyp->sndq; bp && n < MAXIOV; bp = bp->next) {
			iov[n].iov_base = (char *) bp->data;
			iov[n].iov_len = bp->cnt;
			n++;
		}
		n = writev(asyp->fd, iov, n);
		asyp->txints++;
		if (n < 0 && errno == EAGAIN)
			/* No room for even a byte right now; leave the queue
			 * where it is.  select() will call us again when the
			 * device has drained and it is writeable once more.
			 */
			return;
		if (n <= 0) {
			/* Hard write error - the device has gone or wedged.
			 * Drop the queue: the data was never accepted and
			 * re-sending it after a reopen just re-overflows a
			 * half-initialized chip.  The stall reset path
			 * preserves queued data because the device was alive
			 * but stuck; here it has actively refused the write,
			 * so the bytes are lost.  Starting clean after the
			 * reopen gives the TNC time to re-initialize.
			 */
			fprintf(stderr, "%s: write error, dropping %lu bytes (fd %d)\n",
				asyp->iface->name, sndq_len(asyp->sndq), asyp->fd);
			asy_down(asyp,"asy_tx write error");
			asyp->wdreset++;
			asyp->wdreopen = ASY_WD_FAULT_DELAY;
			return;
		}
		asyp->txchar += n;
		asyp->rxenx = 0;        /* Write klappt - der Chip lebt */
		while (n > 0) {
			if (n >= asyp->sndq->cnt) {
				n -= asyp->sndq->cnt;
				free_mbuf(&asyp->sndq);
			} else {
				asyp->sndq->data += n;
				asyp->sndq->cnt -= n;
				n = 0;
			}
		}
	}
	if (asyp->sndq == NULL)
		off_write(asyp->fd);
}

/*---------------------------------------------------------------------------*/

/* Count queued bytes (the head of the packet list, not a queue head) */
static long
sndq_len(
struct mbuf *bp)
{
	long n;

	n = 0;
	for (; bp != NULL; bp = bp->next)
		n += bp->cnt;
	return n;
}

/*---------------------------------------------------------------------------*/

/* Transmit progress watchdog, ticking once a second while a serial interface
 * is up.  Wireless TNCs with USB-CDC backends (a Kenwood TH-D75 among them)
 * can stop draining their endpoint after a burst: select() then never reports
 * the port as writeable again, sndq grows forever and asystat shows "BUSY" no
 * matter how long one waits.  If the queue is not empty and not a single byte
 * has been written for a couple of ticks, that is a wedge beyond any error
 * handling we could do from here; the only cure is to wake the device with a
 * fresh open, not just a fresh file descriptor.  The reopen pulls DTR - the
 * close drops it, asy_up() asserts it again on the way up - which is what
 * resets the hardware, so portdown and - after a short pause, so the
 * device has time to notice the DTR pulse - portup is the recovery.  The data
 * still queued is saved and written as soon as the line is alive again.
 *
 * A reset is not free: the TH-D75 re-enumerates on the USB bus in response,
 * and a port reopened while that is still happening dies again a moment later
 * (writes briefly succeed, then the first read fails with ENXIO).  ASY_WD_DELAY
 * therefore has to outlast the re-enumeration, and any down caused by a read
 * or write error arms another reset cycle on its own - with an empty queue the
 * watchdog would otherwise have nothing to watch and the port would stay down.
 */
static void
asy_wd(void *arg)
{
	struct asy *ap = (struct asy *) arg;

	start_timer(&ap->wd);

	if (ap->iface == NULL)
		return;

	if (ap->wdreopen > 0) {
		/* A reset is in progress: the port is closed and the queued
		 * data is parked in ap->sndq.  Count down ASY_WD_DELAY ticks
		 * with DTR held low, then bring the line up.  If the open
		 * fails (device unplugged, port busy) keep trying - the
		 * queue stays put meanwhile.
		 */
		if (--ap->wdreopen > 0)
			return;
		if (asy_up(ap) < 0) {
			fprintf(stderr, "%s: reopen failed (errno %d), will retry, %lu bytes queued\n",
				ap->iface->name, errno, sndq_len(ap->sndq));
			ap->wdreopen = 1;       /* retry next tick */
			return;
		}
		return;
	}

	if (ap->fd < 0) {
		/* The port is down for no reset of ours.  Something tore the
		 * interface down (a vanished device, an I/O error), and if
		 * traffic is still waiting a reset is the only way to get it
		 * out.  The queue head is already right where the reset parks
		 * it, so there is nothing to save here.
		 */
		if (ap->sndq != NULL) {
			ap->wdreset++;
			ap->wdreopen = ASY_WD_DELAY;
		}
		return;
	}

	/* ENXIO (see get_asy) withdrew read interest to stop a
	 * spinning select.  Re-arm it here, once per check, so a chip that
	 * was merely busy gets exactly one fresh read per second and a
	 * recovered one is picked up on the first tick - while a permanently
	 * wedged one keeps the streak visible in asystat ('enxio') instead
	 * of hammering the USB bus.  The count is preserved on purpose; data
	 * or EAGAIN reset it elsewhere.
	 */
	if (ap->rxenx > 0 && ap->wdreopen == 0)
		on_read(ap->fd, ap->iface->rxproc, ap->iface);

	if (ap->sndq == NULL) {
		/* Nothing pending - the line drains, no stall can build up. */
		ap->lasttx = ap->txchar;
		ap->wdstall = 0;
		return;
	}

	/* A non-empty queue is the situation this watchdog exists for: the
	 * TH-D75 settles into a trickle after a burst - a byte or two per
	 * tick, slow enough that the queue never empties, fast enough that a
	 * "did any byte move?" test never trips.  What matters instead is how
	 * long the queue stays non-empty: a line that is draining properly
	 * empties a packet's worth in milliseconds, so two consecutive checks
	 * with anything still queued is already a reset.  Only real traffic
	 * that is continuous over seconds gets the same treatment - which, on
	 * a device that wedges, is the far lesser evil than an endless BUSY.
	 */
	if (++ap->wdstall < ASY_WD_STALLS)
		return;

	ap->lasttx = ap->txchar;
	ap->wdstall = 0;
	fprintf(stderr, "%s: serial transmit stalled, resetting the interface (fd %d, %lu bytes queued)\n",
		ap->iface->name, ap->fd, sndq_len(ap->sndq));

	/* Keep the queue across the reset: asy_down() discards it, so
	 * save it here and hand it back once the port is back up.
	 */
	{
		struct mbuf *bp = ap->sndq;

		ap->sndq = NULL;
		asy_down(ap,"asy_wd stall reset");
		ap->sndq = bp;
	}
	ap->wdreset++;
	ap->wdreopen = ASY_WD_DELAY;
}

/*---------------------------------------------------------------------------*/

/* Send a message on the specified serial line */
int
asy_send(
int dev,
struct mbuf **bpp)
{
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX){
		free_p(bpp);
		return -1;
	}
	asyp = &Asy[dev];

	if(asyp->iface == NULL || asyp->fd < 0)
		free_p(bpp);
	else {
		asyp->txqueued += sndq_len(*bpp);
		append(&asyp->sndq, bpp);
		on_write(asyp->fd, asy_tx, asyp);
	}
	return 0;
}

