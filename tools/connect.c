#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect.c,v 1.18 1996/02/13 15:30:54 deyke Exp $";
#endif

#ifndef linux
#define FD_SETSIZE      64
#endif

#include <sys/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

extern char *optarg;
extern int optind;

#define MAXCHANNELS     9                       /* Max number of connections */
#define PIDFILE         "/tmp/connect.pid"

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

typedef unsigned char uint8;

struct connection {
  uint8 call[AXALEN];
  uint8 buf[4096];
  int cnt;
};

/* AX.25 broadcast address: "QST-0" in shifted ascii */
static const uint8 ax25_bdcst[] = {
  'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, ('0' << 1) | E,
};

/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static const uint8 nr_bdcst[] = {
  'N' << 1, 'O' << 1, 'D' << 1, 'E' << 1, 'S' << 1, ' ' << 1, ('0' << 1) | E
};

static int all;
static int channels = 2;
static struct connection connections[MAXCHANNELS];
static struct fd_set chkread;

/*---------------------------------------------------------------------------*/

static void terminate(void)
{
  remove(PIDFILE);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static int addreq(const uint8 *a, const uint8 *b)
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

  int i;
  int multicast;
  struct connection *p1;
  uint8 *ap;
  uint8 *dest;
  uint8 *src;

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
  memcpy((char *) p->call, (char *) src, AXALEN);
  multicast = all || addreq(dest, ax25_bdcst) || addreq(dest, nr_bdcst);
  for (i = 0; i < channels; i++) {
    p1 = connections + i;
    if (multicast || !*p1->call || addreq(dest, p1->call))
      write(i, p->buf, p->cnt);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char tmp[1024];
  char *cp;
  FILE *fp;
  int ch;
  int errflag = 0;
  int fail = 0;
  int i;
  int n;
  struct connection *p;
  struct fd_set actread;
  struct timeval timeout;

  if ((fp = fopen(PIDFILE, "r"))) {
    kill(getw(fp), SIGKILL);
    fclose(fp);
    sleep(1);
  }
  remove(PIDFILE);

  while ((ch = getopt(argc, argv, "ac:f:k")) != EOF)
    switch (ch) {
    case 'a':
      all = 1;
      break;
    case 'c':
      channels = atoi(optarg);
      if (channels < 1 || channels > MAXCHANNELS) errflag = 1;
      break;
    case 'f':
      fail = atoi(optarg);
      if (fail < 0 || fail > 100) errflag = 1;
      break;
    case 'k':
      exit(0);
    case '?':
      errflag = 1;
      break;
    }
  if (errflag || optind < argc) {
    fprintf(stderr, "Usage: %s [-a] [-c channels] [-f failures]\n", *argv);
    fprintf(stderr, "       %s -k\n", *argv);
    exit(1);
  }

  if (fork()) exit(0);
  for (i = FD_SETSIZE - 1; i >= 0; i--) close(i);
  chdir("/");
  setsid();

  if ((fp = fopen(PIDFILE, "w"))) {
    putw((int) getpid(), fp);
    fclose(fp);
  }

  for (i = 0; i < channels; i++) {
    sprintf(tmp, "/dev/ptyr%d", i + 1);
    if (open(tmp, O_RDWR, 0666) != i) terminate();
    FD_SET(i, &chkread);
  }

  for (; ; ) {
    actread = chkread;
    timeout.tv_sec = 3600;
    timeout.tv_usec = 0;
    if (!select(channels, SEL_ARG(&actread), 0, 0, &timeout))
      terminate();
    for (i = 0; i < channels; i++)
      if (FD_ISSET(i, &actread)) {
	p = connections + i;
	n = read(i, cp = tmp, sizeof(tmp));
	while (--n >= 0) {
	  if (((p->buf[p->cnt++] = *cp++) & 0xff) == FR_END) {
	    if (p->cnt > 1 && (fail == 0 || rand() % 100 >= fail))
	      route_packet(p);
	    p->cnt = 0;
	  }
	}
      }
  }
}

