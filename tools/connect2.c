#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/connect2.c,v 1.8 1994/06/16 13:14:39 deyke Exp $";
#endif

#ifndef linux
#define FD_SETSIZE      64
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

