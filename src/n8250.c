/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/n8250.c,v 1.20 1992/08/11 21:32:12 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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
#include "n8250.h"
#include "asy.h"
#include "devparam.h"
#include "hpux.h"
#include "timer.h"

static int find_speed __ARGS((long speed));
static void pasy __ARGS((struct asy *asyp));
static void asy_tx __ARGS((struct asy *asyp));

struct asy Asy[ASY_MAX];

/*---------------------------------------------------------------------------*/

static struct {
	long speed;
	int flags;
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
long speed;
{
	int i;

	i = 0;
	while (speed_table[i].speed < speed && speed_table[i+1].speed > 0)
		i++;
	return i;
}

/*---------------------------------------------------------------------------*/

/* Initialize asynch port "dev" */
int
asy_init(dev,ifp,arg1,arg2,bufsize,trigchar,speed,cts,rlsd)
int dev;
struct iface *ifp;
char *arg1,*arg2;       /* Attach args for address and vector */
int16 bufsize;
int trigchar;
long speed;
int cts;                /* Use CTS flow control */
int rlsd;               /* Use Received Line Signal Detect (aka CD) */
{
	register struct asy *ap;
	char filename[80];
	int sp;
	long arg;
	struct termio termio;

	ap = &Asy[dev];
	strcpy(filename, "/dev/");
	strcat(filename, ifp->name);
	if ((ap->fd = open(filename, O_RDWR, 0666)) < 0) goto Fail;
	ap->iface = ifp;
	sp = find_speed(speed);
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
	ioctl(ap->fd, FIOSNBIO, &arg);  /*** will fail on pty master side ***/
	on_read(ap->fd, (void (*)()) ifp->rxproc, ifp);
	return 0;

Fail:
	if (ap->fd >= 0) close(ap->fd);
	ap->iface = NULLIF;
	return -1;
}

/*---------------------------------------------------------------------------*/

int
asy_stop(ifp)
struct iface *ifp;
{
	register struct asy *ap;

	ap = &Asy[ifp->dev];

	if(ap->iface == NULLIF)
		return -1;      /* Not allocated */
	ap->iface = NULLIF;

	off_read(ap->fd);
	off_write(ap->fd);
	free_q(&ap->sndq);
	close(ap->fd);

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

	if(bps == 0)
		return -1;
	sp = find_speed(bps);
	if (ioctl(asyp->fd, TCGETA, &termio))
		return -1;
	termio.c_cflag = (termio.c_cflag & ~CBAUD) | speed_table[sp].flags;
	if (ioctl(asyp->fd, TCSETA, &termio))
		return -1;
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
	struct asy *ap = &Asy[ifp->dev];

	switch(cmd){
	case PARAM_SPEED:
		if(set)
			asy_speed(ifp->dev,val);
		return ap->speed;
	}
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

	ap = &Asy[dev];
	if(ap->iface == NULLIF)
		return 0;
	cnt = read(ap->fd,buf,cnt);
	ap->rxints++;
	if (cnt <= 0)
		return 0;
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
	struct iface *ifp;
	int i;

	if(argc < 2){
		for(asyp = Asy;asyp < &Asy[ASY_MAX];asyp++){
			if(asyp->iface != NULLIF)
				pasy(asyp);
		}
		return 0;
	}
	for(i=1;i<argc;i++){
		if((ifp = if_lookup(argv[i])) == NULLIF){
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
pasy(asyp)
struct asy *asyp;
{

	printf("%s:",asyp->iface->name);

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
asy_tx(asyp)
struct asy *asyp;
{
	int n;

	if (asyp->sndq != NULLBUF) {
		n = write(asyp->fd, asyp->sndq->data, asyp->sndq->cnt);
		asyp->txints++;
		if (n > 0) {
			asyp->txchar += n;
			asyp->sndq->data += n;
			asyp->sndq->cnt -= n;
			if(asyp->sndq->cnt == 0){
				asyp->sndq = free_mbuf(asyp->sndq);
				if (asyp->sndq == NULLBUF)
					off_write(asyp->fd);
			}
		}
	}
}

/*---------------------------------------------------------------------------*/

/* Send a message on the specified serial line */
int
asy_send(dev,bp)
int dev;
struct mbuf *bp;
{
	struct asy *asyp;

	if(dev < 0 || dev >= ASY_MAX){
		free_p(bp);
		return -1;
	}
	asyp = &Asy[dev];

	if(asyp->iface == NULLIF)
		free_p(bp);
	else {
		append(&asyp->sndq, bp);
		on_write(asyp->fd, (void (*)()) asy_tx, asyp);
	}
	return 0;
}

