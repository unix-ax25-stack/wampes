/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.c,v 1.44 1993/07/17 20:34:01 deyke Exp $ */

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
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#ifdef __386BSD__
#include <sys/ioctl.h>
#endif

#ifdef macII
#undef LOGIN_PROCESS
extern struct utmp *getutent();
#endif

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "hpux.h"
#include "telnet.h"
#include "cmdparse.h"
#include "commands.h"
#include "login.h"

#define MASTERPREFIX        "/dev/pty"
#define SLAVEPREFIX         "/dev/tty"
#define NUMPTY              256

#define PASSWDFILE          "/etc/passwd"
#define SPASSWDFILE         "/.secure/etc/passwd"
#define PWLOCKFILE          "/etc/ptmp"

#define MAXUID              32000

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
  void (*readfnc)(void *fncarg);
				/* func to call if pty is readable */
  void (*closefnc)(void *fncarg);
				/* func to call if pty gets closed */
  void *fncarg;                 /* argument for readfnc and closefnc */
  FILE *logfp;                  /* log file pointer */
  int loglastchr;               /* last char written to logfile */
  int telnet;                   /* telnet mode */
  int state;                    /* telnet state */
  char option[NOPTIONS+1];      /* telnet options */
};

static char Defaultuser[16] = "guest";
static char Homedir[80] = "/users/funk";
static char Logfiledir[80];
static char Shell[80];
static int Auto = 1;
static int Create = 1;
static int Gid = 400;
static int Maxuid = MAXUID;
static int Minuid = 400;
static int32 Pty_locktime[NUMPTY];

static int find_pty(int *numptr, char *slave);
static void restore_pty(const char *id);
static int callvalid(const char *call);
static char *find_user_name(const char *name);
static void write_log(struct login_cb *tp, const char *buf, int cnt);
static FILE *fopen_logfile(const char *user, const char *protocol);
static int do_telnet(struct login_cb *tp, int chr);
static void write_pty(struct login_cb *tp);
static void death_handler(struct login_cb *tp);
static int dologinauto(int argc, char *argv [], void *p);
static int dologincreate(int argc, char *argv [], void *p);
static int dologindefaultuser(int argc, char *argv [], void *p);
static int dologingid(int argc, char *argv [], void *p);
static int dologinhomedir(int argc, char *argv [], void *p);
static int dologinlogfiledir(int argc, char *argv [], void *p);
static int dologinmaxuid(int argc, char *argv [], void *p);
static int dologinminuid(int argc, char *argv [], void *p);
static int dologinshell(int argc, char *argv [], void *p);

/*---------------------------------------------------------------------------*/

#define pty_name(name, prefix, num) \
  sprintf(name, "%s%c%x", prefix, 'p' + (num >> 4), num & 0xf)

/*---------------------------------------------------------------------------*/

static int find_pty(int *numptr, char *slave)
{

  char master[80];
  int fd;
  int num;

  for (num = 0; num < NUMPTY; num++)
    if (Pty_locktime[num] < secclock()) {
      Pty_locktime[num] = secclock() + 60;
      pty_name(master, MASTERPREFIX, num);
      if ((fd = open(master, O_RDWR | O_NONBLOCK, 0600)) >= 0) {
	*numptr = num;
#ifdef ULTRIX_RISC
	fcntl(fd, F_SETFL, FIONBIO); /* O_NONBLOCK does not work?! */
#endif
#ifdef macII
	/* this extra fcntl is necessary for an unknown reason, since the
	   flag is already set... perhaps there's a race somewhere? */
	fcntl(fd, F_SETFL, O_NONBLOCK | fcntl(fd, F_GETFL, 0));
#endif
	pty_name(slave, SLAVEPREFIX, num);
	return fd;
      }
    }
  return (-1);
}

/*---------------------------------------------------------------------------*/

static void restore_pty(const char *id)
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

