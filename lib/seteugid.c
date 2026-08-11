/* @(#) $Id: seteugid.c,v 1.7 1996/08/12 18:53:41 deyke Exp $ */

#include <sys/types.h>
#include <grp.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/id.h>
#endif

#include "seteugid.h"

void seteugid(int uid, int gid)
{
  setuid(0);
  if (uid) {
    setgid(gid);
#if defined __hpux
    setresuid(uid, uid, 0);
#elif defined sun
    seteuid(uid);
#elif defined _AIX
    setuidx(ID_REAL | ID_EFFECTIVE, uid);
#else
    setreuid(0, uid);
#endif
  } else {
    setuid(0);
    setgid(gid);
  }
}

/*---------------------------------------------------------------------------*/

int dropprivileges(const char *name, int uid, int gid)
{
  if ((int) getuid() == uid && (int) geteuid() == uid)
    return 0;
  if (setuid(0))
    return -1;
  if (name && initgroups(name, gid))
    return -1;
  if (setgid(gid))
    return -1;
  if (setuid(uid))
    return -1;
  return 0;
}
