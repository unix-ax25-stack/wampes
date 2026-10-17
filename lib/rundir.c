/* rundir.c
 * Copyright (C) 2026 Thomas Osterried <dl9sau@darc.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 */

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

/* mkdir alone would leave the rights to the umask, so a chmod has to follow -
 * but only where we created the directory.  One that is already there carries
 * a decision somebody made, and overwriting it without a word is exactly what
 * the startup check in net refuses to do; it reports instead.  Failures are
 * ignored on purpose - if the directory cannot be made, the bind() below says
 * so, with the path in the message.
 */

static void makedir(const char *name, int mode)
{
  if (!mkdir(name, mode))
    chmod(name, mode);
}

/* .sockets stays 0700.  It used to be forced to 0700 at every start because
 * nothing set a mode on the command socket itself and those 0700 were its
 * whole protection.  The socket says 0600 for itself now, so 0700 is the same
 * decision made twice - which is the point, since the command channel runs
 * "!" as root.  An admin who wants 0750 for a group of sysops may have it and
 * keep it; one who opens it wide is told so at startup.
 */

/*---------------------------------------------------------------------------*/

void create_rundir(void)
{
  makedir(TCPDIR, 0755);
  /* 0755, because the gate is the socket now and not the directory.  The two
   * sockets in sockets/ state it themselves - 0666 for the service, 0660 with
   * the directory's group for the monitor (set_service_rights() and
   * inherit_dir_group() in src/remote_net.c) - and "axsock" moves either one
   * from net.rc.  The directory decides only who may walk the path and who
   * may create a socket there, and both belong to the sysop.  It used to be
   * 0750 "because it is the gate", which decided nothing: on BSD a new
   * directory takes the group of its parent, so it came up root:staff and
   * admitted every local account, while on Linux it came up root:root and
   * admitted nobody - which is what kept an operator running "chmod 755".
   * See doc/PERMISSIONS.md.
   */
  makedir(TCPDIR "/sockets", 0755);
}

/*---------------------------------------------------------------------------*/

void create_admin_rundir(void)
{
  makedir(TCPDIR, 0755);
  /* 0700: the command channel runs "!" as root, and the socket inside is 0600
   * of its own.  The directory says the same thing and keeps saying it even
   * if something later loosens the socket; it is not comfort either way.
   */
  makedir(TCPDIR "/.sockets", 0700);
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
