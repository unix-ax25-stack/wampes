/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.35 1993/03/30 17:24:01 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#ifdef ULTRIX_RISC
#include <sys/signal.h>
#endif
#include <termios.h>
#include <unistd.h>

#include <sys/resource.h>

#ifdef __hpux
#include <sys/rtprio.h>
#endif

#ifndef WNOHANG
#define WNOHANG 1
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

#ifdef __hpux
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

struct proc_t {
  pid_t pid;
  void (*fnc) __ARGS((void *));
  void *arg;
  struct proc_t *next;
};

static struct proc_t *procs;

static struct fd_set chkread;
static struct fd_set actread;
static void (*readfnc[FD_SETSIZE]) __ARGS((void *));
static void *readarg[FD_SETSIZE];

static struct fd_set chkwrite;
static struct fd_set actwrite;
static void (*writefnc[FD_SETSIZE]) __ARGS((void *));
static void *writearg[FD_SETSIZE];

static int maxfd = -1;

static int local_kbd;

static struct termios curr_termios;
static struct termios prev_termios;

static void check_files_changed __ARGS((void));

/*---------------------------------------------------------------------------*/

pid_t dofork()
{
  pid_t pid;

  fflush(stdout);
  if (!(pid = fork())) {
#ifdef __hpux
    rtprio(0, RTPRIO_RTOFF);
#endif
    signal(SIGPIPE, SIG_DFL);
  }
  return pid;
}

/*---------------------------------------------------------------------------*/

void ioinit()
{

  int i;
#ifndef LINUX
  struct rlimit rlp;
#endif

#define fixdir(name, mode) \
	{ mkdir((name), (mode)); chmod((name), (mode)); }

#ifdef RLIMIT_NOFILE
  getrlimit(RLIMIT_NOFILE, &rlp);
  rlp.rlim_cur = FD_SETSIZE;
  if (rlp.rlim_max < rlp.rlim_cur) rlp.rlim_max = rlp.rlim_cur;
  setrlimit(RLIMIT_NOFILE, &rlp);
#endif

  fixdir("/tcp", 0755);
  fixdir("/tcp/sockets", 0755);
  fixdir("/tcp/.sockets", 0700);
  fixdir("/tcp/logs", 0700);

  if (local_kbd = isatty(0)) {
    tcgetattr(0, &prev_termios);
    curr_termios = prev_termios;
    curr_termios.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
    curr_termios.c_lflag = 0;
    curr_termios.c_cc[VMIN] = 0;
    curr_termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &curr_termios);
    on_read(0, (void (*)()) keyboard, (void *) 0);
  } else {
    for (i = 0; i < FD_SETSIZE; i++) close(i);
    setsid();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    remote_kbd_initialize();
  }
  umask(022);
  signal(SIGPIPE, SIG_IGN);
  if (!Debug) {
    signal(SIGALRM, (void (*)()) abort);
    alarm(TIMEOUT);
#ifdef __hpux
    rtprio(0, 127);
#endif
  }
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

  if (local_kbd) tcsetattr(0, TCSANOW, &prev_termios);
  for (ifp = Ifaces; ifp; ifp = ifp->next)
    if (ifp->stop) (*ifp->stop)(ifp);
}

/*---------------------------------------------------------------------------*/

int system(cmdline)
const char *cmdline;
{

  int i;
  int status;
  long oldmask;
  pid_t pid;

  if (!cmdline) return 1;
  switch (pid = dofork()) {
  case -1:
    return (-1);
  case 0:
    for (i = 3; i < FD_SETSIZE; i++) close(i);
    execl("/bin/sh", "sh", "-c", cmdline, (char *) 0);
    exit(1);
  default:
    oldmask = sigsetmask(-1);
    while (waitpid(pid, &status, 0) == -1 && errno == EINTR) ;
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
      if (FD_ISSET(maxfd, &chkread) || FD_ISSET(maxfd, &chkwrite)) break;
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
      if (FD_ISSET(maxfd, &chkread) || FD_ISSET(maxfd, &chkwrite)) break;
}

/*---------------------------------------------------------------------------*/

void on_death(pid, fnc, arg)
int pid;
void (*fnc) __ARGS((void *));
void *arg;
{
  struct proc_t *p;

  for (p = procs; p; p = p->next)
    if (p->pid == pid) {
      p->fnc = fnc;
      p->arg = arg;
      return;
    }
  p = malloc(sizeof(*p));
  p->pid = pid;
  p->fnc = fnc;
  p->arg = arg;
  p->next = procs;
  procs = p;
}

/*---------------------------------------------------------------------------*/

void off_death(pid)
int pid;
{
  struct proc_t *prev, *curr;

  for (prev = 0, curr = procs; curr; prev = curr, curr = curr->next)
    if (curr->pid == pid) {
      if (prev)
	prev->next = curr->next;
      else
	procs = curr->next;
      free(curr);
      return;
    }
}

/*---------------------------------------------------------------------------*/

static void dowait()
{

  int status;
  pid_t pid;
  struct proc_t *prev, *curr;
  void (*fnc) __ARGS((void *));
  void *arg;

  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    for (prev = 0, curr = procs; curr; prev = curr, curr = curr->next)
      if (curr->pid == pid) {
	fnc = curr->fnc;
	arg = curr->arg;
	if (prev)
	  prev->next = curr->next;
	else
	  procs = curr->next;
	free(curr);
	if (fnc) (*fnc)(arg);
	break;
      }
  }
}

/*---------------------------------------------------------------------------*/

static void check_files_changed()
{

  static long nexttime, net_time, rc_time;
  struct stat statbuf;

  if (Debug || nexttime > secclock()) return;
  nexttime = secclock() + 600;

  if (stat("/tcp/net", &statbuf)) return;
  if (!net_time) net_time = statbuf.st_mtime;
  if (net_time != statbuf.st_mtime && statbuf.st_mtime < secclock() - 3600) {
    log((void *) 0, "%s has changed", "/tcp/net");
    doexit(0, (char **) 0, (void *) 0);
  }

  if (stat(Startup, &statbuf)) return;
  if (!rc_time) rc_time = statbuf.st_mtime;
  if (rc_time != statbuf.st_mtime && statbuf.st_mtime < secclock() - 3600) {
    log((void *) 0, "%s has changed", Startup);
    doexit(0, (char **) 0, (void *) 0);
  }

}

/*---------------------------------------------------------------------------*/

void eihalt()
{

  int n;
  int32 nte;
  struct timeval timeout;

  check_files_changed();
  dowait();
  if (!Debug) alarm(TIMEOUT);
  actread  = chkread;
  actwrite = chkwrite;
  timeout.tv_sec = 0;
  if (Rdytab && Rdytab->next)
    timeout.tv_usec = 0;
  else {
    nte = next_timer_event();
    if (nte > 999) nte = 999;
    timeout.tv_usec = 1000 * nte;
  }
  if (select(maxfd + 1, SEL_ARG(&actread), SEL_ARG(&actwrite), SEL_ARG(0), &timeout) < 1) {
    FD_ZERO(&actread);
    actwrite = actread;
  } else
    for (n = maxfd; n >= 0; n--) {
      if (readfnc [n] && FD_ISSET(n, &actread )) (*readfnc [n])(readarg [n]);
      if (writefnc[n] && FD_ISSET(n, &actwrite)) (*writefnc[n])(writearg[n]);
    }
}

