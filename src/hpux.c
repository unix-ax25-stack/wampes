/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.18 1991/06/01 22:18:08 deyke Exp $ */

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

#include "global.h"
#include "iface.h"
#include "timer.h"
#include "files.h"
#include "hardware.h"
#include "login.h"
#include "commands.h"
#include "main.h"
#include "hpux.h"

extern long  sigsetmask();

#define TIMEOUT 120

static int  chkread[2];
static int  actread[2];
static void (*readfnc[_NFILE]) __ARGS((void *));
static void *readarg[_NFILE];

static int  chkwrite[2];
static int  actwrite[2];
static void (*writefnc[_NFILE]) __ARGS((void *));
static void *writearg[_NFILE];

static int  chkexcp[2];
static int  actexcp[2];
static void (*excpfnc[_NFILE]) __ARGS((void *));
static void *excparg[_NFILE];

static int  nfds = -1;

static int  local_kbd;

static struct termio curr_termio;
static struct termio prev_termio;

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

  int  i;
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
    on_read(0, keyboard, (void *) 0);
  } else {
    for (i = 0; i < _NFILE; i++) close(i);
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
  if (!Debug) rtprio(0, 127);
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
  timerproc();          /* Init times */
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

int  system(cmdline)
const char *cmdline;
{

  int  i, pid, status;
  long  oldmask;

  if (!cmdline) return 1;
  fflush(stdout);
  switch (pid = fork()) {
  case -1:
    return (-1);
  case 0:
    for (i = 3; i < _NFILE; i++) close(i);
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

#define setmask(mask, fd) ((mask)[(fd)>>5] |=  (1 << ((fd) & 31)))
#define clrmask(mask, fd) ((mask)[(fd)>>5] &= ~(1 << ((fd) & 31)))
#define maskset(mask, fd) ((mask)[(fd)>>5] &   (1 << ((fd) & 31)))

/*---------------------------------------------------------------------------*/

void on_read(fd, fnc, arg)
int  fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  readfnc[fd] = fnc;
  readarg[fd] = arg;
  setmask(chkread, fd);
  clrmask(actread, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

void off_read(fd)
int  fd;
{
  readfnc[fd] = 0;
  readarg[fd] = 0;
  clrmask(chkread, fd);
  clrmask(actread, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

void on_write(fd, fnc, arg)
int  fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  writefnc[fd] = fnc;
  writearg[fd] = arg;
  setmask(chkwrite, fd);
  clrmask(actwrite, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

void off_write(fd)
int  fd;
{
  writefnc[fd] = 0;
  writearg[fd] = 0;
  clrmask(chkwrite, fd);
  clrmask(actwrite, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

void on_excp(fd, fnc, arg)
int  fd;
void (*fnc) __ARGS((void *));
void *arg;
{
  excpfnc[fd] = fnc;
  excparg[fd] = arg;
  setmask(chkexcp, fd);
  clrmask(actexcp, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

void off_excp(fd)
int  fd;
{
  excpfnc[fd] = 0;
  excparg[fd] = 0;
  clrmask(chkexcp, fd);
  clrmask(actexcp, fd);
  nfds = -1;
}

/*---------------------------------------------------------------------------*/

static void check_files_changed()
{

  int  changed = 0;
  static long  nexttime, net_time, rc_time;
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

  int  n;
  int  status;
  int32 nte;
  register unsigned int  i;
  struct timeval timeout;

  check_files_changed();
  wait3(&status, WNOHANG, (int *) 0);
  if (!Debug) alarm(TIMEOUT);
  if (nfds < 0) {
    if (i = chkread[1] | chkwrite[1] | chkexcp[1])
      nfds = 32;
    else {
      i = chkread[0] | chkwrite[0] | chkexcp[0];
      nfds = 0;
    }
    for (n = 16; n; n >>= 1)
      if (i & ((-1) << n)) {
	nfds += n;
	i >>= n;
      }
    if (i) nfds++;
  }
  actread [0] = chkread [0];
  actread [1] = chkread [1];
  actwrite[0] = chkwrite[0];
  actwrite[1] = chkwrite[1];
  actexcp [0] = chkexcp [0];
  actexcp [1] = chkexcp [1];
  timeout.tv_sec = 0;
  if (Hopper)
    timeout.tv_usec = 0;
  else {
    nte = next_timer_event();
    if (nte > 999) nte = 999;
    timeout.tv_usec = 1000 * nte;
  }
  if (select(nfds, actread, actwrite, actexcp, &timeout) < 1) {
    actread [0] = actread [1] = 0;
    actwrite[0] = actwrite[1] = 0;
    actexcp [0] = actexcp [1] = 0;
  } else
    for (n = nfds - 1; n >= 0; n--) {
      if (readfnc [n] && maskset(actread , n)) (*readfnc [n])(readarg [n]);
      if (writefnc[n] && maskset(actwrite, n)) (*writefnc[n])(writearg[n]);
      if (excpfnc [n] && maskset(actexcp , n)) (*excpfnc [n])(excparg [n]);
    }
}

