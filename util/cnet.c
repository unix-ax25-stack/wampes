#ifndef __lint
static const char rcsid[] = "@(#) $Id: cnet.c,v 1.44 1999/01/22 21:22:04 deyke Exp $";
#endif

#include <sys/types.h>

#include <curses.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifdef ibm032
#include <sgtty.h>
#include <sys/fcntl.h>
#else
#include <termios.h>
#endif

extern char *optarg;
extern int optind;

#ifndef MAXIOV
#if defined IOV_MAX
#define MAXIOV          IOV_MAX
#else
#define MAXIOV          16
#endif
#endif

#ifndef O_NONBLOCK
#define O_NONBLOCK      O_NDELAY
#endif

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

#include "buildsaddr.h"
#include "configure.h"

struct mbuf {
  struct mbuf *next;
  unsigned int cnt;
  char *data;
};

static int HP_terminal;
static int fdin = 0;
static int fdout = 1;
static int fdsock = -1;
static struct mbuf *sock_queue;
static struct mbuf *term_queue;

#ifdef ibm032
static struct sgttyb prev_sgttyb;
static struct tchars prev_tchars;
static struct ltchars prev_ltchars;
#else
static struct termios prev_termios;
#endif

/*---------------------------------------------------------------------------*/

static void open_terminal(void)
{
  if (HP_terminal) {
    fputs("\033Z", stdout);                     /* display fncts off       */
    fputs("\033&k1I", stdout);                  /* enable ascii 8 bits     */
    fputs("\033&s1A", stdout);                  /* enable xmitfnctn        */
    fputs("\033&jB", stdout);                   /* enable user keys        */
    fputs("\033&j@", stdout);                   /* remove key labels       */
    fputs("\033&jS", stdout);                   /* lock keys               */
    fputs("\033&f0a1k0d2L\033p", stdout);       /* key1 = ESC p            */
    fputs("\033&f0a2k0d2L\033q", stdout);       /* key2 = ESC q            */
    fputs("\033&f0a3k0d2L\033r", stdout);       /* key3 = ESC r            */
    fputs("\033&f0a4k0d2L\033s", stdout);       /* key4 = ESC s            */
    fputs("\033&f0a5k0d2L\033t", stdout);       /* key5 = ESC t            */
    fputs("\033&f0a6k0d2L\033u", stdout);       /* key6 = ESC u            */
    fputs("\033&f0a7k0d2L\033v", stdout);       /* key7 = ESC v            */
    fputs("\033&f0a8k0d2L\033w", stdout);       /* key8 = ESC w            */
    fflush(stdout);
  }
}

/*---------------------------------------------------------------------------*/

static void close_terminal(void)
{
  if (HP_terminal) {
    fputs("\033&s0A", stdout);                  /* disable xmitfnctn */
    fputs("\033&jR", stdout);                   /* release keys */
    fflush(stdout);
  }
}

/*---------------------------------------------------------------------------*/

