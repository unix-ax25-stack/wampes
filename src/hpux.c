/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.c,v 1.58 1996/02/04 11:17:38 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include <sys/resource.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifdef ULTRIX_RISC
#include <sys/signal.h>
#endif

#ifdef ibm032
#include <sgtty.h>
#else
#include <termios.h>
#endif

#ifndef WNOHANG
#define WNOHANG 1
#endif

#include "configure.h"

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

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

struct proc_t {
  pid_t pid;
  void (*fnc)(void *);
  void *arg;
  struct proc_t *next;
};

static struct proc_t *procs;

static struct fd_set chkread;
static struct fd_set actread;
static void (*readfnc[FD_SETSIZE])(void *);
static void *readarg[FD_SETSIZE];

static struct fd_set chkwrite;
static struct fd_set actwrite;
static void (*writefnc[FD_SETSIZE])(void *);
static void *writearg[FD_SETSIZE];

static int maxfd = -1;

static int local_kbd;

#ifdef ibm032
static struct sgttyb curr_sgttyb, prev_sgttyb;
static struct tchars curr_tchars, prev_tchars;
static struct ltchars curr_ltchars, prev_ltchars;
#else
static struct termios curr_termios, prev_termios;
#endif

/*---------------------------------------------------------------------------*/

pid_t dofork(void)
{
  pid_t pid;

  fflush(stdout);
  if (!(pid = fork())) {
    nice(-nice(0));
    signal(SIGPIPE, SIG_DFL);
  }
  return pid;
}

/*---------------------------------------------------------------------------*/

void ioinit(void)
{

  int i;
  struct rlimit rlp;

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

  if ((local_kbd = (isatty(0) && isatty(1)))) {
#ifdef ibm032
    ioctl(0, TIOCGETP, &prev_sgttyb);
    ioctl(0, TIOCGETC, &prev_tchars);
    ioctl(0, TIOCGLTC, &prev_ltchars);
    curr_sgttyb = prev_sgttyb;
    curr_tchars = prev_tchars;
    curr_ltchars = prev_ltchars;
    curr_sgttyb.sg_flags |= CBREAK;
    curr_sgttyb.sg_flags &= ~ECHO;
    curr_tchars.t_intrc = 0xff;
    curr_tchars.t_quitc = 0xff;
    curr_tchars.t_eofc = 0xff;
    curr_tchars.t_brkc = 0xff;
    curr_ltchars.t_suspc = 0xff;
    curr_ltchars.t_dsuspc = 0xff;
    ioctl(0, TIOCSETP, &curr_sgttyb);
    ioctl(0, TIOCSETC, &curr_tchars);
    ioctl(0, TIOCSLTC, &curr_ltchars);
#else
    tcgetattr(0, &prev_termios);
    curr_termios = prev_termios;
    curr_termios.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
    curr_termios.c_lflag = 0;
    curr_termios.c_cc[VMIN] = 0;
    curr_termios.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &curr_termios);
#endif
    on_read(0, (void (*)(void *)) keyboard, 0);
  } else {
#ifdef macII
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
#else
    close(0);
    close(1);
    close(2);
#endif
    for (i = 3; i < FD_SETSIZE; i++) close(i);

    setsid();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
  }
  umask(022);
  signal(SIGPIPE, SIG_IGN);
  if (!Debug) {
    signal(SIGALRM, (void (*)(int)) abort);
    alarm(TIMEOUT);
    nice(-39);
  }
  if (!getenv("HOME"))
    putenv("HOME=" HOME_DIR "/root");
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
    gettimeofday(&tv, 0);
    Secclock = tv.tv_sec;
    Msclock = 1000 * Secclock + tv.tv_usec / 1000;
  }

  fixutmpfile();
}

/*---------------------------------------------------------------------------*/

