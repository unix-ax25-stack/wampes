/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.15 1990/10/26 19:20:25 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

#include "global.h"
#include "iface.h"
#include "asy.h"
#include "files.h"
#include "hardware.h"
#include "hpux.h"

extern int  debug;
extern long  sigsetmask();
extern void _exit();

#define TIMEOUT 120

struct asy {
  struct iface *iface;  /* Associated interface structure */
  int  fd;              /* File descriptor */
  int  speed;           /* Line speed */
  char  *ipc_socket;    /* Host:port of ipc destination */
  long  rxints;         /* receive interrupts */
  long  txints;         /* transmit interrupts */
  long  rxchar;         /* Received characters */
  long  txchar;         /* Transmitted characters */
  long  rxhiwat;        /* High water mark on rx fifo */
};

int  Nasy;
static struct asy Asy[ASY_MAX];

long  currtime;
static long  lasttime;

int  chkread[2];
int  actread[2];
void (*readfnc[_NFILE])();
char *readarg[_NFILE];

int  chkwrite[2];
int  actwrite[2];
void (*writefnc[_NFILE])();
char *writearg[_NFILE];

int  chkexcp[2];
int  actexcp[2];
void (*excpfnc[_NFILE])();
char *excparg[_NFILE];

static int  local_kbd;

static struct termio curr_termio;
static struct termio prev_termio;

static int asy_open __ARGS((int dev));
static void check_files_changed __ARGS((void));

/*---------------------------------------------------------------------------*/

static void sigpipe_handler(sig, code, scp)
int  sig;
int  code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

void ioinit()
{

  int i;
  struct sigvec vec;

  if (local_kbd = isatty(0)) {
    ioctl(0, TCGETA, &prev_termio);
    curr_termio = prev_termio;
    curr_termio.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
    curr_termio.c_lflag = 0;
    curr_termio.c_cc[VMIN] = 0;
    curr_termio.c_cc[VTIME] = 0;
    ioctl(0, TCSETA, &curr_termio);
    printf("\033&s1A");   /* enable XmitFnctn */
    fflush(stdout);
    setmask(chkread, 0);
  } else {
    for (i = 0; i < _NFILE; i++) close(i);
    setpgrp();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
  }
  setvbuf(stdout, NULL, _IOLBF, BUFSIZ);
  vec.sv_mask = vec.sv_flags = 0;
  vec.sv_handler = sigpipe_handler;
  sigvector(SIGPIPE, &vec, (struct sigvec *) 0);
  vec.sv_handler = (void (*)()) abort;
  sigvector(SIGALRM, &vec, (struct sigvec *) 0);
  if (!debug) alarm(TIMEOUT);
  umask(022);
  if (!debug) rtprio(0, 127);
  if (!getenv("HOME"))
    putenv("HOME=/users/root");
  if (!getenv("LOGNAME"))
    putenv("LOGNAME=root");
  if (!getenv("PATH"))
    putenv("PATH=/bin:/usr/bin:/usr/contrib/bin:/usr/local/bin");
  if (!getenv("SHELL"))
    putenv("SHELL=/bin/sh");
  if (!getenv("TZ"))
    putenv("TZ=MEZ-1MESZ");
  currtime = time(0);
  fixutmpfile();
}

/*---------------------------------------------------------------------------*/

void iostop()
{
  register struct iface *ifp;

  if (local_kbd) {
    ioctl(0, TCSETA, &prev_termio);
    printf("\033&s0A");   /* disable XmitFnctn */
    fflush(stdout);
  }
  for (ifp = Ifaces; ifp; ifp = ifp->next)
    if (ifp->stop) (*ifp->stop)(ifp);
}

/*---------------------------------------------------------------------------*/

int  kbread()
{

  static char  buf[256], *ptr;
  static int  cnt;

  if (!local_kbd) return remote_kbd();
  if (cnt <= 0 && maskset(actread, 0)) {
    clrmask(actread, 0);
    cnt = read(0, ptr = buf, sizeof(buf));
  }
  if (cnt <= 0) return (-1);
  cnt--;
  return uchar(*ptr++);
}

/*---------------------------------------------------------------------------*/

int  asy_stop(iface)
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
  struct sockaddr *build_sockaddr();
  struct termio termio;

  if (times[dev].next > currtime) return (-1);
  times[dev].wait *= 2;
  if (times[dev].wait < MIN_WAIT) times[dev].wait = MIN_WAIT;
  if (times[dev].wait > MAX_WAIT) times[dev].wait = MAX_WAIT;
  times[dev].next = currtime + times[dev].wait;
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
    ap->speed = 0;
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

int  asy_init(dev, iface, arg1, arg2, bufsize, trigchar, cts)
int  dev;
struct iface *iface;
char  *arg1, *arg2;
unsigned  bufsize;
int  trigchar;
char  cts;
{
  Asy[dev].iface = iface;
  Asy[dev].ipc_socket = strdup(arg2);
  return asy_open(dev);
}

/*---------------------------------------------------------------------------*/

