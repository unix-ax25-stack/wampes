/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/daemon.h,v 1.3 1993/05/17 13:44:50 deyke Exp $ */

#ifndef _DAEMON_H
#define _DAEMON_H

struct daemon {
	char *name;
	unsigned stksize;
	void (*fp)(int,void *,void *);
};
#define NULLDAEMON ((struct daemon *)0)
extern struct daemon Daemons[];

/* In alloc.c: */
void gcollect(int,void*,void*);

/* In main.c: */
void keyboard(int,void*,void*);
void network(int,void *,void *);
void display(int,void *,void *);

/* In kernel.c: */
void killer(int,void*,void*);

/* In timer.c: */
void timerproc(int,void*,void*);

#endif  /* _DAEMON_H */

