/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_subr.c,v 1.4 1990/10/26 19:20:48 deyke Exp $ */

#include <stdlib.h>
#include <string.h>

#include "mail.h"

/*---------------------------------------------------------------------------*/

char  *get_user_from_path(path)
char  *path;
{
  char  *cp;

  return (cp = strrchr(path, '!')) ? cp + 1 : path;
}

/*---------------------------------------------------------------------------*/

char  *get_host_from_path(path)
char  *path;
{

  char  *cp;
  static char  tmp[1024];

  strcpy(tmp, path);
  if (!(cp = strrchr(tmp, '!'))) return "";
  *cp = '\0';
  return (cp = strrchr(tmp, '!')) ? cp + 1 : tmp;
}

/*---------------------------------------------------------------------------*/

void free_mailjobs(sp)
struct mailsys *sp;
{
  struct mailjob *jp;

  while (jp = sp->jobs) {
    sp->jobs = jp->next;
    free(jp);
  }
}

