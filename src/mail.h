/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail.h,v 1.4 1990/10/12 19:26:05 deyke Exp $ */

#ifndef MAIL_INCLUDED
#define MAIL_INCLUDED

#include "global.h"

#define CONFFILE   "/tcp/mail.conf"
#define SPOOLDIR   "/usr/spool/uucp"
#define POLLTIME   (60l*5)
#define RETRYTIME  (60l*60)
#define RETURNTIME (60l*60*24*3)
#define MAXJOBS    10

struct mailjob {
  char  to[1024], from[1024], subject[1024];
  char  dfile[80], cfile[80], xfile[80];
  char  return_reason[1024];
  struct mailjob *next;
};

struct mailsys {
  char  *sysname;
  void (*mailer) __ARGS((struct mailsys *sp));
  char  *protocol, *address;
  long  nexttime;
  struct mailjob *jobs;
  struct mailsys *next;
};

/* In mail_bbs.c: */
void mail_bbs __ARGS((struct mailsys *sp));

/* In mail_daemn.c: */
int mail_daemon __ARGS((int argc, char *argv [], void *argp));

/* In mail_retrn.c: */
void mail_return __ARGS((struct mailjob *jp));

/* In mail_smtp.c: */
void mail_smtp __ARGS((struct mailsys *sp));

/* In mail_subr.c: */
char *get_user_from_path __ARGS((char *path));
char *get_host_from_path __ARGS((char *path));
void free_mailjobs __ARGS((struct mailsys *sp));

#endif  /* MAIL_INCLUDED */

