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

extern char  *get_host_from_path();
extern char  *get_user_from_path();
extern void abort_mailjob();
extern void mail_bbs();
extern void mail_return();
extern void mail_smtp();
