#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/rtprio.h>
#include <termio.h>
#include <time.h>
#include <utmp.h>

#include "global.h"
#include "timer.h"
#include "hpux.h"
#include "login.h"

extern struct utmp *getutent();
extern struct utmp *getutid();
extern void endutent();
extern void exit();
extern void pututline();

#define MASTERPREFIX        "/dev/pty"
#define SLAVEPREFIX         "/dev/tty"

#define PASSWDFILE          "/etc/passwd"
#define LOCKFILE            "/etc/ptmp"

#define DEFAULTUSER         "guest"
#define FIRSTUID            400
#define MAXUID              4095
#define GID                 400
#define HOMEDIRPARENTPARENT "/users/funk"

static char  pty_inuse[256];

/*---------------------------------------------------------------------------*/

#define pty_name(name, prefix, num) \
  sprintf(name, "%s%c%x", prefix, 'p' + (num >> 4), num & 0xf)

/*---------------------------------------------------------------------------*/

static int  find_pty(numptr, slave)
int  *numptr;
char  *slave;
{

  char  master[80];
  int  fd, num;

  for (num = 0; ; num++)
    if (!pty_inuse[num]) {
      pty_name(master, MASTERPREFIX, num);
      if ((fd = open(master, O_RDWR | O_NDELAY, 0600)) >= 0) {
	pty_inuse[num] = 1;
	*numptr = num;
	pty_name(slave, SLAVEPREFIX, num);
	return fd;
      }
      if (errno != EBUSY) return (-1);
      pty_inuse[num] = 1;
    }
}

/*---------------------------------------------------------------------------*/

static void restore_pty(id)
char  *id;
{
  char  filename[80];

  sprintf(filename, "%s%s", MASTERPREFIX, id);
  chown(filename, 0, 0);
  chmod(filename, 0666);
  sprintf(filename, "%s%s", SLAVEPREFIX, id);
  chown(filename, 0, 0);
  chmod(filename, 0666);
}

/*---------------------------------------------------------------------------*/

fixutmpfile()
{
  register struct utmp *up;

  while (up = getutent())
    if (up->ut_type == USER_PROCESS && kill(up->ut_pid, 0)) {
      restore_pty(up->ut_line);
      up->ut_user[0] = '\0';
      up->ut_type = DEAD_PROCESS;
      up->ut_exit.e_termination = 0;
      up->ut_exit.e_exit = 0;
      up->ut_time = currtime;
      pututline(up);
    }
  endutent();
}

/*---------------------------------------------------------------------------*/

