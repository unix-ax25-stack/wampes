/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.h,v 1.15 1993/05/17 13:44:56 deyke Exp $ */

#ifndef _HPUX_H
#define _HPUX_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* In hpux.c: */
pid_t dofork(void);
void ioinit(void);
void iostop(void);
int doshell(int argc, char *argv[], void *p);
void on_read(int fd, void (*fnc)(void *), void *arg);
void off_read(int fd);
void on_write(int fd, void (*fnc)(void *), void *arg);
void off_write(int fd);
void on_death(int pid, void (*fnc)(void *), void *arg);
void off_death(int pid);
void eihalt(void);

#endif  /* _HPUX_H */
