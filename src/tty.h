/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tty.h,v 1.2 1991/10/03 11:05:24 deyke Exp $ */

#ifndef _TTY_H
#define _TTY_H

#define NUM_FKEY        10      /* Number of function keys */

/* In main.c: */
extern char Escape;

/* In ttydriv.c: */
extern char *Fkey_table[];
extern char *Fkey_ptr;
int raw __ARGS((void));
int cooked __ARGS((void));
int ttydriv __ARGS((int chr, char **buf));

#endif /* _TTY_H */
