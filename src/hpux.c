/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.22 1992/08/19 13:20:27 deyke Exp $ */

#define FD_SETSIZE 64

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <termio.h>
#include <unistd.h>

#ifdef __hpux
#include <sys/rtprio.h>
#endif

#include "global.h"
#include "iface.h"
#include "timer.h"
#include "files.h"
#include "hardware.h"
#include "login.h"
#include "commands.h"
#include "main.h"
#include "hpux.h"

#define TIMEOUT 120

static struct fd_set chkread;
static struct fd_set actread;
static void (*readfnc[FD_SETSIZE]) __ARGS((void *));
static void *readarg[FD_SETSIZE];

static struct fd_set chkwrite;
static struct fd_set actwrite;
static void (*writefnc[FD_SETSIZE]) __ARGS((void *));
static void *writearg[FD_SETSIZE];

static struct fd_set chkexcp;
static struct fd_set actexcp;
static void (*excpfnc[FD_SETSIZE]) __ARGS((void *));
static void *excparg[FD_SETSIZE];

static int maxfd = -1;

static int local_kbd;

static struct termio curr_termio;
static struct termio prev_termio;

static void check_files_changed __ARGS((void));

/*---------------------------------------------------------------------------*/

/*ARGSUSED*/
static void sigpipe_handler(sig, code, scp)
int sig;
int code;
struct sigcontext *scp;
{
}

/*---------------------------------------------------------------------------*/

void rtprio_off()
{
#ifdef __hpux
  rtprio(0, RTPRIO_RTOFF);
#endif
}

/*---------------------------------------------------------------------------*/

void ioinit()
{

  int i;
  struct sigvec vec;

#define fixdir(name, mode) \
	{ mkdir((name), (mode)); chmod((name), (mode)); }

  fixdir("/tcp", 0755);
  fixdir("/tcp/sockets", 0755);
  fixdir("/tcp/.sockets", 0700);
  fixdir("/tcp/logs", 0700);

  if (local_kbd = isatty(0)) {
    ioctl(0, TCGETA, &prev_termio);
    curr_termio = prev_termio;
    curr_termio.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
    curr_termio.c_lflag = 0;
    curr_termio.c_cc[VMIN] = 0;
    curr_termio.c_cc[VTIME] = 0;
    ioctl(0, TCSETA, &curr_termio);
    printf("\033&s1A");   /* enable XmitFnctn */
    on_read(0, (void (*)()) keyboard, (void *) 0);
  } else {
    for (i = 0; i < FD_SETSIZE; i++) close(i);
    setpgrp();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    remote_kbd_initialize();
  }
  vec.sv_mask = vec.sv_flags = 0;
  vec.sv_handler = sigpipe_handler;
  sigvector(SIGPIPE, &vec, (struct sigvec *) 0);
  vec.sv_handler = (void (*)()) abort;
  sigvector(SIGALRM, &vec, (struct sigvec *) 0);
  if (!Debug) alarm(TIMEOUT);
  umask(022);
#ifdef __hpux
  if (!Debug) rtprio(0, 127);
#endif
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

  {
    /* Init times */
    struct timeval tv;
    struct timezone tz;
    gettimeofday(&tv, &tz);
    Secclock = tv.tv_sec;
    Msclock = 1000 * Secclock + tv.tv_usec / 1000;
  }

  fixutmpfile();
}

/*---------------------------------------------------------------------------*/

void iostop()
{
  register struct iface *ifp;

  if (local_kbd) {
    ioctl(0, TCSETA, &prev_termio);
    printf("\033&s0A");   /* disable XmitFnctn */
  }
  for (ifp = Ifaces; ifp; ifp = ifp->next)
    if (ifp->stop) (*ifp->stop)(ifp);
}

/*---------------------------------------------------------------------------*/

int system(cmdline)
const char *cmdline;
{

  int i, pid, status;
  long oldmask;

  if (!cmdline) return 1;
  fflush(stdout);
  switch (pid = fork()) {
  case -1:
    return (-1);
  case 0:
    for (i = 3; i < FD_SETSIZE; i++) close(i);
    execl("/bin/sh", "sh", "-c", cmdline, (char *) 0);
    exit(1);
  default:
    oldmask = sigsetmask(-1);
    while (wait(&status) != pid) ;
    sigsetmask(oldmask);
    return status;
  }
}

/*---------------------------------------------------------------------------*/

int _system(cmdline)
char *cmdline;
{
  return system(cmdline);
}

/*---------------------------------------------------------------------------*/

