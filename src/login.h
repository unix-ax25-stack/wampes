/* login server control block */

struct login_cb {
  int  pty;                     /* pty file descriptor */
  char  buffer[256];            /* pty read buffer */
  char  *bufptr;                /* pty read buffer index */
  int  bufcnt;                  /* pty read buffer count */
  int  num;                     /* pty number */
  char  id[4];                  /* pty id (last 2 chars) */
  struct timer poll_timer;      /* pty poll timer */
  int  linelen;                 /* counter for automatic line break */
  int  pid;                     /* process id of login process */
  FILE * fplog;                 /* log file pointer */
  int  state;                   /* telnet state */
#define NOPTIONS 6              /* number of telnet options */
  char  option[NOPTIONS+1];     /* telnet options */
};

extern struct passwd *getpasswdentry();
extern struct login_cb *login_open();
extern void login_close();
extern int  login_read();
extern void login_write();
extern int  login_dead();
