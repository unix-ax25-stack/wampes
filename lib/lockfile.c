/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/lockfile.c,v 1.1 1994/02/22 13:23:13 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef ibm032
#include <sys/fcntl.h>
#endif

/*---------------------------------------------------------------------------*/

int lock_fd(int fd, int dont_block)
{

#ifdef ibm032

  return flock(fd, dont_block ? (LOCK_EX | LOCK_NB) : LOCK_EX);

#else

  struct flock flk;

  flk.l_type = F_WRLCK;
  flk.l_whence = SEEK_SET;
  flk.l_start = 0;
  flk.l_len = 0;
  return fcntl(fd, dont_block ? F_SETLK : F_SETLKW, &flk);

#endif

}

/*---------------------------------------------------------------------------*/

int lock_file(const char *filename, int dont_block)
{
  int fd;

  if ((fd = open(filename, O_RDWR | O_CREAT, 0644)) < 0) return (-1);
  if (!lock_fd(fd, dont_block)) return fd;
  close(fd);
  return (-1);
}

