/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail.h,v 1.3 1990/09/11 13:45:50 deyke Exp $ */

#ifndef MAIL_INCLUDED
#define MAIL_INCLUDED

#define CONFFILE   "/tcp/mail.conf"
#define SPOOLDIR   "/usr/spool/uucp"
#define POLLTIME   (60l*5)
#define RETRYTIME  (60l*60)
#define RETURNTIME (60l*60*24*3)

struct mailjob {
  char  to[1024], from[1024], subject[1024];
  char  dfile[80], cfile[80], xfile[80];
  char  return_reason[1024];
  struct mailjob *nextjob;
};

struct mailsys {
  char  *sysname;
  void (*mailer)();
  char  *protocol, *address;
  long  nexttime;
  struct mailjob *nextjob, *lastjob;
  struct mailsys *nextsys;
};

/* mail_bbs.c */
void mail_bbs __ARGS((struct mailsys *sp));

/* mail_daemn.c */
int mail_daemon __ARGS((int argc,char *argv[],void *p));

/* mail_retrn.c */
void mail_return __ARGS((struct mailjob *jp));

/* mail_smtp.c */
void mail_smtp __ARGS((struct mailsys *sp));

/* mail_subr.c */
char *get_user_from_path __ARGS((char *path));
char *get_host_from_path __ARGS((char *path));
void abort_mailjob __ARGS((struct mailsys *sp));

#endif  /* MAIL_INCLUDED */

