static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect2.c,v 1.1 1992/06/01 17:26:39 deyke Exp $";

#define _HPUX_SOURCE

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>

int main()
{

  char buf[2048];
  int fd1, fd2, n;

  if (fork()) exit(0);
  for (n = 0; n < _NFILE; n++) close(n);
  chdir("/");
  setpgrp();
  if ((fd1 = open("/dev/ptyr1", O_RDWR, 0666)) < 0) exit(1);
  if ((fd2 = open("/dev/ptyr2", O_RDWR, 0666)) < 0) exit(1);
  if (fork()) {
    for (; ; )
      if ((n = read(fd1, buf, sizeof(buf))) > 0) write(fd2, buf, (unsigned) n);
  } else {
    for (; ; )
      if ((n = read(fd2, buf, sizeof(buf))) > 0) write(fd1, buf, (unsigned) n);
  }
}

