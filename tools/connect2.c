#ifndef __lint
static const char rcsid[] = "@(#) $Id: connect2.c,v 1.11 1999/01/22 21:21:39 deyke Exp $";
#endif

#include <sys/types.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

int main(void)
{

  char buf[2048];
  int fd1, fd2, n;

  if (fork()) exit(0);
  for (n = FD_SETSIZE - 1; n >= 0; n--) close(n);
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

