/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/daemon.h,v 1.1 1991/10/11 18:57:18 deyke Exp $ */

#ifndef _DAEMON_H
#define _DAEMON_H

struct daemon {
	char *name;
	unsigned stksize;
	void (*fp) __ARGS((int,void *,void *));
};
#define NULLDAEMON ((struct daemon *)0)
extern struct daemon Daemons[];

/* In main.c: */
void keyboard __ARGS((int,void*,void*));
void network __ARGS((int,void *,void *));
void display __ARGS((int,void *,void *));

/* In kernel.c: */
void killer __ARGS((int,void*,void*));

/* In timer.c: */
void timerproc __ARGS((int,void*,void*));

#endif  /* _DAEMON_H */