static void terminate(void)
{
  close(fdsock);
  fcntl(fdout, F_SETFL, fcntl(fdout, F_GETFL, 0) & ~O_NONBLOCK);
  for (; term_queue; term_queue = term_queue->next) {
    write(fdout, term_queue->data, term_queue->cnt);
  }
  close_terminal();
#ifdef ibm032
  ioctl(fdin, TIOCSETP, &prev_sgttyb);
  ioctl(fdin, TIOCSETC, &prev_tchars);
  ioctl(fdin, TIOCSLTC, &prev_ltchars);
#else
  tcsetattr(fdin, TCSANOW, &prev_termios);
#endif
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void recvq(int fd, struct mbuf **qp)
{

  char buf[1024];
  int n;
  struct mbuf *bp, *tp;

  n = read(fd, buf, sizeof(buf));
  if (n <= 0) {
    terminate();
  }
  bp = (struct mbuf *) malloc(sizeof(struct mbuf) + n);
  if (!bp) {
    terminate();
  }
  bp->next = 0;
  bp->cnt = n;
  bp->data = (char *) (bp + 1);
  memcpy(bp->data, buf, n);
  if (*qp) {
    for (tp = *qp; tp->next; tp = tp->next) ;
    tp->next = bp;
  } else {
    *qp = bp;
  }
}

/*---------------------------------------------------------------------------*/

static void sendq(int fd, struct mbuf **qp)
{

  int n;
  struct iovec iov[MAXIOV];
  struct mbuf *bp;

  n = 0;
  for (bp = *qp; bp && n < MAXIOV; bp = bp->next) {
    iov[n].iov_base = bp->data;
    iov[n].iov_len = bp->cnt;
    n++;
  }
  n = writev(fd, iov, n);
  if (n <= 0) {
    terminate();
  }
  while (n > 0) {
    bp = *qp;
    if (n >= bp->cnt) {
      n -= bp->cnt;
      *qp = bp->next;
      free(bp);
    } else {
      bp->data += n;
      bp->cnt -= n;
      n = 0;
    }
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  TYPE_FD_SET rmask;
  TYPE_FD_SET wmask;
  char *cmdptr = 0;
  char *progname = "cnet";
  char *server = "unix:/tcp/.sockets/netcmd";
  char cmdbuf[1024];
  int addrlen;
  int chr;
  int errflag;
  int flags;
  int n;
  struct sockaddr *addr;

#ifdef ibm032
  struct sgttyb curr_sgttyb;
  struct tchars curr_tchars;
  struct ltchars curr_ltchars;
#else
  struct termios curr_termios;
#endif

#ifdef macII
  setposix();
#endif

  if (argc >= 1) {
    progname = *argv;
  }

  errflag = 0;
  while ((chr = getopt(argc, argv, "c:h")) != EOF) {
    switch (chr) {
    case 'c':
      cmdptr = optarg;
      break;
    case 'h':
      HP_terminal = 1;
      break;
    case '?':
      errflag = 1;
      break;
    }
  }
  if (optind < argc) {
    server = argv[optind++];
  }
  if (errflag || optind < argc) {
    fprintf(stderr, "Usage: %s [-c command] [-h] [server]\n", progname);
    exit(1);
  }

  if (!(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "%s: Cannot build address from \"%s\"\n", progname, server);
    exit(1);
  }
  if ((fdsock = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    exit(1);
  }
  if (connect(fdsock, addr, addrlen)) {
    perror("connect");
    exit(1);
  }

  if (cmdptr) {
    sprintf(cmdbuf, "command %s\n", cmdptr);
    write(fdsock, cmdbuf, strlen(cmdbuf));
    while ((n = read(fdsock, cmdbuf, sizeof(cmdbuf))) > 0) {
      write(1, cmdbuf, n);
    }
    exit(0);
  }

  write(fdsock, "console\n", 8);

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT,  (void (*)(int)) terminate);
  signal(SIGQUIT, (void (*)(int)) terminate);
  signal(SIGTERM, (void (*)(int)) terminate);
  signal(SIGHUP,  (void (*)(int)) terminate);

#ifdef ibm032
  ioctl(fdin, TIOCGETP, &prev_sgttyb);
  ioctl(fdin, TIOCGETC, &prev_tchars);
  ioctl(fdin, TIOCGLTC, &prev_ltchars);
#else
  tcgetattr(fdin, &prev_termios);
#endif

  if (HP_terminal) {
    write(fdsock, "\033D", 2);
  } else {
    write(fdsock, "\033[D", 3);
  }
  if ((flags = fcntl(fdsock, F_GETFL, 0)) == -1 ||
      fcntl(fdsock, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    terminate();
  }

  open_terminal();
#ifdef ibm032
  curr_sgttyb = prev_sgttyb;
  curr_tchars = prev_tchars;
  curr_ltchars = prev_ltchars;
  curr_sgttyb.sg_flags |= CBREAK;
  curr_sgttyb.sg_flags &= ~ECHO;
/*
  curr_tchars.t_intrc = 0xff;
  curr_tchars.t_quitc = 0xff;
  curr_tchars.t_eofc = 0xff;
  curr_tchars.t_brkc = 0xff;
*/
  curr_ltchars.t_suspc = 0xff;
  curr_ltchars.t_dsuspc = 0xff;
  ioctl(fdin, TIOCSETP, &curr_sgttyb);
  ioctl(fdin, TIOCSETC, &curr_tchars);
  ioctl(fdin, TIOCSLTC, &curr_ltchars);
#else
  curr_termios = prev_termios;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 0;
  curr_termios.c_cc[VTIME] = 0;
  tcsetattr(fdin, TCSANOW, &curr_termios);
#endif
  if ((flags = fcntl(fdout, F_GETFL, 0)) == -1 ||
      fcntl(fdout, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    terminate();
  }

  for (;;) {
    FD_ZERO(&rmask);
    FD_SET(fdin, &rmask);
    FD_SET(fdsock, &rmask);
    FD_ZERO(&wmask);
    if (sock_queue) {
      FD_SET(fdsock, &wmask);
    }
    if (term_queue) {
      FD_SET(fdout, &wmask);
    }
    if (select(fdsock + 1, SEL_ARG(&rmask), SEL_ARG(&wmask), 0, 0) > 0) {
      if (FD_ISSET(fdsock, &rmask)) {
	recvq(fdsock, &term_queue);
      }
      if (FD_ISSET(fdin, &rmask)) {
	recvq(fdin, &sock_queue);
      }
      if (FD_ISSET(fdsock, &wmask)) {
	sendq(fdsock, &sock_queue);
      }
      if (FD_ISSET(fdout, &wmask)) {
	sendq(fdout, &term_queue);
      }
    }
  }
}
