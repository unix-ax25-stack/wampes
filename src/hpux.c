#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <termio.h>
#include <time.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "asy.h"
#include "unix.h"
#include "hpux.h"

extern char  *getenv();
extern int  abort();
extern int  debug;
extern int  errno;
extern long  sigsetmask();
extern long  time();
extern struct mbuf *loopq;
extern unsigned long  alarm();
extern void _exit();

char *readarg[_NFILE];
int  filemask[2];
int  readmask[2];
long  currtime;
struct asy asy[ASY_MAX];
struct interface *ifaces;
unsigned  nasy;
void (*readfnc[_NFILE])();

static int  local_kbd;
static long  lasttime;
static struct termio curr_termio;
static struct termio prev_termio;

/*---------------------------------------------------------------------------*/

static void sigpipe_handler(sig, code, scp)
int  sig;
int  code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

int  ioinit()
{

  int i;
  struct sigvec vec;

  if (local_kbd = isatty(0)) {
    ioctl(0, TCGETA, &prev_termio);
    memcpy(&curr_termio, &prev_termio, sizeof(struct termio ));
    curr_termio.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
    curr_termio.c_lflag = 0;
    curr_termio.c_cc[VMIN] = 0;
    curr_termio.c_cc[VTIME] = 0;
    ioctl(0, TCSETA, &curr_termio);
    setmask(filemask, 0);
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
  if (!debug) alarm(60l);
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

int  iostop()
{
  register struct interface *ifp;

  if (local_kbd) ioctl(0, TCSETA, &prev_termio);
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->stop) (*ifp->stop)(ifp->dev);
}

/*---------------------------------------------------------------------------*/

int  kbread()
{

  static char  inbuf[256], *inptr;
  static int  incnt;

  if (!local_kbd) return remote_kbd();
  if (incnt <= 0 && maskset(readmask, 0)) {
    clrmask(readmask, 0);
    incnt = read(0, inptr = inbuf, sizeof(inbuf));
  }
  if (incnt <= 0) return (-1);
  incnt--;
  return uchar(*inptr++);
}

/*---------------------------------------------------------------------------*/

int  asy_stop(dev)
int  dev;
{
  if (asy[dev].fd > 0) {
    close(asy[dev].fd);
    clrmask(filemask, asy[dev].fd);
    asy[dev].fd = -1;
  }
}

/*---------------------------------------------------------------------------*/

static int  asy_open(dev)
int16 dev;
{

#define MIN_WAIT (3*60)
#define MAX_WAIT (3*60*60)

  static struct {
    long  next;
    long  wait;
  } times[ASY_MAX];

  char  buf[80];
  int  addrlen;
  register struct asy *asp;
  register struct interface *ifp;
  struct sockaddr *addr;
  struct sockaddr *build_sockaddr();
  struct termio termio;

  if (times[dev].next > currtime) return (-1);
  times[dev].wait *= 2;
  if (times[dev].wait < MIN_WAIT) times[dev].wait = MIN_WAIT;
  if (times[dev].wait > MAX_WAIT) times[dev].wait = MAX_WAIT;
  times[dev].next = currtime + times[dev].wait;
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->dev == dev && ifp->stop == asy_stop) break;
  if (!ifp) return (-1);
  asy_stop(dev);
  asp = asy + dev;
  if (!strncmp(ifp->name, "ipc", 3)) {
    if (!(addr = build_sockaddr(asp->tty, &addrlen))) return (-1);
    if ((asp->fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) return (-1);
    if (connect(asp->fd, addr, addrlen)) {
      close(asp->fd);
      asp->fd = -1;
      return (-1);
    }
    asp->speed = 0;
  } else {
    strcpy(buf, "/dev/");
    strcat(buf, ifp->name);
    if ((asp->fd = open(buf, O_RDWR)) < 0) return (-1);
    termio.c_iflag = IGNBRK | IGNPAR;
    termio.c_oflag = 0;
    termio.c_cflag = B9600 | CS8 | CREAD | CLOCAL;
    termio.c_lflag = 0;
    termio.c_line = 0;
    termio.c_cc[VMIN] = 0;
    termio.c_cc[VTIME] = 0;
    ioctl(asp->fd, TCSETA, &termio);
    ioctl(asp->fd, TCFLSH, 2);
    asp->speed = 9600;
  }
  setmask(filemask, asp->fd);
  return 0;
}

/*---------------------------------------------------------------------------*/

int  asy_init(dev, arg1, arg2, bufsize)
int16 dev;
char  *arg1, *arg2;
unsigned  bufsize;
{
  asy[dev].tty = strcpy(malloc((unsigned) (strlen(arg2) + 1)), arg2);
  return asy_open(dev);
}

/*---------------------------------------------------------------------------*/

int  asy_speed(dev, speed)
int  dev, speed;
{

  register struct asy *asp;
  register struct interface *ifp;
  struct termio termio;

  asp = asy + dev;
  for (ifp = ifaces; ifp; ifp = ifp->next)
    if (ifp->dev == dev && ifp->stop == asy_stop) break;
  if (!ifp || asp->fd <= 0 || !strncmp(ifp->name, "ipc", 3)) return (-1);
  if (ioctl(asp->fd, TCGETA, &termio)) return (-1);
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
  if (ioctl(asp->fd, TCSETA, &termio)) return (-1);
  asp->speed = speed;
  return 0;
}

/*---------------------------------------------------------------------------*/

asy_ioctl(interface, argc, argv)
struct interface *interface;
int  argc;
char  *argv[];
{
  if (argc < 1) {
    printf("%d\n", asy[interface->dev].speed);
    return 0;
  }
  return asy_speed(interface->dev, atoi(argv[0]));
}

/*---------------------------------------------------------------------------*/

int  stxrdy(dev)
int  dev;
{
  return 1;
}

/*---------------------------------------------------------------------------*/

int  asy_output(dev, buf, cnt)
int  dev;
char  *buf;
unsigned short  cnt;
{
  if (asy[dev].fd <= 0 && asy_open(dev) < 0) return;
  if (dowrite(asy[dev].fd, buf, (unsigned) cnt) < 0) asy_open(dev);
}

/*---------------------------------------------------------------------------*/

int  asy_recv(dev, buf, cnt)
int  dev;
char  *buf;
unsigned  cnt;
{
  register struct asy *asp;

  if (cnt != 1) {
    printf("*** asy_recv called with (cnt != 1); Programm aborted ***\n");
    abort();
  }

  asp = asy + dev;
  if (asp->incnt <= 0) {
    if (asy[dev].fd <= 0 && asy_open(dev) < 0) return 0;
    if (!maskset(readmask, asp->fd)) return 0;
    clrmask(readmask, asp->fd);
    asp->incnt = read(asp->fd, asp->inptr = asp->inbuf, sizeof(asp->inbuf));
    if (asp->incnt <= 0) {
      asy_open(dev);
      return 0;
    }
  }
  asp->incnt--;
  *buf = *asp->inptr++;
  return 1;
}

/*---------------------------------------------------------------------------*/

/* checks the time then ticks and updates ISS */

void check_time()
{
  currtime = time(0);
  if (!lasttime) lasttime = currtime;
  while (lasttime < currtime) { /* Handle possibility of several missed ticks */
    lasttime++;
    icmpclk();                  /* Call this one before tick */
    tick();
    iss();
  }
}

/*---------------------------------------------------------------------------*/

int  system(cmdline)
char  *cmdline;
{

  int  i, pid, status;
  long  oldmask;

  switch (pid = fork()) {
  case -1:
    return (-1);
  case 0:
    for (i = 3; i < _NFILE; i++) close(i);
    execl("/bin/sh", "sh", "-c", cmdline, 0);
    _exit(127);
  default:
    oldmask = sigsetmask(-1);
    while (wait(&status) != pid) ;
    sigsetmask(oldmask);
    return status;
  }
}

/*---------------------------------------------------------------------------*/

static char  *strquote_for_shell(s1, s2)
char  *s1, *s2;
{
  char  *p = s1;

  while (*s2) {
    *p++ = '\\';
    *p++ = *s2++;
  }
  *p = '\0';
  return s1;
}

/*---------------------------------------------------------------------------*/

FILE *dir(path, full)
char  *path;
int  full;
{

  FILE * fp;
  char  buf[1024];
  char  cmd[1024];
  char  fname[L_tmpnam];

  tmpnam(fname);
  sprintf(cmd, "/bin/ls -A %s %s >%s 2>&1",
	       full ? "-l" : "",
	       strquote_for_shell(buf, path),
	       fname);
  fp = system(cmd) ? 0 : fopen(fname, "r");
  unlink(fname);
  return fp;
}

/*---------------------------------------------------------------------------*/

int  disable()
{
  return 0;
}

/*---------------------------------------------------------------------------*/

/*ARGSUSED*/
int  restore(i_state)
char  i_state;
{
}

/*---------------------------------------------------------------------------*/

eihalt()
{

  int  status;
  register int  nfds;
  register unsigned int  i;
  struct timeval t;
  struct timeval timeout;
  struct timezone tz;

  wait3(&status, -1, (int *) 0);
  if (!debug) alarm(60l);
  timeout.tv_sec = timeout.tv_usec = 0;
  if (!loopq) {
    gettimeofday(&t, &tz);
    currtime = t.tv_sec;
    if (currtime == lasttime) timeout.tv_usec = 999999 - t.tv_usec;
  }
  if (filemask[1]) {
    i = filemask[1];
    nfds = 32;
  } else {
    i = filemask[0];
    nfds = 0;
  }
  while (i) {
    nfds++;
    i >>= 1;
  }
  readmask[0] = filemask[0];
  readmask[1] = filemask[1];
  if (select(nfds, readmask, (int *) 0, (int *) 0, &timeout) < 1)
    readmask[0] = readmask[1] = 0;
  else
    for (i = 0; i < nfds; i++)
      if (readfnc[i] && maskset(readmask, i)) (*readfnc[i])(readarg[i]);
}

