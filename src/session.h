/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/session.h,v 1.2 1990/08/23 17:33:59 deyke Exp $ */

#ifndef NULLSESSION

extern int mode;
#define CMD_MODE        1       /* Command mode */
#define CONV_MODE       2       /* Converse mode */

/* Session control structure; only one entry is used at a time */
struct session {
	int type;
#define FREE    0
#define TELNET  1
#define FTP     2
#define AX25TNC 3
#define FINGER  4
#define NRSESSION 5

	char *name;     /* Name of remote host */
	union {
		struct ftp *ftp;
		struct telnet *telnet;
#ifdef  AX25
		struct axcb *ax25;
#endif
		struct finger *finger;
#ifdef  NETROM
		struct circuit *netrom;
#endif
	} cb;
	int (*parse)();         /* Where to hand typed input when conversing */
	FILE *record;           /* Receive record file */
	char *rfile;            /* Record file name */
	FILE *upload;           /* Send file */
	char *ufile;            /* Upload file name */
};
#define NULLSESSION     (struct session *)0
extern unsigned nsessions;
extern struct session *sessions;
extern struct session *current;

/* session.c */
int dosession __ARGS((int argc, char *argv [], void *p));
int go __ARGS((int argc,char *argv[],void *p));
int doclose __ARGS((int argc, char *argv [], void *p));
int doreset __ARGS((int argc, char *argv [], void *p));
int dokick __ARGS((int argc, char *argv [], void *p));
struct session *newsession __ARGS((void));
int freesession __ARGS((struct session *s));
int dorecord __ARGS((int argc, char *argv [], void *p));
int doupload __ARGS((int argc, char *argv [], void *p));

extern int16 lport;

#endif  /* NULLSESSION */
