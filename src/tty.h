/* @(#) $Id: tty.h,v 1.5 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _TTY_H
#define _TTY_H

#define NUM_FKEY        10      /* Number of function keys */

/* In main.c: */
extern char Escape;

/* In ttydriv.c: */
extern char *Fkey_table[];
extern char *Fkey_ptr;
int raw(void);
int cooked(void);
int ttydriv(int chr, char **buf);

#endif /* _TTY_H */