void fixutmpfile(void)
{

#ifdef USER_PROCESS

  struct utmp *up;

  while (up = getutent())
    if (up->ut_type == USER_PROCESS && kill(up->ut_pid, 0)) {
      restore_pty(up->ut_id);
      up->ut_name[0] = 0;
      up->ut_type = DEAD_PROCESS;
#ifndef linux
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

static int callvalid(const char *call)
{
  int d, l;

  l = strlen(call);
  if (l < 3 || l > 6) return 0;
  if (isdigit(uchar(call[0])) && isdigit(uchar(call[1]))) return 0;
  if (!(isdigit(uchar(call[1])) || isdigit(uchar(call[2])))) return 0;
  if (!isalpha(uchar(call[l-1]))) return 0;
  d = 0;
  for (; *call; call++) {
    if (!isalnum(uchar(*call))) return 0;
    if (isdigit(uchar(*call))) d++;
  }
  if (d < 1 || d > 2) return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

static char *find_user_name(const char *name)
{

  char *cp;
  static char username[128];

  for (; ; ) {
    while (*name && !isalnum(*name & 0xff)) name++;
    for (cp = username; isalnum(*name & 0xff); *cp++ = Xtolower(*name++)) ;
    *cp = 0;
    if (!*username) return Defaultuser;
    if (callvalid(username)) return username;
  }
}

/*---------------------------------------------------------------------------*/

struct passwd *getpasswdentry(const char *name, int create)
{

  FILE *fp;
  char cmdbuf[1024];
  char homedir[80];
  char homedirparent[80];
  int fd;
  int secured = 0;
  int uid;
  static char bitmap[MAXUID+1];         /* Keep off the stack */
  struct passwd *pw;
  struct stat statbuf;

  /* Search existing passwd entry */

  if (pw = getpwnam(name)) return pw;
  if (!create) return 0;

  /* Find free user id */

  if ((fd = open(PWLOCKFILE, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) return 0;
  close(fd);
  memset(bitmap, 0, sizeof(bitmap));
  while (pw = getpwent()) {
    if (!strcmp(name, pw->pw_name)) break;
    if (pw->pw_uid <= Maxuid) bitmap[pw->pw_uid] = 1;
  }
  endpwent();
  if (pw) {
    unlink(PWLOCKFILE);
    return pw;
  }
  for (uid = Minuid; uid <= Maxuid && bitmap[uid]; uid++) ;
  if (uid > Maxuid) {
    unlink(PWLOCKFILE);
    return 0;
  }

  /* Add user to passwd file(s) */

  sprintf(homedirparent, "%s/%.3s...", Homedir, name);
  sprintf(homedir, "%s/%s", homedirparent, name);
#ifdef __386BSD__
  sprintf(cmdbuf, "chpass -a '%s::%d:%d::0:0::%s:%s' >/dev/null 2>&1", name, uid, Gid, homedir, Shell);
  system(cmdbuf);
#else
#ifdef __hpux
  if (!stat(SPASSWDFILE, &statbuf) && (fp = fopen(SPASSWDFILE, "a"))) {
    fprintf(fp, "%s::%d:0\n", name, uid);
    fclose(fp);
    secured = 1;
  }
#endif
  if (!(fp = fopen(PASSWDFILE, "a"))) {
    unlink(PWLOCKFILE);
    return 0;
  }
  fprintf(fp,
	  "%s:%s:%d:%d::%s:%s\n",
	  name,
	  secured ? "*" : "",
	  uid,
	  Gid,
	  homedir,
	  Shell);
  fclose(fp);
#endif
  pw = getpwuid(uid);
  unlink(PWLOCKFILE);

  /* Create home directory */

  mkdir(homedirparent, 0755);
  mkdir(homedir, 0755);
  chown(homedir, uid, Gid);
  return pw;
}

/*---------------------------------------------------------------------------*/

static void write_log(struct login_cb *tp, const char *buf, int cnt)
{
  int chr;

  if (tp->logfp) {
    while (--cnt >= 0) {
      chr = *buf++;
      if (!chr) {
	/* ignore nulls */
      } else if (chr == '\r')
	putc('\n', tp->logfp);
      else if (chr != '\n' || tp->loglastchr != '\r')
	putc(chr, tp->logfp);
      tp->loglastchr = chr;
    }
    fflush(tp->logfp);
  }
}

/*---------------------------------------------------------------------------*/

static FILE *fopen_logfile(const char *user, const char *protocol)
{

  FILE *fp;
  char filename[80];
  static int cnt;
  struct tm *tm;

  if (!*Logfiledir) return 0;
  sprintf(filename, "%s/log.%05d.%04d", Logfiledir, getpid(), cnt++);
  if (fp = fopen(filename, "a")) {
    tm = localtime((long *) &Secclock);
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
  }
  return fp;
}

/*---------------------------------------------------------------------------*/

static int do_telnet(struct login_cb *tp, int chr)
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

static void write_pty(struct login_cb *tp)
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
    if (n) write_log(tp, tp->outptr, n);
    tp->outptr += n;
    tp->outcnt -= n;
  }
  if (!tp->outcnt && !tp->sndq) off_write(tp->pty);
}

/*---------------------------------------------------------------------------*/

static void death_handler(struct login_cb *tp)
{
  off_read(tp->pty);
  off_write(tp->pty);
  if (tp->closefnc) (*tp->closefnc)(tp->fncarg);
}

/*---------------------------------------------------------------------------*/

struct login_cb *login_open(const char *user, const char *protocol, void (*read_upcall)(void *arg), void (*close_upcall)(void *arg), void *upcall_arg)
{

  char *argv[16];
  char *env = 0;
  char slave[80];
  int argc;
  int i;
  struct login_cb *tp;
  struct passwd *pw;
  struct termios termios;
  struct utmp utmpbuf;

  tp = (struct login_cb *) calloc(1, sizeof(*tp));
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
  tp->logfp = fopen_logfile(user, protocol);
  if (!(tp->pid = dofork())) {
    if (Auto) {
      pw = getpasswdentry(find_user_name(user), Create);
      if (!pw && *Defaultuser) pw = getpasswdentry(Defaultuser, 0);
    } else
      pw = 0;
    for (i = 0; i < FD_SETSIZE; i++) close(i);
    setsid();
    open(slave, O_RDWR, 0666);
    dup(0);
    dup(0);
    chmod(slave, 0622);
#ifdef TIOCSCTTY
    ioctl(0, TIOCSCTTY, (char *) 0);
#endif
    memset((char *) &termios, 0, sizeof(termios));
    termios.c_iflag = ICRNL | IXOFF;
    termios.c_oflag = OPOST | ONLCR;
#ifdef TAB3
    termios.c_oflag |= TAB3;
#endif
#ifdef OXTABS
    termios.c_oflag |= OXTABS;
#endif
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
#ifdef LOGIN_PROCESS
    memset(&utmpbuf, 0, sizeof(utmpbuf));
    strcpy(utmpbuf.ut_name, "LOGIN");
    strcpy(utmpbuf.ut_id, tp->id);
    strcpy(utmpbuf.ut_line, slave + 5);
    utmpbuf.ut_pid = getpid();
    utmpbuf.ut_type = LOGIN_PROCESS;
    utmpbuf.ut_time = secclock();
#ifdef __hpux
    strncpy(utmpbuf.ut_host, protocol, sizeof(utmpbuf.ut_host));
#endif
    pututline(&utmpbuf);
    endutent();
#endif
    argc = 0;
#if defined(sun) || defined(__386BSD__)
    argv[argc++] = "/usr/bin/login";
    argv[argc++] = "-h";
    argv[argc++] = (char *) protocol;
#elif macII
    argv[argc++] = "/bin/remlogin";
    argv[argc++] = "-h";
    argv[argc++] = (char *) protocol;
#elif linux
    argv[argc++] = "/bin/login";
    argv[argc++] = "-h";
    argv[argc++] = (char *) protocol;
#else
    argv[argc++] = "/bin/login";
#endif
    if (pw) argv[argc++] = pw->pw_name;
    argv[argc] = 0;
    execve(argv[0], argv, &env);
    exit(1);
  }
  on_death(tp->pid, (void (*)()) death_handler, tp);
  return tp;
}

/*---------------------------------------------------------------------------*/

void login_close(struct login_cb *tp)
{

#ifndef UTMP_FILE
#ifdef _PATH_UTMP
#define UTMP_FILE       _PATH_UTMP
#else
#define UTMP_FILE       "/etc/utmp"
#endif
#endif

#ifndef WTMP_FILE
#ifdef _PATH_WTMP
#define WTMP_FILE       _PATH_WTMP
#else
#define WTMP_FILE       "/var/adm/wtmp"
#endif
#endif

  char slave[80];
  int fdut = -1;
  struct utmp utmpbuf;

  if (!tp) return;
  if (tp->pty > 0) {
    off_read(tp->pty);
    off_write(tp->pty);
    off_death(tp->pid);
#ifdef linux
    ioctl(tp->pty, TCFLSH, 2);
#endif
    close(tp->pty);
    restore_pty(tp->id);
    Pty_locktime[tp->num] = secclock() + 20;
  }
  if (tp->logfp) fclose(tp->logfp);
  if (tp->pid > 0) {
    kill(-tp->pid, SIGHUP);
    pty_name(slave, SLAVEPREFIX, tp->num);
    if ((fdut = open(UTMP_FILE, O_RDWR, 0644)) >= 0) {
      while (read(fdut, &utmpbuf, sizeof(utmpbuf)) == sizeof(utmpbuf))
	if (!strncmp(utmpbuf.ut_line, slave + 5, sizeof(utmpbuf.ut_line))) {
	  utmpbuf.ut_name[0] = 0;
	  utmpbuf.ut_time = secclock();
#ifdef DEAD_PROCESS
	  utmpbuf.ut_type = DEAD_PROCESS;
#ifndef linux
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

struct mbuf *login_read(struct login_cb *tp, int cnt)
{

  int chr;
  struct mbuf *head, *tail;

  if (cnt <= 0) {
    off_read(tp->pty);
    return 0;
  }
#ifdef ULTRIX_RISC
  fcntl(tp->pty, F_SETFL, FIONBIO); /* Without this the read blocks! */
#endif
  on_read(tp->pty, tp->readfnc, tp->fncarg);
  head = 0;
  while (cnt) {
    if (tp->inpcnt <= 0) {
      if ((tp->inpcnt = read(tp->pty, tp->inpptr = tp->inpbuf, sizeof(tp->inpbuf))) <= 0)
	return head;
      write_log(tp, tp->inpbuf, tp->inpcnt);
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

void login_write(struct login_cb *tp, struct mbuf *bp)
{
  append(&tp->sndq, bp);
  on_write(tp->pty, (void (*)()) write_pty, tp);
  if (tp->linelen) write_pty(tp);
}

/*---------------------------------------------------------------------------*/

static int dologinauto(int argc, char *argv[], void *p)
{
  return setbool(&Auto, "Auto login", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int dologincreate(int argc, char *argv[], void *p)
{
  return setbool(&Create, "Create account", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int dologindefaultuser(int argc, char *argv[], void *p)
{
  if (argc < 2)
    printf("Default user name \"%s\"\n", Defaultuser);
  else {
    memcpy(Defaultuser, argv[1], sizeof(Defaultuser));
    Defaultuser[sizeof(Defaultuser)-1] = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dologingid(int argc, char *argv[], void *p)
{
  return setint(&Gid, "Group ID", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int dologinhomedir(int argc, char *argv[], void *p)
{
  if (argc < 2)
    printf("Home directory \"%s\"\n", Homedir);
  else {
    memcpy(Homedir, argv[1], sizeof(Homedir));
    Homedir[sizeof(Homedir)-1] = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dologinlogfiledir(int argc, char *argv[], void *p)
{
  if (argc < 2)
    printf("Logfile directory \"%s\"\n", Logfiledir);
  else {
    memcpy(Logfiledir, argv[1], sizeof(Logfiledir));
    Logfiledir[sizeof(Logfiledir)-1] = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int dologinmaxuid(int argc, char *argv[], void *p)
{
  return setintrc(&Maxuid, "Max user ID", argc, argv, 1, MAXUID);
}

/*---------------------------------------------------------------------------*/

static int dologinminuid(int argc, char *argv[], void *p)
{
  return setintrc(&Minuid, "Min user ID", argc, argv, 1, MAXUID);
}

/*---------------------------------------------------------------------------*/

static int dologinshell(int argc, char *argv[], void *p)
{
  if (argc < 2)
    printf("Login shell \"%s\"\n", Shell);
  else {
    memcpy(Shell, argv[1], sizeof(Shell));
    Shell[sizeof(Shell)-1] = 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int dologin(int argc, char *argv[], void *p)
{

  static struct cmds Logincmds[] = {
    "auto",         dologinauto,        0, 0, NULLCHAR,
    "create",       dologincreate,      0, 0, NULLCHAR,
    "defaultuser",  dologindefaultuser, 0, 0, NULLCHAR,
    "gid",          dologingid,         0, 0, NULLCHAR,
    "homedir",      dologinhomedir,     0, 0, NULLCHAR,
    "logfiledir",   dologinlogfiledir,  0, 0, NULLCHAR,
    "maxuid",       dologinmaxuid,      0, 0, NULLCHAR,
    "minuid",       dologinminuid,      0, 0, NULLCHAR,
    "shell",        dologinshell,       0, 0, NULLCHAR,
    NULLCHAR
  };

  return subcmd(Logincmds, argc, argv, p);
}