int doshell(argc, argv, p)
int argc;
char *argv[];
void *p;
{
  char buf[2048];

  *buf = '\0';
  while (--argc > 0) {
    if (*buf) strcat(buf, " ");
    strcat(buf, *++argv);
  }
  return system(buf);
}

/*---------------------------------------------------------------------------*/

void on_read(fd, fnc, arg)
int fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  readfnc[fd] = fnc;
  readarg[fd] = arg;
  FD_SET(fd, &chkread);
  FD_CLR(fd, &actread);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

void off_read(fd)
int fd;
{
  readfnc[fd] = 0;
  readarg[fd] = 0;
  FD_CLR(fd, &chkread);
  FD_CLR(fd, &actread);
  if (fd == maxfd)
    for (; maxfd >= 0; maxfd--)
      if (FD_ISSET(maxfd, &chkread)  ||
	  FD_ISSET(maxfd, &chkwrite) ||
	  FD_ISSET(maxfd, &chkexcp)) break;
}

/*---------------------------------------------------------------------------*/

void on_write(fd, fnc, arg)
int fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  writefnc[fd] = fnc;
  writearg[fd] = arg;
  FD_SET(fd, &chkwrite);
  FD_CLR(fd, &actwrite);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

void off_write(fd)
int fd;
{
  writefnc[fd] = 0;
  writearg[fd] = 0;
  FD_CLR(fd, &chkwrite);
  FD_CLR(fd, &actwrite);
  if (fd == maxfd)
    for (; maxfd >= 0; maxfd--)
      if (FD_ISSET(maxfd, &chkread)  ||
	  FD_ISSET(maxfd, &chkwrite) ||
	  FD_ISSET(maxfd, &chkexcp)) break;
}

/*---------------------------------------------------------------------------*/

void on_excp(fd, fnc, arg)
int fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  excpfnc[fd] = fnc;
  excparg[fd] = arg;
  FD_SET(fd, &chkexcp);
  FD_CLR(fd, &actexcp);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

void off_excp(fd)
int fd;
{
  excpfnc[fd] = 0;
  excparg[fd] = 0;
  FD_CLR(fd, &chkexcp);
  FD_CLR(fd, &actexcp);
  if (fd == maxfd)
    for (; maxfd >= 0; maxfd--)
      if (FD_ISSET(maxfd, &chkread)  ||
	  FD_ISSET(maxfd, &chkwrite) ||
	  FD_ISSET(maxfd, &chkexcp)) break;
}

/*---------------------------------------------------------------------------*/

static void check_files_changed()
{

  int changed = 0;
  static long nexttime, net_time, rc_time;
  struct stat statbuf;

  if (Debug || nexttime > secclock()) return;
  nexttime = secclock() + 600;

  if (stat("/tcp/net", &statbuf)) return;
  if (!net_time) net_time = statbuf.st_mtime;
  if (net_time != statbuf.st_mtime && statbuf.st_mtime < secclock() - 3600)
    changed = 1;

  if (stat(Startup, &statbuf)) return;
  if (!rc_time) rc_time = statbuf.st_mtime;
  if (rc_time != statbuf.st_mtime && statbuf.st_mtime < secclock() - 3600)
    changed = 1;

  if (changed) doexit(0, (char **) 0, (void *) 0);
}

/*---------------------------------------------------------------------------*/

void eihalt()
{

  int n;
  int status;
  int32 nte;
  struct timeval timeout;

  check_files_changed();
  wait3(&status, WNOHANG, (int *) 0);
  if (!Debug) alarm(TIMEOUT);
  actread  = chkread;
  actwrite = chkwrite;
  actexcp  = chkexcp;
  timeout.tv_sec = 0;
  if (Hopper)
    timeout.tv_usec = 0;
  else {
    nte = next_timer_event();
    if (nte > 999) nte = 999;
    timeout.tv_usec = 1000 * nte;
  }
  if (select(maxfd + 1, (int *) &actread, (int *) &actwrite, (int *) &actexcp, &timeout) < 1) {
    FD_ZERO(&actread);
    actwrite = actread;
    actexcp  = actread;
  } else
    for (n = maxfd; n >= 0; n--) {
      if (readfnc [n] && FD_ISSET(n, &actread )) (*readfnc [n])(readarg [n]);
      if (writefnc[n] && FD_ISSET(n, &actwrite)) (*writefnc[n])(writearg[n]);
      if (excpfnc [n] && FD_ISSET(n, &actexcp )) (*excpfnc [n])(excparg [n]);
    }
}

