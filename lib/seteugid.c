/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/seteugid.c,v 1.1 1994/01/21 11:12:48 deyke Exp $ */

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
    setreuid(-1, uid);
#endif
  } else {
    setuid(0);
    setuid(0);
    setgid(gid);
  }
}

