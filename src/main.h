/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.h,v 1.5 1995/12/20 09:46:50 deyke Exp $ */

#ifndef _MAIN_H
#define _MAIN_H

#ifndef _PROC_H
#include "proc.h"
#endif

extern char Badhost[];
extern char *Hostname;
extern char Nospace[];                  /* Generic malloc fail message */

extern int main_exit;                   /* from main program (flag) */
extern int stop_repeat;

void keyboard(int,void *,void *);

#endif  /* _MAIN_H */