struct passwd *getpasswdentry(name, create)
char  *name;
int  create;
{

  FILE * fp;
  char  *cp;
  char  bitmap[MAXUID+1];
  char  homedir[80];
  char  homedirparent[80];
  char  username[128];
  int  fd;
  int  uid;
  struct passwd *pw;

  /* Fix user name */

  for (cp = username; isalnum(uchar(*name)); *cp++ = tolower(uchar(*name++))) ;
  *cp = '\0';
  if (!isalpha(uchar(*username)) || strlen(username) > 8)
    strcpy(username, DEFAULTUSER);

  /* Search existing passwd entry */

  while ((pw = getpwent()) && strcmp(username, pw->pw_name)) ;
  endpwent();
  if (pw) return pw;
  if (!create) return 0;

  /* Find free user id */

  memset(bitmap, 0, sizeof(bitmap));
  if ((fd = open(LOCKFILE, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) return 0;
  close(fd);
  while (pw = getpwent()) {
    if (!strcmp(username, pw->pw_name)) break;
    if (pw->pw_uid <= MAXUID) bitmap[pw->pw_uid] = 1;
  }
  endpwent();
  if (pw) {
    unlink(LOCKFILE);
    return pw;
  }
  for (uid = FIRSTUID; uid <= MAXUID && bitmap[uid]; uid++) ;
  if (uid > MAXUID) {
    unlink(LOCKFILE);
    return 0;
  }

  /* Add user to passwd file */

  sprintf(homedirparent, "%s/%.3s...", HOMEDIRPARENTPARENT, username);
  sprintf(homedir, "%s/%s", homedirparent, username);
  if (!(fp = fopen(PASSWDFILE, "a"))) {
    unlink(LOCKFILE);
    return 0;
  }
  fprintf(fp, "%s:,./:%d:%d::%s:/bin/sh\n", username, uid, GID, homedir);
  fclose(fp);
  pw = getpwuid(uid);
  endpwent();
  unlink(LOCKFILE);

  /* Create home directory */

  mkdir(homedirparent, 0755);
  mkdir(homedir, 0755);
  chown(homedir, uid, GID);
  return pw;
}

/*---------------------------------------------------------------------------*/

static FILE *fopen_logfile(user, protocol)
char  *user, *protocol;
{

  FILE * fp;
  char  filename[80];
  static int  cnt;
  struct tm *tm;

  sprintf(filename, "/tcp/logs/log.%05d.%04d", getpid(), cnt++);
  fp = fopen(filename, "a");
  if (!fp) return 0;
  tm = localtime(&currtime);
  fprintf(fp,
	  "%s at %2d-%.3s-%02d %2d:%02d:%02d by %s\n",
	  protocol,
	  tm->tm_mday,
	  "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	  tm->tm_year % 100,
	  tm->tm_hour,
	  tm->tm_min,
	  tm->tm_sec,
	  user);
  fflush(fp);
  return fp;
}

/*---------------------------------------------------------------------------*/

struct login_cb *login_open(user, protocol, pollfnc, pollarg)
char  *user, *protocol;
void (*pollfnc)();
char  *pollarg;
{

  char  *env = 0;
  char  slave[80];
  int  i;
  struct login_cb *tp;
  struct passwd *pw;
  struct termio termio;
  struct utmp utmp;

  tp = (struct login_cb *) calloc(1, sizeof(struct login_cb ));
  if (!tp) return 0;
  if ((tp->pty = find_pty(&tp->num, slave)) < 0) {
    free(tp);
    return 0;
  }
  strcpy(tp->id, slave + strlen(slave) - 2);
  tp->fplog = fopen_logfile(user, protocol);
  memset(&utmp, 0, sizeof(struct utmp ));
  strcpy(utmp.ut_id, tp->id);
  strcpy(utmp.ut_line, slave + 5);
  utmp.ut_type = DEAD_PROCESS;
  pututline(&utmp);
  endutent();
  if (!(tp->pid = fork())) {
    rtprio(0, RTPRIO_RTOFF);
    pw = getpasswdentry(user, 1);
    if (!pw || pw->pw_passwd[0]) {
      pw = getpasswdentry("", 0);
      if (!pw || pw->pw_passwd[0]) exit(1);
    }
    for (i = 0; i < _NFILE; i++) close(i);
    setpgrp();
    open(slave, O_RDWR, 0666);
    dup(0);
    dup(0);
    chmod(slave, 0622);
    memset((char *) &termio, 0, sizeof(termio));
    termio.c_iflag = ICRNL;
    termio.c_oflag = OPOST | ONLCR | TAB3;
    termio.c_cflag = B1200 | CS8 | CREAD | CLOCAL;
    termio.c_lflag = ISIG | ICANON;
    termio.c_cc[VINTR] = 127;
    termio.c_cc[VQUIT] = 28;
    termio.c_cc[VERASE] = 8;
    termio.c_cc[VKILL] = 24;
    termio.c_cc[VEOF] = 4;
    ioctl(0, TCSETA, &termio);
    ioctl(0, TCFLSH, 2);
    strcpy(utmp.ut_user, "LOGIN");
    utmp.ut_pid = getpid();
    utmp.ut_type = LOGIN_PROCESS;
    utmp.ut_time = currtime;
    pututline(&utmp);
    endutent();
    execle("/bin/login", "login", pw->pw_name, (char *) 0, &env);
    exit(1);
  }
  tp->poll_timer.start = 1;
  tp->poll_timer.func = pollfnc;
  tp->poll_timer.arg = pollarg;
  start_timer(&tp->poll_timer);
  return tp;
}

/*---------------------------------------------------------------------------*/

void login_close(tp)
struct login_cb *tp;
{

  int  fwtmp;
  struct utmp utmp, *up;

  if (!tp) return;
  stop_timer(&tp->poll_timer);
  if (tp->pid) kill(-tp->pid, SIGHUP);
  if (tp->pty > 0) {
    close(tp->pty);
    restore_pty(tp->id);
    pty_inuse[tp->num] = 0;
  }
  if (tp->fplog) fclose(tp->fplog);
  memset(&utmp, 0, sizeof(struct utmp ));
  strcpy(utmp.ut_id, tp->id);
  utmp.ut_type = DEAD_PROCESS;
  if (up = getutid(&utmp)) {
    up->ut_user[0] = '\0';
    up->ut_type = DEAD_PROCESS;
    up->ut_exit.e_termination = 0;
    up->ut_exit.e_exit = 0;
    up->ut_time = currtime;
    memcpy(&utmp, up, sizeof(struct utmp ));
    pututline(up);
    fwtmp = open("/etc/wtmp", O_WRONLY | O_CREAT | O_APPEND, 0644);
    write(fwtmp, (char *) &utmp, sizeof(struct utmp ));
    close(fwtmp);
  }
  endutent();
  free((char *) tp);
}

/*---------------------------------------------------------------------------*/

int  login_read(tp)
struct login_cb *tp;
{
  int  c;

  if (tp->bufcnt < 1)
    if ((tp->bufcnt = read(tp->pty, tp->bufptr = tp->buffer, sizeof(tp->buffer))) < 1)
      return (-1);
  c = uchar(*tp->bufptr++);
  tp->bufcnt--;
  if (tp->fplog && c != '\r') putc(c, tp->fplog);
  return c;
}

/*---------------------------------------------------------------------------*/

void login_write(tp, c)
struct login_cb *tp;
char  c;
{
  if (!c || c == '\n') return;
  if (c == '\r' || tp->linelen >= 255) {
    write(tp->pty, "\r", 1);
    if (tp->fplog) putc('\n', tp->fplog);
    tp->linelen = 0;
  }
  if (c != '\r') {
    write(tp->pty, &c, 1);
    if (tp->fplog) putc(c, tp->fplog);
    tp->linelen++;
  }
}

/*---------------------------------------------------------------------------*/

int  login_dead(tp)
struct login_cb *tp;
{
  return (kill(tp->pid, 0) == -1);
}

