/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tty.h,v 1.1 1991/04/25 18:28:46 deyke Exp $ */

#ifndef _TTY_H
#define _TTY_H

/* In ttydriv.c: */
int raw __ARGS((void));
int cooked __ARGS((void));
int ttydriv __ARGS((int chr, char **buf));

#endif /* _TTY_H */
