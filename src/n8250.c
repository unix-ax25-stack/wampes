/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/n8250.c,v 1.4 1991/04/17 19:47:05 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <termio.h>
#include <unistd.h>

#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "asy.h"
#include "slip.h"
#include "nrs.h"
#include "hpux.h"
#include "8250.h"
#include "devparam.h"

extern struct sockaddr *build_sockaddr();

static int asy_open __ARGS((int dev));

struct asy Asy[ASY_MAX];

/*---------------------------------------------------------------------------*/

static int  asy_open(dev)
int  dev;
{

#define MIN_WAIT (3*60)
#define MAX_WAIT (3*60*60)

  static struct {
    long  next;
    long  wait;
  } times[ASY_MAX];

  char  buf[80];
  int  addrlen;
  register struct asy *ap;
  register struct iface *ifp;
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
    if (!(addr = build_sockaddr(ap->ipc_socket, &addrlen))) return (-1);
    if ((ap->fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) return (-1);
    if (connect(ap->fd, addr, addrlen)) {
      close(ap->fd);
      ap->fd = -1;
      return (-1);
    }
    ap->speed = -1;
  } else {
    strcpy(buf, "/dev/");
    strcat(buf, ifp->name);
    if ((ap->fd = open(buf, O_RDWR)) < 0) return (-1);
    termio.c_iflag = IGNBRK | IGNPAR;
    termio.c_oflag = 0;
    termio.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
    termio.c_lflag = 0;
    termio.c_line = 0;
    termio.c_cc[VMIN] = 0;
    termio.c_cc[VTIME] = 0;
    ioctl(ap->fd, TCSETA, &termio);
    ioctl(ap->fd, TCFLSH, 2);
    ap->speed = 9600;
  }
  setmask(chkread, ap->fd);
  times[dev].wait = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Initialize asynch port "dev" */
int
asy_init(dev,iface,arg1,arg2,bufsize,trigchar,cts,rlsd)
int dev;
struct iface *iface;
char *arg1,*arg2;       /* Attach args for address and vector */
unsigned bufsize;
int trigchar;
char cts;
char rlsd;
{
  Asy[dev].iface = iface;
  Asy[dev].ipc_socket = strdup(arg2);
  return asy_open(dev);
}

/*---------------------------------------------------------------------------*/

int
asy_stop(iface)
struct iface *iface;
{
  register struct asy *ap;

  ap = Asy + iface->dev;
  if (ap->fd > 0) {
    close(ap->fd);
    clrmask(chkread, ap->fd);
    ap->fd = -1;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Asynchronous line I/O control */
int32
asy_ioctl(iface,cmd,set,val)
struct iface *iface;
int cmd;
int set;
int32 val;
{
	switch(cmd){
	case PARAM_SPEED:
		if(set){
			asy_speed(iface->dev,val,0L);
		}
		return Asy[iface->dev].speed;
	}
	return -1;
}

/*---------------------------------------------------------------------------*/

/* Set asynch line speed */
int
asy_speed(dev,speed,autospeed)
int dev;
long speed;
long autospeed;
{

  register struct asy *ap;
  register struct iface *ifp;
  struct termio termio;

  ap = Asy + dev;
  ifp = ap->iface;
  if (!ifp || ap->fd <= 0 || !strncmp(ifp->name, "ipc", 3)) return (-1);
  if (ioctl(ap->fd, TCGETA, &termio)) return (-1);
  termio.c_cflag &= ~CBAUD;
  switch (speed) {
#ifdef    B50
  case     50: termio.c_cflag |= B50;    break;
#endif
#ifdef    B75
  case     75: termio.c_cflag |= B75;    break;
#endif
#ifdef   B110
  case    110: termio.c_cflag |= B110;   break;
#endif
#ifdef   B134
  case    134: termio.c_cflag |= B134;   break;
#endif
#ifdef   B150
  case    150: termio.c_cflag |= B150;   break;
#endif
#ifdef   B200
  case    200: termio.c_cflag |= B200;   break;
#endif
#ifdef   B300
  case    300: termio.c_cflag |= B300;   break;
#endif
#ifdef   B600
  case    600: termio.c_cflag |= B600;   break;
#endif
#ifdef   B900
  case    900: termio.c_cflag |= B900;   break;
#endif
#ifdef  B1200
  case   1200: termio.c_cflag |= B1200;  break;
#endif
#ifdef  B1800
  case   1800: termio.c_cflag |= B1800;  break;
#endif
#ifdef  B2400
  case   2400: termio.c_cflag |= B2400;  break;
#endif
#ifdef  B3600
  case   3600: termio.c_cflag |= B3600;  break;
#endif
#ifdef  B4800
  case   4800: termio.c_cflag |= B4800;  break;
#endif
#ifdef  B7200
  case   7200: termio.c_cflag |= B7200;  break;
#endif
#ifdef  B9600
  case   9600: termio.c_cflag |= B9600;  break;
#endif
#ifdef B19200
  case  19200: termio.c_cflag |= B19200; break;
#endif
#ifdef B38400
  case  38400: termio.c_cflag |= B38400; break;
#endif
  default:     return (-1);
  }
  if (ioctl(ap->fd, TCSETA, &termio)) return (-1);
  ap->speed = speed;
  return 0;
}

/*---------------------------------------------------------------------------*/

int  get_asy(dev, buf, cnt)
int  dev;
char  *buf;
int  cnt;
{
  struct asy *ap;

  ap = Asy + dev;
  if (Asy[dev].fd <= 0 && asy_open(dev) < 0) return 0;
  if (!maskset(actread, ap->fd)) return 0;
  cnt = read(ap->fd, buf, (unsigned) cnt);
  ap->rxints++;
  if (cnt <= 0) {
    asy_open(dev);
    return 0;
  }
  ap->rxchar += cnt;
  if (ap->rxhiwat < cnt) ap->rxhiwat = cnt;
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

  for (asyp = Asy; asyp < &Asy[ASY_MAX]; asyp++) {
    if (asyp->iface == NULLIF) continue;
    tprintf("%s:", asyp->iface->name);
    tprintf(" [%ld baud]", asyp->speed);
    tprintf("\n");
    tprintf(" RX: int %lu chr %lu hiwat %lu",
	    asyp->rxints, asyp->rxchar, asyp->rxhiwat);
    asyp->rxhiwat = 0;
    tprintf("\n");
    if (tprintf(" TX: int %lu chr %lu\n",
		asyp->txints, asyp->txchar) == EOF) break;
    /* Show more stats if SLIP and VJ TCP compression */
    doslstat(asyp->iface);
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

  char  buf[4096];
  int  cnt;
  struct asy *ap;

  ap = Asy + dev;
  while (cnt = pullup(&bp, buf, sizeof(buf)))
    if (ap->fd > 0 || !asy_open(dev)) {
      if (dowrite(ap->fd, buf, (unsigned) cnt) < 0) asy_open(dev);
      ap->txints++;
      ap->txchar += cnt;
    }
  return 0;
}

