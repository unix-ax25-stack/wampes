/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.h,v 1.8 1991/04/25 18:26:55 deyke Exp $ */

#ifndef _HPUX_H
#define _HPUX_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

extern int  chkread[];
extern int  actread[];
extern void (*readfnc[]) __ARGS((void *));
extern void *readarg[];

extern int  chkwrite[];
extern int  actwrite[];
extern void (*writefnc[]) __ARGS((void *));
extern void *writearg[];

extern int  chkexcp[];
extern int  actexcp[];
extern void (*excpfnc[]) __ARGS((void *));
extern void *excparg[];

#define setmask(mask, fd) ((mask)[(fd)>>5] |=  (1 << ((fd) & 31)))
#define clrmask(mask, fd) ((mask)[(fd)>>5] &= ~(1 << ((fd) & 31)))
#define maskset(mask, fd) ((mask)[(fd)>>5] &   (1 << ((fd) & 31)))

/* In hpux.c: */
void ioinit __ARGS((void));
void iostop __ARGS((void));
int kbread __ARGS((void));
int system __ARGS((const char *cmdline));
int _system __ARGS((char *cmdline));
void eihalt __ARGS((void));

#endif  /* _HPUX_H */
