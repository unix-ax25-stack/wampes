#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/bridge.c,v 1.15 1996/02/04 11:17:45 deyke Exp $";
#endif

#ifndef linux
#define FD_SETSIZE      64
#endif

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifndef SOMAXCONN
#define SOMAXCONN       5
#endif

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

extern char *optarg;
extern int optind;

#include "buildsaddr.h"

/* SLIP constants */

#define FR_END          0300    /* Frame End */
#define FR_ESC          0333    /* Frame Escape */
#define T_FR_END        0334    /* Transposed frame end */
#define T_FR_ESC        0335    /* Transposed frame escape */

/* KISS constants */

#define KISS_DATA       0

/* AX25 constants */

#define AXALEN          7       /* Total AX.25 address length, including SSID */
#define E               0x01    /* Address extension bit */
#define REPEATED        0x80    /* Has-been-repeated bit in repeater field */
#define SSID            0x1e    /* Sub station ID */

struct connection {
  struct connection *prev, *next;
  char call[AXALEN];
  int fd;
  unsigned int cnt;
  char buf[4096];
};

/* AX.25 broadcast address: "QST-0" in shifted ascii */
static const char ax25_bdcst[] = {
  'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, ('0' << 1) | E,
};

/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static const char nr_bdcst[] = {
  'N' << 1, 'O' << 1, 'D' << 1, 'E' << 1, 'S' << 1, ' ' << 1, ('0' << 1) | E
};

static int all;
static int maxfd = -1;
static struct connection *connections;
static struct fd_set chkread;

/*---------------------------------------------------------------------------*/

static void create_connection(int flisten)
{

  int addrlen;
  int fd;
  struct connection *p;
  struct sockaddr addr;

  addrlen = 0;
  fd = accept(flisten, &addr, &addrlen);
  if (fd >= 0) {
    p = (struct connection *) calloc(1, sizeof(struct connection));
    p->fd = fd;
    FD_SET(fd, &chkread);
    if (maxfd < fd) maxfd = fd;
    if (connections) {
      p->next = connections;
      connections->prev = p;
    }
    connections = p;
  }
}

/*---------------------------------------------------------------------------*/

static void close_connection(struct connection *p)
{
  FD_CLR(p->fd, &chkread);
  if (p->fd == maxfd)
    while (--maxfd >= 0)
      if (FD_ISSET(maxfd, &chkread)) break;
  close(p->fd);
  if (p->prev) p->prev->next = p->next;
  if (p->next) p->next->prev = p->prev;
  if (p == connections) connections = p->next;
  free(p);
}

/*---------------------------------------------------------------------------*/

static int addreq(const char *a, const char *b)
{
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  if (*a++ != *b++) return 0;
  return (*a & SSID) == (*b & SSID);
}

/*---------------------------------------------------------------------------*/

static void route_packet(struct connection *p)
{

  char *ap;
  char *dest;
  char *src;
  int multicast;
  struct connection *p1, *p1next;

  if ((*p->buf & 0xf) != KISS_DATA) return;
  dest = p->buf + 1;
  ap = src = dest + AXALEN;
  while (!(ap[6] & E)) {
    ap += AXALEN;
    if (ap[6] & REPEATED)
      src = ap;
    else {
      dest = ap;
      break;
    }
  }
  memcpy(p->call, src, AXALEN);
  multicast = all || addreq(dest, ax25_bdcst) || addreq(dest, nr_bdcst);
  for (p1 = connections; p1; p1 = p1next) {
    p1next = p1->next;
    if (multicast || !*p1->call || addreq(dest, p1->call)) {
      if (write(p1->fd, p->buf, p->cnt) <= 0) close_connection(p1);
    }
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char buf[1024];
  int addrlen;
  int arg;
  int ch;
  int errflag = 0;
  int fail = 0;
  int flisten;
  int i;
  int n;
  struct connection *p;
  struct fd_set actread;
  struct sockaddr *addr;

  while ((ch = getopt(argc, argv, "af:")) != EOF)
    switch (ch) {
    case 'a':
      all = 1;
      break;
    case 'f':
      fail = atoi(optarg);
      if (fail < 0 || fail > 100) errflag = 1;
      break;
    case '?':
      errflag = 1;
      break;
    }
  if (errflag || optind < argc) {
    fprintf(stderr, "Usage: %s [-a] [-f failures]\n", *argv);
    exit(1);
  }

  for (n = 0; n < FD_SETSIZE; n++) close(n);
  chdir("/");
  setsid();
  signal(SIGPIPE, SIG_IGN);

  addr = build_sockaddr("*:4713", &addrlen);
  if (!addr) exit(1);
  flisten = socket(addr->sa_family, SOCK_STREAM, 0);
  if (flisten < 0) exit(1);
  arg = 1;
  setsockopt(flisten, SOL_SOCKET, SO_REUSEADDR, (char *) &arg, sizeof(arg));
  if (bind(flisten, addr, addrlen)) exit(1);
  if (listen(flisten, SOMAXCONN)) exit(1);
  FD_SET(flisten, &chkread);
  if (maxfd < flisten) maxfd = flisten;

  for (; ; ) {
    actread = chkread;
    if (select(maxfd + 1, SEL_ARG(&actread), 0, 0, 0) <= 0)
      continue;
    if (FD_ISSET(flisten, &actread)) create_connection(flisten);
    for (p = connections; p; p = p->next)
      if (FD_ISSET(p->fd, &actread)) {
	n = read(p->fd, buf, sizeof(buf));
	if (n <= 0) {
	  close_connection(p);
	  break;
	}
	for (i = 0; i < n; i++)
	  if (((p->buf[p->cnt++] = buf[i]) & 0xff) == FR_END) {
	    if (p->cnt > 1 && (fail == 0 || rand() % 100 >= fail))
	      route_packet(p);
	    p->cnt = 0;
	  }
      }
  }
}

