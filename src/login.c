/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.c,v 1.13 1991/09/24 05:52:37 deyke Exp $ */

#include <sys/types.h>

#include <stdio.h>      /* must be before pwd.h */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ptyio.h>
#include <sys/rtprio.h>
#include <sys/stat.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>
#include <utmp.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "hpux.h"
#include "telnet.h"
#include "login.h"

extern struct utmp *getutent();
extern struct utmp *getutid();
extern void endutent();
extern void pututline();

#define MASTERPREFIX        "/dev/pty"
#define SLAVEPREFIX         "/dev/tty"

#define PASSWDFILE          "/etc/passwd"
#define PWLOCKFILE          "/etc/ptmp"

#define DEFAULTUSER         "guest"
#define FIRSTUID            400
#define MAXUID              4095
#define HOMEDIRPARENTPARENT "/users/funk"

#ifndef SCO
#define GID                 400
#else
#include "group.h"          /* Group-Nummer wird vom Inst-Script vergeben */
#endif                      /* 30.08.91 DH6SAE                            */

/* login server control block */

struct login_cb {
  int  pty;                     /* pty file descriptor */
  int  num;                     /* pty number */
  char  id[4];                  /* pty id (last 2 chars) */
  int  pid;                     /* process id of login process */
  char  buffer[512];            /* pty read buffer */
  char  *bufptr;                /* pty read buffer index */
  int  bufcnt;                  /* pty read buffer count */
  struct mbuf *sndq;            /* pty send queue */
  int  lastchr;                 /* last chr written to pty */
  int  linelen;                 /* counter for automatic line break */
  void (*readfnc) __ARGS((void *fncarg));
				/* func to call if pty is readable */
  void (*closefnc) __ARGS((void *fncarg));
				/* func to call if pty gets closed */
  void  *fncarg;                /* argument for readfnc and closefnc */
  int  telnet;                  /* telnet mode */
  int  state;                   /* telnet state */
  char  option[NOPTIONS+1];     /* telnet options */
};

static char  pty_inuse[256];

static int find_pty __ARGS((int *numptr, char *slave));
static void restore_pty __ARGS((char *id));
static void write_log_header __ARGS((int fd, char *user, char *protocol));
static int do_telnet __ARGS((struct login_cb *tp, int chr));
static void write_pty __ARGS((struct login_cb *tp));
static void excp_handler __ARGS((struct login_cb *tp));

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

