static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/cnet.c,v 1.11 1991/11/22 16:21:02 deyke Exp $";

#define _HPUX_SOURCE

#include <sys/types.h>

#include <curses.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <term.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

#ifdef ISC
#include <sys/tty.h>
#include <sys/stream.h>
#include <sys/ptem.h>
#include <sys/pty.h>
#define FIOSNBIO        FIONBIO
#endif

#if defined(__TURBOC__) || defined(__STDC__)
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#define const
#endif

#include "buildsaddr.h"

#define TERM_INP_FDES   0
#define TERM_INP_MASK   1
#define TERM_OUT_FDES   1
#define TERM_OUT_MASK   2

#define SOCK_INP_FDES   3
#define SOCK_INP_MASK   8
#define SOCK_OUT_FDES   3
#define SOCK_OUT_MASK   8

struct mbuf {
  struct mbuf *next;
  unsigned int  cnt;
  char  *data;
};

static int ansiterminal = 1;
static struct mbuf *sock_queue;
static struct mbuf *term_queue;
static struct termio prev_termio;

static void open_terminal __ARGS((void));
static void close_terminal __ARGS((void));
static void terminate __ARGS((void));
static void recvq __ARGS((int fd, struct mbuf **qp));
static void sendq __ARGS((int fd, struct mbuf **qp));

/*---------------------------------------------------------------------------*/

static void open_terminal()
{
  if (ansiterminal) {
    fputs("\033=", stdout);                     /* keypad application mode */
  } else {
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
  }
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void close_terminal()
{
  if (ansiterminal) {
    fputs("\033>", stdout);                     /* keypad numeric mode */
  } else {
    fputs("\033&s0A", stdout);                  /* disable xmitfnctn */
    fputs("\033&jR", stdout);                   /* release keys */
  }
  fflush(stdout);
}

/*---------------------------------------------------------------------------*/

static void terminate()
{
  long  arg;

  close(SOCK_OUT_FDES);
  arg = 0;
  ioctl(TERM_OUT_FDES, FIOSNBIO, &arg);
  for (; term_queue; term_queue = term_queue->next)
    write(TERM_OUT_FDES, term_queue->data, term_queue->cnt);
  close_terminal();
  ioctl(TERM_INP_FDES, TCSETA, &prev_termio);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void recvq(fd, qp)
int  fd;
struct mbuf **qp;
{

  char  buf[1024];
  int  n;
  struct mbuf *bp, *tp;

  n = read(fd, buf, sizeof(buf));
  if (n <= 0) terminate();
  bp = malloc(sizeof(*bp) + n);
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

static void sendq(fd, qp)
int  fd;
struct mbuf **qp;
{

  int  n;
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

int main(argc, argv)
int argc;
char **argv;
{

  char *server;
  int addrlen;
  int flags;
  int rmask;
  int wmask;
  long arg;
  struct sockaddr *addr;
  struct termio curr_termio;

#ifdef ISC
  server = (argc < 2) ? "*:4720" : argv[1];
#else
  server = (argc < 2) ? "unix:/tcp/.sockets/netkbd" : argv[1];
#endif
  if (!(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "%s: Cannot build address from \"%s\"\n", *argv, server);
    exit(1);
  }
  close(SOCK_OUT_FDES);
  if (socket(addr->sa_family, SOCK_STREAM, 0) != SOCK_OUT_FDES) {
    perror(*argv);
    exit(1);
  }
  if (connect(SOCK_OUT_FDES, addr, addrlen)) {
    perror(*argv);
    exit(1);
  }
  if ((flags = fcntl(SOCK_OUT_FDES, F_GETFL, 0)) == -1 ||
      fcntl(SOCK_OUT_FDES, F_SETFL, flags | O_NDELAY) == -1) {
    perror(*argv);
    exit(1);
  }

  setupterm(0, 1, 0);
  if (!strcmp(cursor_up, "\033A")) ansiterminal = 0;
  resetterm();

  open_terminal();
  arg = 1;
  ioctl(TERM_OUT_FDES, FIOSNBIO, &arg);
  ioctl(TERM_INP_FDES, TCGETA, &prev_termio);
  ioctl(TERM_INP_FDES, TCGETA, &curr_termio);
  curr_termio.c_lflag = 0;
  curr_termio.c_cc[VMIN] = 1;
  curr_termio.c_cc[VTIME] = 0;
  ioctl(TERM_INP_FDES, TCSETA, &curr_termio);

  for (; ; ) {
    rmask = SOCK_INP_MASK | TERM_INP_MASK;
    wmask = 0;
    if (sock_queue) wmask |= SOCK_OUT_MASK;
    if (term_queue) wmask |= TERM_OUT_MASK;
    if (select(4, &rmask, &wmask, (int *) 0, (struct timeval *) 0) < 1)
      continue;
    if (rmask & SOCK_INP_MASK) recvq(SOCK_INP_FDES, &term_queue);
    if (rmask & TERM_INP_MASK) recvq(TERM_INP_FDES, &sock_queue);
    if (wmask & SOCK_OUT_MASK) sendq(SOCK_OUT_FDES, &sock_queue);
    if (wmask & TERM_OUT_MASK) sendq(TERM_OUT_FDES, &term_queue);
  }
}