int  asy_speed(dev, speed)
int  dev;
long  speed;
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
  case    50: termio.c_cflag |= B50;    break;
  case    75: termio.c_cflag |= B75;    break;
  case   110: termio.c_cflag |= B110;   break;
  case   134: termio.c_cflag |= B134;   break;
  case   150: termio.c_cflag |= B150;   break;
  case   200: termio.c_cflag |= B200;   break;
  case   300: termio.c_cflag |= B300;   break;
  case   600: termio.c_cflag |= B600;   break;
  case   900: termio.c_cflag |= B900;   break;
  case  1200: termio.c_cflag |= B1200;  break;
  case  1800: termio.c_cflag |= B1800;  break;
  case  2400: termio.c_cflag |= B2400;  break;
  case  3600: termio.c_cflag |= B3600;  break;
  case  4800: termio.c_cflag |= B4800;  break;
  case  7200: termio.c_cflag |= B7200;  break;
  case  9600: termio.c_cflag |= B9600;  break;
  case 19200: termio.c_cflag |= B19200; break;
  case 38400: termio.c_cflag |= B38400; break;
  default:    return (-1);
  }
  if (ioctl(ap->fd, TCSETA, &termio)) return (-1);
  ap->speed = speed;
  return 0;
}

/*---------------------------------------------------------------------------*/

int  asy_ioctl(iface, argc, argv)
struct iface *iface;
int  argc;
char  *argv[];
{
  if (argc < 1) {
    tprintf("%u\n", Asy[iface->dev].speed);
    return 0;
  }
  return asy_speed(iface->dev, atoi(argv[0]));
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

int  asy_send(dev, bp)
int  dev;
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

/*---------------------------------------------------------------------------*/

int  doasystat(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  register struct asy *asyp;

  for (asyp = Asy; asyp < &Asy[Nasy]; asyp++) {
    tprintf("%s:\n", asyp->iface->name);
    tprintf(" RX: int %lu chr %lu hiwat %lu\n",
     asyp->rxints, asyp->rxchar, asyp->rxhiwat);
    if (tprintf(" TX: int %lu chr %lu\n", asyp->txints, asyp->txchar) == EOF)
      break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* checks the time then ticks and updates ISS */

void check_time()
{
  currtime = time(0);
  if (!lasttime) lasttime = currtime;
  while (lasttime < currtime) { /* Handle possibility of several missed ticks */
    lasttime++;
    tick();
  }
}

/*---------------------------------------------------------------------------*/

int  system(cmdline)
const char *cmdline;
{

  int  i, pid, status;
  long  oldmask;

  if (!cmdline) return 1;
  switch (pid = fork()) {
  case -1:
    return (-1);
  case 0:
    for (i = 3; i < _NFILE; i++) close(i);
    execl("/bin/sh", "sh", "-c", cmdline, (char *) 0);
    _exit(127);
  default:
    oldmask = sigsetmask(-1);
    while (wait(&status) != pid) ;
    sigsetmask(oldmask);
    return status;
  }
}

/*---------------------------------------------------------------------------*/

int  _system(cmdline)
char  *cmdline;
{
  return system(cmdline);
}

/*---------------------------------------------------------------------------*/

int  doshell(argc, argv, p)
int  argc;
char  *argv[];
void *p;
{
  char  buf[2048];

  *buf = '\0';
  while (--argc > 0) {
    if (*buf) strcat(buf, " ");
    strcat(buf, *++argv);
  }
  return system(buf);
}

/*---------------------------------------------------------------------------*/

static void check_files_changed()
{

  int  changed = 0;
  static long  nexttime, net_time, rc_time;
  struct stat statbuf;

  if (debug || nexttime > currtime) return;
  nexttime = currtime + 600;

  if (stat("/tcp/net", &statbuf)) return;
  if (!net_time) net_time = statbuf.st_mtime;
  if (net_time != statbuf.st_mtime && statbuf.st_mtime < currtime - 3600)
    changed = 1;

  if (stat(Startup, &statbuf)) return;
  if (!rc_time) rc_time = statbuf.st_mtime;
  if (rc_time != statbuf.st_mtime && statbuf.st_mtime < currtime - 3600)
    changed = 1;

  if (changed) doexit(0, (char **) 0, (void *) 0);
}

/*---------------------------------------------------------------------------*/

eihalt()
{

  int  n;
  int  status;
  register int  nfds;
  register unsigned int  i;
  struct timeval t;
  struct timeval timeout;
  struct timezone tz;

  check_files_changed();
  wait3(&status, WNOHANG, (int *) 0);
  if (!debug) alarm(TIMEOUT);
  timeout.tv_sec = timeout.tv_usec = 0;
  if (!Hopper) {
    gettimeofday(&t, &tz);
    currtime = t.tv_sec;
    if (currtime == lasttime) timeout.tv_usec = 999999 - t.tv_usec;
  }
  if (chkread[1] | chkwrite[1] | chkexcp[1]) {
    i = chkread[1] | chkwrite[1] | chkexcp[1];
    nfds = 32;
  } else {
    i = chkread[0] | chkwrite[0] | chkexcp[0];
    nfds = 0;
  }
  for (n = 16; n; n >>= 1)
    if (i & ((-1) << n)) {
      nfds += n;
      i >>= n;
    }
  if (i) nfds++;
  actread [0] = chkread [0];
  actread [1] = chkread [1];
  actwrite[0] = chkwrite[0];
  actwrite[1] = chkwrite[1];
  actexcp [0] = chkexcp [0];
  actexcp [1] = chkexcp [1];
  if (select(nfds, actread, actwrite, actexcp, &timeout) < 1) {
    actread [0] = actread [1] = 0;
    actwrite[0] = actwrite[1] = 0;
    actexcp [0] = actexcp [1] = 0;
  } else
    for (i = 0; i < nfds; i++) {
      if (readfnc [i] && maskset(actread , i)) (*readfnc [i])(readarg [i]);
      if (writefnc[i] && maskset(actwrite, i)) (*writefnc[i])(writearg[i]);
      if (excpfnc [i] && maskset(actexcp , i)) (*excpfnc [i])(excparg [i]);
    }
}

