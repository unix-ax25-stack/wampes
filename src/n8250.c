/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/n8250.c,v 1.12 1992/01/08 13:44:55 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termio.h>
#include <unistd.h>

#if defined(ISC) || defined(SCO)
#include <sys/tty.h>
#include <sys/stream.h>
#include <sys/ptem.h>
#ifdef ISC
#include <sys/pty.h>
#endif
#ifdef SCO
#include <sys/vty.h>
#endif
#define FIOSNBIO        FIONBIO
#endif

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "iface.h"
#include "8250.h"
#include "asy.h"
#include "devparam.h"
#include "buildsaddr.h"
#include "hpux.h"
#include "timer.h"

static int find_speed __ARGS((int speed));
static int asy_open __ARGS((int dev));
static void asy_tx __ARGS((struct asy *asyp));

struct asy Asy[ASY_MAX];

/*---------------------------------------------------------------------------*/

static struct {
	int speed, flags;
} speed_table[] = {
#ifdef B50
	50, B50,
#endif
#ifdef B75
	75, B75,
#endif
#ifdef B110
	110, B110,
#endif
#ifdef B134
	134, B134,
#endif
#ifdef B150
	150, B150,
#endif
#ifdef B200
	200, B200,
#endif
#ifdef B300
	300, B300,
#endif
#ifdef B600
	600, B600,
#endif
#ifdef B900
	900, B900,
#endif
#ifdef B1200
	1200, B1200,
#endif
#ifdef B1800
	1800, B1800,
#endif
#ifdef B2400
	2400, B2400,
#endif
#ifdef B3600
	3600, B3600,
#endif
#ifdef B4800
	4800, B4800,
#endif
#ifdef B7200
	7200, B7200,
#endif
#ifdef B9600
	9600, B9600,
#endif
#ifdef B19200
	19200, B19200,
#endif
#ifdef B38400
	38400, B38400,
#endif
#ifdef B57600
	57600, B57600,
#endif
#ifdef B115200
	115200, B115200,
#endif
#ifdef B230400
	230400, B230400,
#endif
#ifdef B460800
	460800, B460800,
#endif
	-1, 0
};

/*---------------------------------------------------------------------------*/

static int
find_speed(speed)
int speed;
{
	int i;

	i = 0;
	while (speed_table[i].speed < speed && speed_table[i+1].speed > 0)
		i++;
	return i;
}

/*---------------------------------------------------------------------------*/

