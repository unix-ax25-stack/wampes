/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.h,v 1.11 1992/09/01 16:52:48 deyke Exp $ */

#ifndef _HPUX_H
#define _HPUX_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* In hpux.c: */
int dofork __ARGS((void));
void ioinit __ARGS((void));
void iostop __ARGS((void));
int system __ARGS((const char *cmdline));
int _system __ARGS((char *cmdline));
int doshell __ARGS((int argc, char *argv [], void *p));
void on_read __ARGS((int fd, void (*fnc )__ARGS ((void *)), void *arg));
void off_read __ARGS((int fd));
void on_write __ARGS((int fd, void (*fnc )__ARGS ((void *)), void *arg));
void off_write __ARGS((int fd));
void on_death __ARGS((int pid, void (*fnc )__ARGS ((void *)), void *arg));
void off_death __ARGS((int pid));
void eihalt __ARGS((void));

#endif  /* _HPUX_H */
