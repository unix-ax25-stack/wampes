#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/pty-test.c,v 1.1 1992/09/05 08:14:59 deyke Exp $";
#endif

#define _HPUX_SOURCE

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

static char line[4096];
static int linelen;

/*---------------------------------------------------------------------------*/

void master(void)
{

  char *p;
  int cnt;
  int emask;
  int fd;
  int fmask;
  int i;
  int n;
  int rmask;
  int wmask;
  struct request_info request_info;
  struct timeval timeout;

  fd = open("/dev/ptyr7", O_RDWR | O_NDELAY, 0600);
  if (fd < 0) {
    perror("/dev/ptyr7");
    exit(1);
  }
  i = 1;
  if (ioctl(fd, TIOCTRAP, &i)) {
    perror("TIOCTRAP");
    exit(1);
  }
  fmask = (1 << fd);
  timeout.tv_sec = 0;
  timeout.tv_usec = 0;
  p = line;
  cnt = linelen;
  for (; ; ) {
    rmask = wmask = emask = fmask;
    if (select(fd + 1, &rmask, &wmask, &emask, &timeout) < 1)
      continue;

    if (1 /*rmask & fmask*/ ) {
      char *rp;
      char buf[4096];
      int rn;
      rn = read(fd, rp = buf, sizeof(buf));
/*
      printf("read() returned %d\n", rn);
      for (; rn > 0; rn--) {
	if (*rp == 0x11)
	  printf("^Q received.\n");
	if (*rp == 0x13)
	  printf("^S received.\n");
	rp++;
      }
*/
    }

    if (wmask & fmask) {
      n = cnt / 2 + 1;
      if (n > cnt)
	n = cnt;
      n = write(fd, p, n);
      if (!n)
	printf("write returned 0\n");
      if (n < 0) {
	perror("write");
	exit(1);
      }
      p += n;
      cnt -= n;
      if (!cnt) {
	p = line;
	cnt = linelen;
      }
    }

    if (emask & fmask) {
      if (ioctl(fd, TIOCREQCHECK, &request_info))
	continue;
      ioctl(fd, TIOCREQSET, &request_info);
      if (request_info.request == TIOCCLOSE) {
	printf("Close trapped.\n");
	exit(0);
      }
    }

  }
}

/*---------------------------------------------------------------------------*/

void slave(void)
{

  char buf[4096];
  int fd;
  int n;
  struct termio termio;
  struct timeval timeout;

  fd = open("/dev/ttyr7", O_RDWR, 0666);
  if (fd < 0) {
    perror("/dev/ttyr7");
    exit(1);
  }
  memset(&termio, 0, sizeof(termio));
  termio.c_iflag = ICRNL | IXOFF /* | IXON */ ;
  termio.c_oflag = OPOST | ONLCR | TAB3;
  termio.c_cflag = B1200 | CS8 | CREAD | CLOCAL;
  termio.c_lflag = ISIG | ICANON;
  termio.c_cc[VINTR]  = 127;
  termio.c_cc[VQUIT]  =  28;
  termio.c_cc[VERASE] =   8;
  termio.c_cc[VKILL]  =  24;
  termio.c_cc[VEOF]   =   4;
  if (ioctl(fd, TCSETA, &termio)) {
    perror("TCSETA");
    exit(1);
  }
  if (ioctl(fd, TCFLSH, 2)) {
    perror("TCFLSH");
    exit(1);
  }

  timeout.tv_sec = 0;
  timeout.tv_usec = 300000;
  for (; ; ) {
    n = read(fd, buf, sizeof(buf));
    if (n < 0) {
      perror("read");
      exit(1);
    }
    if (n != linelen)
      printf("linelen = %d n = %d\n", linelen, n);
    else if (strcmp(buf, line))
      printf("buf = '%s'\n", buf);
    else
      putchar('.');
    fflush(stdout);
    select(0, (int *) 0, (int *) 0, (int *) 0, &timeout);
  }
}

/*---------------------------------------------------------------------------*/

static void makeline(char *s, int n)
{
  int c = ' ';
  char *p = s;
  while (n-- > 1) {
    *p++ = c++;
    if (c >= 127)
      c = ' ';
  }
  *p++ = '\n';
  *p = 0;
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  int n = 80;

  if (argc >= 2)
    n = atoi(argv[1]);
  if (n < 1)
    n = 1;
  makeline(line, n);
  linelen = strlen(line);
  printf("linelen = %d\n", linelen);
  signal(SIGCLD, SIG_IGN);
  if (fork()) {
    master();
  } else {
    slave();
  }
  return 0;
}

