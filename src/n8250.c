/* @(#) $Id: n8250.c,v 1.45 1996/08/19 16:30:14 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
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

struct asy Asy[ASY_MAX];

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
		strcpy(filename, "/dev/");
		strcat(filename, ap->iface->name);
		if ((ap->fd = open(filename, O_RDWR|O_NONBLOCK|O_NOCTTY, 0644)) < 0)
			goto Fail;
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
			termios.c_cflag = CS8 | CREAD | CLOCAL;
			if (cfsetispeed(&termios, speed_table[sp].flags))
				goto Fail;
			if (cfsetospeed(&termios, speed_table[sp].flags))
				goto Fail;
			if (tcsetattr(ap->fd, TCSANOW, &termios))
				goto Fail;
#endif
		}
	}
	on_read(ap->fd, ap->iface->rxproc, ap->iface);
	return 0;

Fail:
	if (ap->fd >= 0) {
		close(ap->fd);
		ap->fd = -1;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

static int asy_down(struct asy *ap)
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
	asy_down(ap);
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
	case PARAM_DOWN:
		return asy_down(ap) ? 0 : 1;
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
	if (cnt <= 0) {
		asy_down(ap);
		return 0;
	}
	ap->rxchar += cnt;
	if (ap->rxhiwat < cnt)
		ap->rxhiwat = cnt;
	return cnt;
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

	printf(" RX: int %lu chars %lu hw hi %lu\n",
	 asyp->rxints,asyp->rxchar,asyp->rxhiwat);
	asyp->rxhiwat = 0;

	printf(" TX: int %lu chars %lu%s\n",
	 asyp->txints,asyp->txchar,
	 asyp->sndq ? " BUSY" : "");
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
		if (n <= 0) {
			asy_down(asyp);
			return;
		}
		asyp->txchar += n;
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
		append(&asyp->sndq, bpp);
		on_write(asyp->fd, asy_tx, asyp);
	}
	return 0;
}

