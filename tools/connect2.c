#ifndef __LINT__
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect2.c,v 1.3 1992/09/05 08:15:18 deyke Exp $";
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main()
{

  char buf[2048];
  int fd1, fd2, n;

  if (fork()) exit(0);
  for (n = 0; n < _NFILE; n++) close(n);
  chdir("/");
  setsid();
  if ((fd1 = open("/dev/ptyr1", O_RDWR, 0666)) < 0) exit(1);
  if ((fd2 = open("/dev/ptyr2", O_RDWR, 0666)) < 0) exit(1);
  if (fork()) {
    n = fd1;
    fd1 = fd2;
    fd2 = n;
  }
  for (; ; )
    if ((n = read(fd1, buf, sizeof(buf))) > 0) write(fd2, buf, n);
}

