/* @(#) $Id$ */

/* The run directories and the unix sockets in them.
 *
 * Both daemons need both halves of this.  net created the directories in
 * ioinit() and conversd never did, which only worked because the directories
 * sit under TCPDIR and outlive a reboot; a run directory under /run or
 * /var/run does not, and whichever daemon starts first has to create it.
 *
 * Both also removed a socket in the way before binding, unconditionally.
 * That kept a restart from ever failing, at the price of a second daemon
 * silently taking the name away from the first - which then keeps running,
 * unreachable, holding a socket nobody can find.
 */

#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "configure.h"
#include "rundir.h"

/* mkdir alone would leave the rights to the umask, so chmod follows; that
 * also repairs a directory which is already there with the wrong mode.
 * Failures are ignored on purpose - if the directory cannot be made, the
 * bind() below says so, with the path in the message.
 */

static void fixdir(const char *name, int mode)
{
  mkdir(name, mode);
  chmod(name, mode);
}

/*---------------------------------------------------------------------------*/

void create_rundir(void)
{
  fixdir(TCPDIR, 0755);
  fixdir(TCPDIR "/sockets", 0755);
}

/*---------------------------------------------------------------------------*/

void create_admin_rundir(void)
{
  fixdir(TCPDIR, 0755);
  fixdir(TCPDIR "/.sockets", 0700);
}

/*---------------------------------------------------------------------------*/

/* Is anyone still listening?  1 yes, 0 no, -1 could not tell. */

static int socket_is_live(const struct sockaddr *addr, int addrlen)
{
  int fd;
  int live;

  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0)
    return -1;

  /* Nonblocking, so that a daemon which is alive but not answering cannot
   * hang our startup.  A full backlog still means somebody is there.
   */
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

  if (!connect(fd, addr, addrlen))
    live = 1;
  else
    switch (errno) {
    case ECONNREFUSED:
      live = 0;                 /* bound, but no process has it open */
      break;
    case ENOENT:
      live = 0;                 /* vanished under us; binding will work now */
      break;
    case EAGAIN:
#if defined(EWOULDBLOCK) && EWOULDBLOCK != EAGAIN
    case EWOULDBLOCK:
#endif
    case EINPROGRESS:
      live = 1;                 /* backlog full or still connecting */
      break;
    default:
      live = -1;
      break;
    }

  close(fd);
  return live;
}

/*---------------------------------------------------------------------------*/

int bind_socket(int fd, const struct sockaddr *addr, int addrlen)
{

  const char *path;
  struct stat st;

  if (!bind(fd, addr, addrlen))
    return 0;
  if (addr->sa_family != AF_UNIX || errno != EADDRINUSE)
    return -1;

  /* sun_path, not sa_data: both start at the same offset, but sa_data is
   * declared as 14 bytes while the path may be far longer.
   */
  path = ((const struct sockaddr_un *) addr)->sun_path;

  /* Only ever remove a socket.  Whatever else got left at that path - a
   * regular file, or a symlink pointing somewhere interesting - is not ours
   * to delete, and following it would be exactly the wrong move.
   */
  if (lstat(path, &st) || !S_ISSOCK(st.st_mode)) {
    errno = EADDRINUSE;
    return -1;
  }

  /* Live, or we could not tell: keep our hands off either way.  Failing to
   * start is the smaller harm, and it is visible.
   */
  if (socket_is_live(addr, addrlen)) {
    errno = EADDRINUSE;
    return -1;
  }

  if (remove(path))
    return -1;
  return bind(fd, addr, addrlen);
}
