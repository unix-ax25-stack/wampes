/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail.h,v 1.8 1991/04/12 18:35:10 deyke Exp $ */

#ifndef _MAIL_H
#define _MAIL_H

#include "global.h"

#define CONFFILE   "/tcp/mail.conf"
#define SPOOLDIR   "/usr/spool/uucp"
#define RETRYTIME  (60L*60)
#define RETURNTIME (60L*60*24*3)
#define MAXJOBS    10

struct mailers;

struct mailsys {
  char  *sysname;
  struct mailers *mailer;
  char  *protocol, *address;
  int  state;
#define MS_NEVER        0
#define MS_TRYING       1
#define MS_TALKING      2
#define MS_SUCCESS      3
#define MS_FAILURE      4
  long  nexttime;
  struct mailjob *jobs;
  struct mailsys *next;
};

struct mailers {
  char  *name;
  void (*func) __ARGS((struct mailsys *sp));
};

struct mailjob {
  char  to[1024], from[1024], subject[1024];
  char  dfile[80], cfile[80], xfile[80];
  char  return_reason[1024];
  struct mailjob *next;
};

/* In mail_bbs.c: */
void mail_bbs __ARGS((struct mailsys *sp));

/* In mail_retrn.c: */
void mail_return __ARGS((struct mailjob *jp));

/* In mail_smtp.c: */
void mail_smtp __ARGS((struct mailsys *sp));

/* In mail_subr.c: */
char *get_user_from_path __ARGS((char *path));
char *get_host_from_path __ARGS((char *path));
void free_mailjobs __ARGS((struct mailsys *sp));

#endif  /* _MAIL_H */
