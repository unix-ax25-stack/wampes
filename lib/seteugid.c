/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/seteugid.c,v 1.5 1995/11/19 11:54:19 deyke Exp $ */

#include <sys/types.h>
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
