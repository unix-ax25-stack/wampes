/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.16 1991/02/24 20:16:50 deyke Exp $ */

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
#include "hpux.h"

extern int  debug;
extern long  sigsetmask();
extern void _exit();

#define TIMEOUT 120

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

  if (debug || nexttime > secclock()) return;
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

eihalt()
{

  int  n;
  int  status;
  int32 nte;
  register int  nfds;
  register unsigned int  i;
  struct timeval timeout;

  check_files_changed();
  wait3(&status, WNOHANG, (int *) 0);
  if (!debug) alarm(TIMEOUT);
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
    for (i = 0; i < nfds; i++) {
      if (readfnc [i] && maskset(actread , i)) (*readfnc [i])(readarg [i]);
      if (writefnc[i] && maskset(actwrite, i)) (*writefnc[i])(writearg[i]);
      if (excpfnc [i] && maskset(actexcp , i)) (*excpfnc [i])(excparg [i]);
    }
}

