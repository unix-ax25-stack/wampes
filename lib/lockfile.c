/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/lockfile.c,v 1.3 1995/11/19 11:54:19 deyke Exp $ */

#include <sys/types.h>

#include <fcntl.h>
#include <unistd.h>

#ifdef ibm032
#include <sys/fcntl.h>
#endif

#include "lockfile.h"
#include "seek.h"

/*---------------------------------------------------------------------------*/

int lock_fd(int fd, int exclusive, int dont_block)
{

#ifdef ibm032

  return flock(fd,
	       (exclusive  ? LOCK_EX : 0) |
	       (dont_block ? LOCK_NB : 0)
      );

#else

  struct flock flk;

  flk.l_type = exclusive ? F_WRLCK : F_RDLCK;
  flk.l_whence = SEEK_SET;
  flk.l_start = 0;
  flk.l_len = 0;
  return fcntl(fd, dont_block ? F_SETLK : F_SETLKW, &flk);

#endif

}

/*---------------------------------------------------------------------------*/

int lock_file(const char *filename, int exclusive, int dont_block)
{
  int fd;

  if ((fd = open(filename, O_RDWR | O_CREAT, 0644)) < 0)
    return -1;
  if (!lock_fd(fd, exclusive, dont_block))
    return fd;
  close(fd);
  return -1;
}
