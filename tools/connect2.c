#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect2.c,v 1.6 1993/05/17 13:46:46 deyke Exp $";
#endif

#define _POSIX_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(void)
{

  char buf[2048];
  int fd1, fd2, n;

  if (fork()) exit(0);
  for (n = sysconf(_SC_OPEN_MAX) - 1; n >= 0; n--) close(n);
  chdir("/");
  setsid();
  if ((fd1 = open("/dev/ptyr1", O_RDWR, 0644)) < 0) exit(1);
  if ((fd2 = open("/dev/ptyr2", O_RDWR, 0644)) < 0) exit(1);
  if (fork()) {
    n = fd1;
    fd1 = fd2;
    fd2 = n;
  }
  for (; ; )
    if ((n = read(fd1, buf, sizeof(buf))) > 0) write(fd2, buf, n);
}

