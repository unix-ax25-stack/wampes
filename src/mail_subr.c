/* @(#) $Id: mail_subr.c,v 1.11 2002/09/20 15:07:49 dl9sau Exp $ */

#include "timer.h"
#include "mail.h"

/*---------------------------------------------------------------------------*/

char *get_user_from_path(char *path)
{
  char *cp, *cp2;
  static char tmp[1024];

  if ((cp = strrchr(path, '!')))
    cp++;
  else
    cp = path;
  if (!(cp2 = strchr(cp, '@')))
    cp++;
  else {
    strncpy(tmp, cp, cp2-cp);
    tmp[cp2-cp] = 0;
    cp = tmp;
  }
  return (*cp ? cp : "MAILER-DAEMON");
}

/*---------------------------------------------------------------------------*/

char *get_host_from_path(char *path)
{

  char *cp, *cp2;
  static char tmp[1024];

  if ((cp = strrchr(path, '@'))) {
    cp++;
    if ((cp2 = strchr(cp, '!'))) {
      strncpy(tmp, cp, cp2-cp);
      tmp[cp2-cp] = 0;
      cp = tmp;
    }
  } else {
    strcpy(tmp, path);
    if ((cp = strrchr(tmp, '!')))
      *cp = '\0';
    if ((cp = strrchr(tmp, '!')))
      cp++;
    else
      cp = tmp;
  }
  return (*cp ? cp : Hostname);
}

/*---------------------------------------------------------------------------*/

void mailer_failed(struct mailsys *sp)
{
  struct mailjob *jp;

  while ((jp = sp->jobs)) {
    sp->jobs = jp->next;
    free(jp);
  }
  sp->state = MS_FAILURE;
  sp->nexttime = secclock() + RETRYTIME;
}
