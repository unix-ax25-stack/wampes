#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect.c,v 1.9 1993/03/05 20:15:43 deyke Exp $";
#endif

#define _HPUX_SOURCE

#define FD_SETSIZE      32

#include <sys/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

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

static struct tab {
  char call[AXALEN];
  char buf[4096];
  int cnt;
} tab[MAXCHANNELS];

/* AX.25 broadcast address: "QST-0" in shifted ascii */
static const char ax25_bdcst[] = {
  'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, ('0' << 1) | E,
};

/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static const char nr_bdcst[] = {
  'N' << 1, 'O' << 1, 'D' << 1, 'E' << 1, 'S' << 1, ' ' << 1, ('0' << 1) | E
};

static int all;
static int channels = 2;

/*---------------------------------------------------------------------------*/

static void terminate(void)
{
  remove(PIDFILE);
  exit(0);
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

static void route_packet(struct tab *tp)
{

  char *ap;
  char *dest;
  char *src;
  int i;
  int multicast;
  struct tab *tp1;

  if ((*tp->buf & 0xf) != KISS_DATA) return;
  dest = tp->buf + 1;
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
  memcpy(tp->call, src, AXALEN);
  multicast = all || addreq(dest, ax25_bdcst) || addreq(dest, nr_bdcst);
  for (i = 0; i < channels; i++) {
    tp1 = tab + i;
    if (multicast || !*tp1->call || addreq(dest, tp1->call))
      write(i, tp->buf, tp->cnt);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  FILE * fp;
  char *p;
  char tmp[1024];
  int ch;
  int errflag = 0;
  int fail = 0;
  int i;
  int n;
  struct fd_set fmask;
  struct fd_set readmask;
  struct tab *tp;
  struct timeval timeout;

  if (fp = fopen(PIDFILE, "r")) {
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
    fprintf(stderr, "usage: %s [-c channels] [-f failures] [-s]\n", *argv);
    exit(1);
  }

  if (fork()) exit(0);
  for (i = sysconf(_SC_OPEN_MAX) - 1; i >= 0; i--) close(i);
  chdir("/");
  setsid();

  if (fp = fopen(PIDFILE, "w")) {
    putw(getpid(), fp);
    fclose(fp);
  }

  FD_ZERO(&fmask);
  for (i = 0; i < channels; i++) {
    sprintf(tmp, "/dev/ptyr%d", i + 1);
    if (open(tmp, O_RDWR, 0666) != i) terminate();
    FD_SET(i, &fmask);
  }

  for (; ; ) {
    readmask = fmask;
    timeout.tv_sec = 3600;
    timeout.tv_usec = 0;
    if (!select(channels, (int *) &readmask, (int *) 0, (int *) 0, &timeout))
      terminate();
    for (i = 0; i < channels; i++)
      if (FD_ISSET(i, &readmask)) {
	tp = tab + i;
	n = read(i, p = tmp, sizeof(tmp));
	while (--n >= 0) {
	  if (((tp->buf[tp->cnt++] = *p++) & 0xff) == FR_END) {
	    if (tp->cnt > 1 && (fail == 0 || rand() % 100 >= fail))
	      route_packet(tp);
	    tp->cnt = 0;
	  }
	}
      }
  }
}

