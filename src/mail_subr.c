#include <string.h>

#include "global.h"
#include "hpux.h"
#include "mail.h"

/*---------------------------------------------------------------------------*/

char  *get_user_from_path(path)
char  *path;
{
  register char  *cp;

  return (cp = strrchr(path, '!')) ? cp + 1 : path;
}

/*---------------------------------------------------------------------------*/

char  *get_host_from_path(path)
char  *path;
{

  register char  *cp;
  static char  tmp[1024];

  strcpy(tmp, path);
  if (!(cp = strrchr(tmp, '!'))) return "";
  *cp = '\0';
  return (cp = strrchr(tmp, '!')) ? cp + 1 : tmp;
}

/*---------------------------------------------------------------------------*/

void abort_mailjob(sp)
struct mailsys *sp;
{
  struct mailjob *jp;

  while (jp = sp->nextjob) {
    sp->nextjob = jp->nextjob;
    free((char *) jp);
  }
  sp->nexttime = currtime + RETRYTIME;
}

