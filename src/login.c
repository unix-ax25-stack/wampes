/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.c,v 1.25 1992/09/25 20:07:18 deyke Exp $ */

#include <sys/types.h>

#include <stdio.h>      /* must be before pwd.h */

#include <ctype.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "hpux.h"
#include "telnet.h"
#include "login.h"

#define MASTERPREFIX        "/dev/pty"
#define SLAVEPREFIX         "/dev/tty"
#define NUMPTY              256

#define PASSWDFILE          "/etc/passwd"
#define PWLOCKFILE          "/etc/ptmp"

#define DEFAULTUSER         "guest"
#define FIRSTUID            400
#define MAXUID              4095
#define GID                 400
#define HOMEDIRPARENTPARENT "/users/funk"

/* login server control block */

struct login_cb {
  int pty;                      /* pty file descriptor */
  int num;                      /* pty number */
  char id[4];                   /* pty id (last 2 chars) */
  pid_t pid;                    /* process id of login process */
  char inpbuf[512];             /* pty read buffer */
  char *inpptr;                 /* pty read buffer pointer */
  int inpcnt;                   /* pty read buffer count */
  struct mbuf *sndq;            /* pty send queue */
  int lastchr;                  /* last chr fetched from send queue */
  int linelen;                  /* counter for automatic line break */
  char outbuf[256];             /* pty write buffer */
  char *outptr;                 /* pty write buffer pointer */
  int outcnt;                   /* pty write buffer count */
  void (*readfnc) __ARGS((void *fncarg));
				/* func to call if pty is readable */
  void (*closefnc) __ARGS((void *fncarg));
				/* func to call if pty gets closed */
  void *fncarg;                 /* argument for readfnc and closefnc */
  int telnet;                   /* telnet mode */
  int state;                    /* telnet state */
  char option[NOPTIONS+1];      /* telnet options */
};

static int32 pty_locktime[NUMPTY];

static int find_pty __ARGS((int *numptr, char *slave));
static void restore_pty __ARGS((const char *id));
static void write_log_header __ARGS((int fd, const char *user, const char *protocol));
static int do_telnet __ARGS((struct login_cb *tp, int chr));
static void write_pty __ARGS((struct login_cb *tp));
static void death_handler __ARGS((struct login_cb *tp));

/*---------------------------------------------------------------------------*/

#define pty_name(name, prefix, num) \
  sprintf(name, "%s%c%x", prefix, 'p' + (num >> 4), num & 0xf)

/*---------------------------------------------------------------------------*/

static int find_pty(numptr, slave)
int *numptr;
char *slave;
{

  char master[80];
  int fd;
  int num;

  for (num = 0; num < NUMPTY; num++)
    if (pty_locktime[num] < secclock()) {
      pty_locktime[num] = secclock() + 60;
      pty_name(master, MASTERPREFIX, num);
      if ((fd = open(master, O_RDWR | O_NONBLOCK, 0600)) >= 0) {
	*numptr = num;
	pty_name(slave, SLAVEPREFIX, num);
	return fd;
      }
    }
  return (-1);
}

/*---------------------------------------------------------------------------*/

static void restore_pty(id)
const char *id;
{
  char filename[80];

  sprintf(filename, "%s%s", MASTERPREFIX, id);
  chown(filename, 0, 0);
  chmod(filename, 0666);
  sprintf(filename, "%s%s", SLAVEPREFIX, id);
  chown(filename, 0, 0);
  chmod(filename, 0666);
}

/*---------------------------------------------------------------------------*/

void fixutmpfile()
{

#ifdef USER_PROCESS

  struct utmp *up;

  while (up = getutent())
    if (up->ut_type == USER_PROCESS && kill(up->ut_pid, 0)) {
      restore_pty(up->ut_id);
      up->ut_user[0] = 0;
      up->ut_type = DEAD_PROCESS;
#ifndef LINUX
      up->ut_exit.e_termination = 0;
      up->ut_exit.e_exit = 0;
#endif
      up->ut_time = secclock();
      pututline(up);
    }
  endutent();

#endif

}

/*---------------------------------------------------------------------------*/