void fixutmpfile()
{
  register struct utmp *up;

  while (up = getutent())
    if (up->ut_type == USER_PROCESS && kill(up->ut_pid, 0)) {
      restore_pty(up->ut_id);
      up->ut_user[0] = '\0';
      up->ut_type = DEAD_PROCESS;
      up->ut_exit.e_termination = 0;
      up->ut_exit.e_exit = 0;
      up->ut_time = secclock();
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
#ifdef SCO
  fprintf(fp, "%s::%d:%d::%s:/bin/sh\n", username, uid, GID, homedir);
#else
  fprintf(fp, "%s:,./:%d:%d::%s:/bin/sh\n", username, uid, GID, homedir);
#endif
  fclose(fp);
  pw = getpwuid(uid);
  endpwent();
  unlink(PWLOCKFILE);

  /* Create home directory */

  mkdir(homedirparent, 0755);
  mkdir(homedir, 0755);
  chown(homedir, uid, GID);
  return pw;
}

/*---------------------------------------------------------------------------*/

static void write_log_header(fd, user, protocol)
int  fd;
char  *user, *protocol;
{

  char  buf[1024];
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

static int  do_telnet(tp, chr)
struct login_cb *tp;
int  chr;
{
  struct termio termio;

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
      ioctl(tp->pty, TCGETA, &termio);
      termio.c_lflag |= (ECHO | ECHOE);
      ioctl(tp->pty, TCSETA, &termio);
    }
    tp->state = TS_DATA;
    break;
  case TS_DONT:
    if (chr <= NOPTIONS) tp->option[chr] = 0;
    if (chr == TN_ECHO) {
      ioctl(tp->pty, TCGETA, &termio);
      termio.c_lflag &= ~(ECHO | ECHOE);
      ioctl(tp->pty, TCSETA, &termio);
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

  char  *p;
  char  buf[256];
  int  chr;
  int  cnt;
  int  lastchr;

  p = buf;
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
  if (cnt = p - buf) {
    write(tp->pty, buf, (unsigned) cnt);
    write_log(tp->pty, buf, cnt);
  }
  if (!tp->sndq) off_write(tp->pty);
}

/*---------------------------------------------------------------------------*/

static void excp_handler(tp)
struct login_cb *tp;
{
  struct request_info request_info;

  if (ioctl(tp->pty, TIOCREQCHECK, &request_info)) return;
  ioctl(tp->pty, TIOCREQSET, &request_info);
  if (request_info.request == TIOCCLOSE) {
    off_read(tp->pty);
    off_write(tp->pty);
    off_excp(tp->pty);
    if (tp->closefnc) (*tp->closefnc)(tp->fncarg);
  }
}

/*---------------------------------------------------------------------------*/

struct login_cb *login_open(user, protocol, read_upcall, close_upcall, upcall_arg)
char  *user, *protocol;
void (*read_upcall) __ARGS((void *arg));
void (*close_upcall) __ARGS((void *arg));
void  *upcall_arg;
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
  on_excp(tp->pty, (void (*)()) excp_handler, tp);
  i = 1;
  ioctl(tp->pty, TIOCTRAP, &i);
  write_log_header(tp->pty, user, protocol);
  if (!(tp->pid = fork())) {
    rtprio(0, RTPRIO_RTOFF);
    pw = getpasswdentry(user, 1);
    if (!pw || pw->pw_passwd[0]) pw = getpasswdentry("", 0);
    for (i = 0; i < _NFILE; i++) close(i);
    setpgrp();
    open(slave, O_RDWR, 0666);
    dup(0);
    dup(0);
    chmod(slave, 0622);
    memset(&termio, 0, sizeof(termio));
    termio.c_iflag = ICRNL | IXOFF;
    termio.c_oflag = OPOST | ONLCR | TAB3;
    termio.c_cflag = B1200 | CS8 | CREAD | CLOCAL;
    termio.c_lflag = ISIG | ICANON;
    termio.c_cc[VINTR]  = 127;
    termio.c_cc[VQUIT]  =  28;
    termio.c_cc[VERASE] =   8;
    termio.c_cc[VKILL]  =  24;
    termio.c_cc[VEOF]   =   4;
    ioctl(0, TCSETA, &termio);
    ioctl(0, TCFLSH, 2);
    if (!pw || pw->pw_passwd[0]) exit(1);
    memset(&utmp, 0, sizeof(utmp));
    strcpy(utmp.ut_user, "LOGIN");
    strcpy(utmp.ut_id, tp->id);
    strcpy(utmp.ut_line, slave + 5);
    utmp.ut_pid = getpid();
    utmp.ut_type = LOGIN_PROCESS;
    utmp.ut_time = secclock();
#ifdef _UTMP_INCLUDED   /* for HP-UX 6.5 compatibility */
    strncpy(utmp.ut_host, protocol, sizeof(utmp.ut_host));
#endif
    pututline(&utmp);
    endutent();
#ifdef SCO
    execle("/tcp/bin/wampes_login", "login", pw->pw_name, (char *) 0, &env);
#else
    execle("/bin/login", "login", pw->pw_name, (char *) 0, &env);
#endif
    exit(1);
  }
  return tp;
}

/*---------------------------------------------------------------------------*/

void login_close(tp)
struct login_cb *tp;
{

  int  fwtmp;
  struct utmp utmp, *up;

  if (!tp) return;
  if (tp->pty > 0) {
    off_read(tp->pty);
    off_write(tp->pty);
    off_excp(tp->pty);
    close(tp->pty);
    restore_pty(tp->id);
    pty_inuse[tp->num] = 0;
    write_log(tp->pty, (char *) 0, -1);
  }
  if (tp->pid > 0) {
    kill(-tp->pid, SIGHUP);
    memset(&utmp, 0, sizeof(utmp));
    strcpy(utmp.ut_id, tp->id);
    utmp.ut_type = DEAD_PROCESS;
    if (up = getutid(&utmp)) {
      up->ut_user[0] = '\0';
      up->ut_type = DEAD_PROCESS;
      up->ut_exit.e_termination = 0;
      up->ut_exit.e_exit = 0;
      up->ut_time = secclock();
      memcpy(&utmp, up, sizeof(utmp));
      pututline(up);
      fwtmp = open("/etc/wtmp", O_WRONLY | O_CREAT | O_APPEND, 0644);
      write(fwtmp, (char *) &utmp, sizeof(utmp));
      close(fwtmp);
    }
    endutent();
  }
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
int  cnt;
{

  int  chr;
  struct mbuf *head, *tail;

  if (cnt <= 0) {
    off_read(tp->pty);
    return 0;
  }
  on_read(tp->pty, tp->readfnc, tp->fncarg);
  head = 0;
  while (cnt) {
    if (tp->bufcnt <= 0) {
      if ((tp->bufcnt = read(tp->pty, tp->bufptr = tp->buffer, sizeof(tp->buffer))) <= 0)
	return head;
      write_log(tp->pty, tp->buffer, tp->bufcnt);
    }
    tp->bufcnt--;
    chr = uchar(*tp->bufptr++);
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

