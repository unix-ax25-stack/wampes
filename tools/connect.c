#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect.c,v 1.6 1993/02/23 21:33:47 deyke Exp $";
#endif

#define _HPUX_SOURCE

#define FD_SETSIZE      32

#include <sys/types.h>

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

extern char *optarg;
extern int optind;

#define MAXCHANNELS     9       /* Max number of connections */

#define FR_END          0300    /* Frame End */
#define FR_ESC          0333    /* Frame Escape */
#define T_FR_END        0334    /* Transposed frame end */
#define T_FR_ESC        0335    /* Transposed frame escape */

#define PIDFILE         "/tmp/connect.pid"

#define uchar(c)        ((c) & 0xff)

static void terminate(void)
{
  remove(PIDFILE);
  exit(0);
}

int main(int argc, char **argv)
{

  static struct tab {
    char buf[4096];
    int cnt;
  } tab[MAXCHANNELS];

  FILE * fp;
  char tmp[1024];
  int ch;
  int channels = 2;
  int errflag = 0;
  int fail = 0;
  int i;
  int self = 0;
  struct fd_set fmask;
  struct tab *tp;
  struct timeval timeout;

  if (fp = fopen(PIDFILE, "r")) {
    kill(getw(fp), SIGKILL);
    fclose(fp);
    sleep(1);
  }
  remove(PIDFILE);

  while ((ch = getopt(argc, argv, "c:f:s")) != EOF)
    switch (ch) {
    case 'c':
      channels = atoi(optarg);
      if (channels < 1 || channels > MAXCHANNELS) errflag = 1;
      break;
    case 'f':
      fail = atoi(optarg);
      if (fail < 0 || fail > 100) errflag = 1;
      break;
    case 's':
      self = 1;
      break;
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

    char c;
    int j;
    struct fd_set readmask = fmask;

    timeout.tv_sec = 3600;
    timeout.tv_usec = 0;
    if (!select(channels, (int *) &readmask, (int *) 0, (int *) 0, &timeout))
      terminate();
    for (i = 0; i < channels; i++)
      if (FD_ISSET(i, &readmask)) {
	tp = tab + i;
	read(i, &c, 1);
	tp->buf[tp->cnt++] = c;
	if (uchar(c) == FR_END) {
	  if (tp->cnt > 1 && rand() % 100 >= fail)
	    for (j = 0; j < channels; j++)
	      if (self || j != i) write(j, tp->buf, tp->cnt);
	  tp->cnt = 0;
	}
      }
  }
}

