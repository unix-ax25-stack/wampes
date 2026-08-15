/* @(#) $Id: hpux.h,v 1.17 1996/08/12 18:51:17 deyke Exp $ */

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
void *on_read_arg(int fd);
void on_read(int fd, void (*fnc)(void *), void *arg);
void off_read(int fd);
void on_write(int fd, void (*fnc)(void *), void *arg);
void off_write(int fd);
void on_death(int pid, void (*fnc)(void *), void *arg);
void off_death(int pid);
void eihalt(void);

#endif  /* _HPUX_H */
