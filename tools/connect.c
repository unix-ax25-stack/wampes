#ifndef __LINT__
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect.c,v 1.2 1992/09/05 08:15:18 deyke Exp $";
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

extern char *optarg;
extern int optind;

#define MAXCHANNELS     9       /* Max number of connections */

#define FR_END          0300    /* Frame End */
#define FR_ESC          0333    /* Frame Escape */
#define T_FR_END        0334    /* Transposed frame end */
#define T_FR_ESC        0335    /* Transposed frame escape */

#define uchar(c)        ((c) & 0xff)

int main(argc, argv)
int argc;
char **argv;
{

  static struct tab {
    char buf[4096];
    unsigned int cnt;
  } tab[MAXCHANNELS];

  static struct timeval timeout = {
    3600, 0
  };

  char tmp[1024];
  int ch;
  int channels = 2;
  int errflag = 0;
  int fail = 0;
  int fmask = 0;
  int i;
  int self = 0;
  struct tab *tp;

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
    puts("usage: connect [-c channels] [-f failures] [-s]");
    exit(1);
  }

  if (fork()) exit(0);
  for (i = 0; i < _NFILE; i++) close(i);
  chdir("/");
  setsid();

  for (i = 0; i < channels; i++) {
    sprintf(tmp, "/dev/ptyr%d", i + 1);
    if (open(tmp, O_RDWR, 0666) != i) exit(1);
    fmask |= (1 << i);
  }

  for (; ; ) {

    char c;
    int j;
    int readmask = fmask;

    if (!select(channels, &readmask, (int *) 0, (int *) 0, &timeout)) exit(0);
    for (i = 0; i < channels; i++)
      if (readmask & (1 << i)) {
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

