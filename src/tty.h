/* @(#) $Id: tty.h,v 1.6 1999/02/01 22:24:25 deyke Exp $ */

#ifndef _TTY_H
#define _TTY_H

#define NUM_FKEY        12      /* Number of function keys */

/* In main.c: */
extern char Escape;

/* In ttydriv.c: */
extern char *Fkey_table[];
extern char *Fkey_ptr;
int raw(void);
int cooked(void);
int ttydriv(int chr, char **buf);

#endif /* _TTY_H */