void iostop(void)
{
  register struct iface *ifp;

  if (local_kbd) {
#ifdef ibm032
    ioctl(0, TIOCSETP, &prev_sgttyb);
    ioctl(0, TIOCSETC, &prev_tchars);
    ioctl(0, TIOCSLTC, &prev_ltchars);
#else
    tcsetattr(0, TCSANOW, &prev_termios);
#endif
  }
  for (ifp = Ifaces; ifp; ifp = ifp->next)
    if (ifp->stop) (*ifp->stop)(ifp);
}

/*---------------------------------------------------------------------------*/

static void child_dead(pid_t pid)
{

  struct proc_t *prev, *curr;
  void (*fnc)(void *);
  void *arg;

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
      return;
    }
}

/*---------------------------------------------------------------------------*/

static void dowait(void)
{

  int status;
  pid_t pid;

#ifdef ibm032
  while ((pid = wait3(&status, WNOHANG, 0)) > 0) child_dead(pid);
#else
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) child_dead(pid);
#endif
}

/*---------------------------------------------------------------------------*/

#ifndef RISCiX

int system(const char *cmdline)
{

  int i;
  int status;
  pid_t p;
  pid_t pid;

  if (!cmdline) return 1;
  switch (pid = dofork()) {
  case -1:
    return -1;
  case 0:
    for (i = 3; i < FD_SETSIZE; i++) close(i);
    execl("/bin/sh", "sh", "-c", cmdline, 0);
    exit(1);
  default:
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    for (; ; ) {
      p = wait(&status);
      if (p == pid) break;
      if (p > 0) child_dead(p);
    }
    signal(SIGINT,  SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    return status;
  }
}

#endif

/*---------------------------------------------------------------------------*/

int doshell(int argc, char *argv[], void *p)
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

void on_read(int fd, void (*fnc)(void *), void *arg)
{
  readfnc[fd] = fnc;
  readarg[fd] = arg;
  FD_SET(fd, &chkread);
  FD_CLR(fd, &actread);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

void off_read(int fd)
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

void on_write(int fd, void (*fnc)(void *), void *arg)
{
  writefnc[fd] = fnc;
  writearg[fd] = arg;
  FD_SET(fd, &chkwrite);
  FD_CLR(fd, &actwrite);
  if (maxfd < fd) maxfd = fd;
}

/*---------------------------------------------------------------------------*/

void off_write(int fd)
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

void on_death(int pid, void (*fnc)(void *), void *arg)
{
  struct proc_t *p;

  for (p = procs; p; p = p->next)
    if (p->pid == pid) {
      p->fnc = fnc;
      p->arg = arg;
      return;
    }
  p = (struct proc_t *) malloc(sizeof(struct proc_t));
  p->pid = pid;
  p->fnc = fnc;
  p->arg = arg;
  p->next = procs;
  procs = p;
}

/*---------------------------------------------------------------------------*/

void off_death(int pid)
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

static void check_files_changed(void)
{

  int i;
  static char *filetable[3];
  static long nexttime;
  struct stat statbuf;

  if (Debug || nexttime > secclock())
    return;
  nexttime = secclock() + 600;

  if (!filetable[0]) {
    filetable[0] = "/tcp/net";
    filetable[1] = Startup;
  };

  for (i = 0; filetable[i]; i++) {
    if (!stat(filetable[i], &statbuf)) {
      if (statbuf.st_mtime > StartTime &&
	  statbuf.st_mtime < secclock() - 21600) {
	logmsg(0, "%s has changed", filetable[i]);
	main_exit = 1;
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

void eihalt(void)
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
  if (select(maxfd + 1, SEL_ARG(&actread), SEL_ARG(&actwrite), 0, &timeout) < 1) {
    FD_ZERO(&actread);
    actwrite = actread;
  } else
    for (n = maxfd; n >= 0; n--) {
      if (readfnc [n] && FD_ISSET(n, &actread )) (*readfnc [n])(readarg [n]);
      if (writefnc[n] && FD_ISSET(n, &actwrite)) (*writefnc[n])(writearg[n]);
    }
}