static int
asy_open(dev)
int dev;
{

#define MIN_WAIT (3*60)
#define MAX_WAIT (3*60*60)

  static struct {
    long  next;
    long  wait;
  } times[ASY_MAX];

  char  buf[80];
  int  addrlen;
  int  flags;
  int  sp;
  long  arg;
  struct asy *ap;
  struct iface *ifp;
  struct sockaddr *addr;
  struct termio termio;

  if (times[dev].next > secclock()) return (-1);
  times[dev].wait *= 2;
  if (times[dev].wait < MIN_WAIT) times[dev].wait = MIN_WAIT;
  if (times[dev].wait > MAX_WAIT) times[dev].wait = MAX_WAIT;
  times[dev].next = secclock() + times[dev].wait;
  ap = Asy + dev;
  ifp = ap->iface;
  asy_stop(ifp);
  if (!strncmp(ifp->name, "ipc", 3)) {
    if (!(addr = build_sockaddr(ap->ipc_socket, &addrlen))) goto Fail;
    if ((ap->fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) goto Fail;
    if (connect(ap->fd, addr, addrlen)) goto Fail;
    if ((flags = fcntl(ap->fd, F_GETFL, 0)) == -1) goto Fail;
    if (fcntl(ap->fd, F_SETFL, flags | O_NDELAY) == -1) goto Fail;
    ap->speed = -1;
  } else {
    strcpy(buf, "/dev/");
    strcat(buf, ifp->name);
    if ((ap->fd = open(buf, O_RDWR, 0666)) < 0) goto Fail;
    sp = find_speed(ap->speed);
    ap->speed = speed_table[sp].speed;
    termio.c_iflag = IGNBRK | IGNPAR;
    termio.c_oflag = 0;
    termio.c_cflag = speed_table[sp].flags | CS8 | CREAD | CLOCAL;
    termio.c_lflag = 0;
    termio.c_line = 0;
    termio.c_cc[VMIN] = 0;
    termio.c_cc[VTIME] = 0;
    if (ioctl(ap->fd, TCSETA, &termio) == -1) goto Fail;
    if (ioctl(ap->fd, TCFLSH, 2) == -1) goto Fail;
    arg = 1;
    ioctl(ap->fd, FIOSNBIO, &arg);      /*** will fail on pty master side ***/
  }
  times[dev].wait = 0;
  on_read(ap->fd, (void (*)()) ifp->rxproc, ifp);
  return 0;

Fail:
  if (ap->fd > 0) close(ap->fd);
  ap->fd = -1;
  return (-1);
}

/*---------------------------------------------------------------------------*/

/* Initialize asynch port "dev" */
int
asy_init(dev,ifp,arg1,arg2,bufsize,trigchar,monitor,speed)
int dev;
struct iface *ifp;
char *arg1,*arg2;       /* Attach args for address and vector */
int16 bufsize;
int trigchar;
char monitor;
long speed;
{
	register struct asy *ap;

	ap = &Asy[dev];
	ap->iface = ifp;
	ap->ipc_socket = strdup(arg2);
	ap->speed = speed;
	asy_open(dev);
	return 0;
}

/*---------------------------------------------------------------------------*/

int
asy_stop(ifp)
struct iface *ifp;
{
	register struct asy *ap;

	ap = &Asy[ifp->dev];

	if (ap->fd > 0) {
		off_read(ap->fd);
		off_write(ap->fd);
		free_q(&ap->sndq);
		close(ap->fd);
		ap->fd = -1;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Set asynch line speed */
int
asy_speed(dev,bps)
int dev;
long bps;
{

	struct asy *asyp;
	int sp;
	struct termio termio;

	if(bps <= 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if(asyp->iface == NULLIF)
		return -1;

	if (asyp->fd <= 0 || !strncmp(asyp->iface->name, "ipc", 3))
		return (-1);
	sp = find_speed(bps);
	if (ioctl(asyp->fd, TCGETA, &termio))
		return (-1);
	termio.c_cflag = (termio.c_cflag & ~CBAUD) | speed_table[sp].flags;
	if (ioctl(asyp->fd, TCSETA, &termio))
		return (-1);
	asyp->speed = speed_table[sp].speed;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Asynchronous line I/O control */
int32
asy_ioctl(ifp,cmd,set,val)
struct iface *ifp;
int cmd;
int set;
int32 val;
{
	switch(cmd){
	case PARAM_SPEED:
		if(set)
			asy_speed(ifp->dev,val);
		return Asy[ifp->dev].speed;
	};
	return -1;
}

/*---------------------------------------------------------------------------*/

int
get_asy(dev, buf, cnt)
int dev;
char *buf;
int cnt;
{
	struct asy *ap;

	ap = Asy + dev;
	if (ap->fd <= 0 && asy_open(dev))
		return 0;
	cnt = read(ap->fd, buf, (unsigned) cnt);
	ap->rxints++;
	if (cnt <= 0) {
		asy_stop(ap->iface);
		return 0;
	}
	ap->rxchar += cnt;
	if (ap->rxhiwat < cnt)
		ap->rxhiwat = cnt;
	return cnt;
}

/*---------------------------------------------------------------------------*/

int
doasystat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct asy *asyp;

	for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
		if(asyp->iface == NULLIF)
			continue;

		tprintf("%s:",asyp->iface->name);
		tprintf(" %d bps\n",asyp->speed);

		tprintf(" RX: %lu int, %lu chr, %lu hw hi\n",
			asyp->rxints,
			asyp->rxchar,
			asyp->rxhiwat);
		asyp->rxhiwat = 0;

		if(tprintf(" TX: %lu int, %lu chr, %u q\n",
			asyp->txints,
			asyp->txchar,
			len_p(asyp->sndq)) == EOF)
			break;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Send a message on the specified serial line */
int
asy_send(dev,bp)
int dev;
struct mbuf *bp;
{
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX)
		return -1;
	asyp = &Asy[dev];
	if (asyp->fd > 0 || !asy_open(dev)) {
		append(&asyp->sndq, bp);
		on_write(asyp->fd, (void (*)()) asy_tx, asyp);
	} else
		free_p(bp);
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Serial transmit process, common to all protocols */
static void
asy_tx(asyp)
struct asy *asyp;
{
	int  n;

	if (asyp->sndq != NULLBUF) {
		n = write(asyp->fd, asyp->sndq->data, asyp->sndq->cnt);
		if (n <= 0) {
			asy_stop(asyp->iface);
			return;
		}
		asyp->sndq->data += n;
		asyp->sndq->cnt -= n;
		asyp->txints++;
		asyp->txchar += n;
	}
	while (asyp->sndq != NULLBUF && asyp->sndq->cnt == 0)
		asyp->sndq = free_mbuf(asyp->sndq);
	if (asyp->sndq == NULLBUF)
		off_write(asyp->fd);
}