struct passwd *getpasswdentry(name, create)
const char *name;
int create;
{

  FILE *fp;
  char *cp;
  char bitmap[MAXUID+1];
  char homedir[80];
  char homedirparent[80];
  char username[128];
  int fd;
  int uid;
  struct passwd *pw;

  /* Fix user name */

  for (cp = username; isalnum(uchar(*name)); *cp++ = tolower(uchar(*name++))) ;
  *cp = 0;
  if (!isalpha(uchar(*username)) || strlen(username) > 8)
    strcpy(username, DEFAULTUSER);

  /* Search existing passwd entry */

  if (pw = getpwnam(username)) return pw;
  if (!create) return 0;

  /* Find free user id */

  if ((fd = open(PWLOCKFILE, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) return 0;
  close(fd);
  memset(bitmap, 0, sizeof(bitmap));
  while (pw = getpwent()) {
    if (!strcmp(username, pw->pw_name)) break;
    if (pw->pw_uid <= MAXUID) bitmap[pw->pw_uid] = 1;
  }
  endpwent();
  if (pw) {
    unlink(PWLOCKFILE);
    return pw;
  }
  for (uid = FIRSTUID; uid <= MAXUID && bitmap[uid]; uid++) ;
  if (uid > MAXUID) {
    unlink(PWLOCKFILE);
    return 0;
  }

  /* Add user to passwd file */

  sprintf(homedirparent, "%s/%.3s...", HOMEDIRPARENTPARENT, username);
  sprintf(homedir, "%s/%s", homedirparent, username);
  if (!(fp = fopen(PASSWDFILE, "a"))) {
    unlink(PWLOCKFILE);
    return 0;
  }
#ifndef LINUX
  fprintf(fp, "%s:,./:%d:%d::%s:\n", username, uid, GID, homedir);
#else
  fprintf(fp, "%s::%d:%d::%s:\n", username, uid, GID, homedir);
#endif
  fclose(fp);
  pw = getpwuid(uid);
  unlink(PWLOCKFILE);

  /* Create home directory */

  mkdir(homedirparent, 0755);
  mkdir(homedir, 0755);
  chown(homedir, uid, GID);
  return pw;
}

/*---------------------------------------------------------------------------*/

static void write_log_header(fd, user, protocol)
int fd;
const char *user;
const char *protocol;
{

  char buf[1024];
  struct tm *tm;

  tm = localtime((long *) &Secclock);
  sprintf(buf,
	  "%s at %2d-%.3s-%02d %2d:%02d:%02d by %s\n",
	  protocol,
	  tm->tm_mday,
	  "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	  tm->tm_year % 100,
	  tm->tm_hour,
	  tm->tm_min,
	  tm->tm_sec,
	  user);
  write_log(fd, buf, (int) strlen(buf));
}

/*---------------------------------------------------------------------------*/

static int do_telnet(tp, chr)
struct login_cb *tp;
int chr;
{
  struct termios termios;

  switch (tp->state) {
  case TS_DATA:
    if (chr != IAC) {
      /*** if (!tp->option[TN_TRANSMIT_BINARY]) chr &= 0x7f; ***/
      return 1;
    }
    tp->state = TS_IAC;
    break;
  case TS_IAC:
    switch (chr) {
    case WILL:
      tp->state = TS_WILL;
      break;
    case WONT:
      tp->state = TS_WONT;
      break;
    case DO:
      tp->state = TS_DO;
      break;
    case DONT:
      tp->state = TS_DONT;
      break;
    case IAC:
      tp->state = TS_DATA;
      return 1;
    default:
      tp->state = TS_DATA;
      break;
    }
    break;
  case TS_WILL:
    tp->state = TS_DATA;
    break;
  case TS_WONT:
    tp->state = TS_DATA;
    break;
  case TS_DO:
    if (chr <= NOPTIONS) tp->option[chr] = 1;
    if (chr == TN_ECHO) {
      tcgetattr(tp->pty, &termios);
      termios.c_lflag |= (ECHO | ECHOE);
      tcsetattr(tp->pty, TCSANOW, &termios);
    }
    tp->state = TS_DATA;
    break;
  case TS_DONT:
    if (chr <= NOPTIONS) tp->option[chr] = 0;
    if (chr == TN_ECHO) {
      tcgetattr(tp->pty, &termios);
      termios.c_lflag &= ~(ECHO | ECHOE);
      tcsetattr(tp->pty, TCSANOW, &termios);
    }
    tp->state = TS_DATA;
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static void write_pty(tp)
struct login_cb *tp;
{

  char *p;
  int chr;
  int lastchr;
  int n;

  if (!tp->outcnt) {
    p = tp->outbuf;
    while ((chr = PULLCHAR(&tp->sndq)) != -1) {
      lastchr = tp->lastchr;
      tp->lastchr = chr;
      if (!tp->telnet || do_telnet(tp, uchar(chr))) {
	if (lastchr != '\r' || chr != '\0' && chr != '\n') {
	  *p++ = chr;
	  if (chr == '\r' || chr == '\n') {
	    tp->linelen = 0;
	    break;
	  }
	  if (++tp->linelen >= 250) {
	    *p++ = '\n';
	    tp->linelen = 0;
	    break;
	  }
	}
      }
    }
    tp->outptr = tp->outbuf;
    tp->outcnt = p - tp->outbuf;
  }
  if (tp->outcnt) {
    n = write(tp->pty, tp->outptr, tp->outcnt);
    if (n < 0) n = 0;
    if (n) write_log(tp->pty, tp->outptr, n);
    tp->outptr += n;
    tp->outcnt -= n;
  }
  if (!tp->outcnt && !tp->sndq) off_write(tp->pty);
}

/*---------------------------------------------------------------------------*/

static void death_handler(tp)
struct login_cb *tp;
{
  off_read(tp->pty);
  off_write(tp->pty);
  if (tp->closefnc) (*tp->closefnc)(tp->fncarg);
}

/*---------------------------------------------------------------------------*/

struct login_cb *login_open(user, protocol, read_upcall, close_upcall, upcall_arg)
const char *user;
const char *protocol;
void (*read_upcall) __ARGS((void *arg));
void (*close_upcall) __ARGS((void *arg));
void *upcall_arg;
{

  char *env = 0;
  char slave[80];
  int i;
  struct login_cb *tp;
  struct passwd *pw;
  struct termios termios;
  struct utmp utmpbuf;

  tp = (struct login_cb *) calloc(1, sizeof(struct login_cb ));
  if (!tp) return 0;
  tp->telnet = !strcmp(protocol, "TELNET");
  if ((tp->pty = find_pty(&tp->num, slave)) < 0) {
    free(tp);
    return 0;
  }
  strcpy(tp->id, slave + strlen(slave) - 2);
  tp->readfnc = read_upcall;
  tp->closefnc = close_upcall;
  tp->fncarg = upcall_arg;
  on_read(tp->pty, tp->readfnc, tp->fncarg);
  write_log_header(tp->pty, user, protocol);
  if (!(tp->pid = dofork())) {
    pw = getpasswdentry(user, 1);
    if (!pw || pw->pw_passwd[0]) pw = getpasswdentry("", 0);
    for (i = 0; i < FD_SETSIZE; i++) close(i);
    setsid();
    open(slave, O_RDWR, 0666);
    dup(0);
    dup(0);
    chmod(slave, 0622);
    memset((char *) &termios, 0, sizeof(termios));
    termios.c_iflag = ICRNL | IXOFF;
    termios.c_oflag = OPOST | ONLCR | TAB3;
    termios.c_cflag = CS8 | CREAD | CLOCAL;
    termios.c_lflag = ISIG | ICANON;
    termios.c_cc[VINTR]  = 127;
    termios.c_cc[VQUIT]  =  28;
    termios.c_cc[VERASE] =   8;
    termios.c_cc[VKILL]  =  24;
    termios.c_cc[VEOF]   =   4;
    cfsetispeed(&termios, B1200);
    cfsetospeed(&termios, B1200);
    tcsetattr(0, TCSANOW, &termios);
    if (!pw || pw->pw_passwd[0]) exit(1);
#ifdef LOGIN_PROCESS
    memset(&utmpbuf, 0, sizeof(utmpbuf));
    strcpy(utmpbuf.ut_user, "LOGIN");
    strcpy(utmpbuf.ut_id, tp->id);
    strcpy(utmpbuf.ut_line, slave + 5);
    utmpbuf.ut_pid = getpid();
    utmpbuf.ut_type = LOGIN_PROCESS;
    utmpbuf.ut_time = secclock();
    strncpy(utmpbuf.ut_host, protocol, sizeof(utmpbuf.ut_host));
    pututline(&utmpbuf);
    endutent();
#endif
#ifdef sun
    execle("/usr/bin/login", "login", "-h", protocol, pw->pw_name, (char *) 0, &env);
#else
    execle("/bin/login", "login", pw->pw_name, (char *) 0, &env);
#endif
    exit(1);
  }
  on_death(tp->pid, (void (*)()) death_handler, tp);
  return tp;
}

/*---------------------------------------------------------------------------*/

void login_close(tp)
struct login_cb *tp;
{

#ifdef sun
#define UTMP_FILE       "/etc/utmp"
#define WTMP_FILE       "/var/adm/wtmp"
#endif

  char slave[80];
  int fdut = -1;
  struct utmp utmpbuf;

  if (!tp) return;
  if (tp->pty > 0) {
    off_read(tp->pty);
    off_write(tp->pty);
    off_death(tp->pid);
#ifdef LINUX
    ioctl(tp->pty, TCFLSH, 2);
#endif
    close(tp->pty);
    restore_pty(tp->id);
    pty_locktime[tp->num] = secclock() + 20;
    write_log(tp->pty, (char *) 0, -1);
  }
  if (tp->pid > 0) {
    kill(-tp->pid, SIGHUP);
    pty_name(slave, SLAVEPREFIX, tp->num);
    if ((fdut = open(UTMP_FILE, O_RDWR, 0644)) >= 0) {
      while (read(fdut, &utmpbuf, sizeof(utmpbuf)) == sizeof(utmpbuf))
	if (!strncmp(utmpbuf.ut_line, slave + 5, sizeof(utmpbuf.ut_line))) {
	  utmpbuf.ut_name[0] = 0;
	  utmpbuf.ut_host[0] = 0;
	  utmpbuf.ut_time = secclock();
#ifdef DEAD_PROCESS
	  utmpbuf.ut_type = DEAD_PROCESS;
#ifndef LINUX
	  utmpbuf.ut_exit.e_termination = 0;
	  utmpbuf.ut_exit.e_exit = 0;
#endif
#endif
	  lseek(fdut, -sizeof(utmpbuf), SEEK_CUR);
	  write(fdut, &utmpbuf, sizeof(utmpbuf));
	  close(fdut);
	  if ((fdut = open(WTMP_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644)) >= 0)
	    write(fdut, &utmpbuf, sizeof(utmpbuf));
	  break;
	}
    }
  }
  if (fdut >= 0) close(fdut);
  free_q(&tp->sndq);
  free(tp);
}

/*---------------------------------------------------------------------------*/

#define ASIZE 512

#define add_to_mbuf(chr) \
{ \
  if (!head) head = tail = alloc_mbuf(ASIZE); \
  if (tail->cnt >= ASIZE) tail = tail->next = alloc_mbuf(ASIZE); \
  tail->data[tail->cnt++] = chr; \
  cnt--; \
}

/*---------------------------------------------------------------------------*/

struct mbuf *login_read(tp, cnt)
struct login_cb *tp;
int cnt;
{

  int chr;
  struct mbuf *head, *tail;

  if (cnt <= 0) {
    off_read(tp->pty);
    return 0;
  }
  on_read(tp->pty, tp->readfnc, tp->fncarg);
  head = 0;
  while (cnt) {
    if (tp->inpcnt <= 0) {
      if ((tp->inpcnt = read(tp->pty, tp->inpptr = tp->inpbuf, sizeof(tp->inpbuf))) <= 0)
	return head;
      write_log(tp->pty, tp->inpbuf, tp->inpcnt);
    }
    tp->inpcnt--;
    chr = uchar(*tp->inpptr++);
    if (chr == 0x11 || chr == 0x13) {
      /* ignore XON / XOFF */
    } else if (tp->telnet) {
      add_to_mbuf(chr);
      if (chr == IAC) add_to_mbuf(IAC);
    } else {
      if (chr != '\n') add_to_mbuf(chr);
    }
  }
  return head;
}

/*---------------------------------------------------------------------------*/

void login_write(tp, bp)
struct login_cb *tp;
struct mbuf *bp;
{
  append(&tp->sndq, bp);
  on_write(tp->pty, (void (*)()) write_pty, tp);
  if (tp->linelen) write_pty(tp);
}

