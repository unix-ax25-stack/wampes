#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/cnet.c,v 1.27 1993/10/13 22:31:23 deyke Exp $";
#endif

#ifndef linux
#define FD_SETSIZE 32
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
#include <termios.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifdef __hpux
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

#include "buildsaddr.h"

struct mbuf {
  struct mbuf *next;
  unsigned int cnt;
  char *data;
};

static int Ansiterminal = 1;
static int fdin = 0;
static int fdout = 1;
static int fdsock = -1;
static struct mbuf *sock_queue;
static struct mbuf *term_queue;
static struct termios prev_termios;

static void open_terminal(void);
static void close_terminal(void);
static void terminate(void);
static void recvq(int fd, struct mbuf **qp);
static void sendq(int fd, struct mbuf **qp);

/*---------------------------------------------------------------------------*/

static void open_terminal(void)
{
  if (!Ansiterminal) {
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
  if (!Ansiterminal) {
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
  for (; term_queue; term_queue = term_queue->next)
    write(fdout, term_queue->data, term_queue->cnt);
  close_terminal();
  tcsetattr(fdin, TCSANOW, &prev_termios);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void recvq(int fd, struct mbuf **qp)
{

  char buf[1024];
  int n;
  struct mbuf *bp, *tp;

  n = read(fd, buf, sizeof(buf));
  if (n <= 0) terminate();
  bp = (struct mbuf *) malloc(sizeof(*bp) + n);
  if (!bp) terminate();
  bp->next = 0;
  bp->cnt = n;
  bp->data = (char *) (bp + 1);
  memcpy(bp->data, buf, n);
  if (*qp) {
    for (tp = *qp; tp->next; tp = tp->next) ;
    tp->next = bp;
  } else
    *qp = bp;
}

/*---------------------------------------------------------------------------*/

static void sendq(int fd, struct mbuf **qp)
{

  int n;
  struct mbuf *bp;

  bp = *qp;
  n = write(fd, bp->data, bp->cnt);
  if (n <= 0) terminate();
  bp->data += n;
  bp->cnt -= n;
  if (!bp->cnt) {
    *qp = bp->next;
    free(bp);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char *ap;
  char *server;
  char area[1024];
  char bp[1024];
  int addrlen;
  int flags;
  struct fd_set rmask;
  struct fd_set wmask;
  struct sockaddr *addr;
  struct termios curr_termios;

#ifdef macII
  setposix();
#endif

  signal(SIGPIPE, SIG_IGN);
  signal(SIGTERM, (void (*)()) terminate);
  signal(SIGHUP, (void (*)()) terminate);

  tcgetattr(fdin, &prev_termios);
  ap = area;
  if (tgetent(bp, getenv("TERM")) == 1 && strcmp(tgetstr("up", &ap), "\033[A"))
    Ansiterminal = 0;

#if 1
  server = (argc < 2) ? "unix:/tcp/.sockets/netcmd" : argv[1];
#else
  server = (argc < 2) ? "*:4720" : argv[1];
#endif
  if (!(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "%s: Cannot build address from \"%s\"\n", *argv, server);
    terminate();
  }
  if ((fdsock = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) {
    perror("socket");
    terminate();
  }
  if (connect(fdsock, addr, addrlen)) {
    perror("connect");
    terminate();
  }
  write(fdsock, "console\n", 8);
  if (Ansiterminal)
    write(fdsock, "\033[D", 3);
  else
    write(fdsock, "\033D", 2);
  if ((flags = fcntl(fdsock, F_GETFL, 0)) == -1 ||
      fcntl(fdsock, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    terminate();
  }

  open_terminal();
  curr_termios = prev_termios;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 0;
  curr_termios.c_cc[VTIME] = 0;
  tcsetattr(fdin, TCSANOW, &curr_termios);
  if ((flags = fcntl(fdout, F_GETFL, 0)) == -1 ||
      fcntl(fdout, F_SETFL, flags | O_NONBLOCK) == -1) {
    perror("fcntl");
    terminate();
  }

  for (; ; ) {
    FD_ZERO(&rmask);
    FD_SET(fdin, &rmask);
    FD_SET(fdsock, &rmask);
    FD_ZERO(&wmask);
    if (sock_queue) FD_SET(fdsock, &wmask);
    if (term_queue) FD_SET(fdout,  &wmask);
    if (select(fdsock + 1, SEL_ARG(&rmask), SEL_ARG(&wmask), SEL_ARG(0), (struct timeval *) 0) > 0) {
      if (FD_ISSET(fdsock, &rmask)) recvq(fdsock, &term_queue);
      if (FD_ISSET(fdin,   &rmask)) recvq(fdin,   &sock_queue);
      if (FD_ISSET(fdsock, &wmask)) sendq(fdsock, &sock_queue);
      if (FD_ISSET(fdout,  &wmask)) sendq(fdout,  &term_queue);
    }
  }
}

