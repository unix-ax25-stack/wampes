/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/seteugid.c,v 1.2 1994/04/09 15:30:51 deyke Exp $ */

#include <sys/types.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/id.h>
#endif

#include "seteugid.h"

void seteugid(int uid, int gid)
{
  if (uid) {
    setgid(gid);
#if defined __hpux
    setresuid(uid, uid, -1);
#elif defined sun
    seteuid(uid);
#elif defined _AIX
    setuidx(ID_REAL | ID_EFFECTIVE, uid);
#else
    setreuid(0, 0);
    setreuid(0, uid);
#endif
  } else {
    setuid(0);
    setuid(0);
    setgid(gid);
  }
}

