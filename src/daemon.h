/* @(#) $Id: daemon.h,v 1.7 1999/02/11 19:26:49 deyke Exp $ */

#ifndef _DAEMON_H
#define _DAEMON_H

struct daemon {
	char *name;
	unsigned stksize;
	void (*fp)(int,void *,void *);
};
extern struct daemon Daemons[];

/* In main.c: */
void keyboard(int,void*,void*);
void network(int,void *,void *);

/* In kernel.c: */
void killer(int,void*,void*);

/* In timer.c: */
void timerproc(int,void*,void*);

#endif  /* _DAEMON_H */

