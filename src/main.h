/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.h,v 1.1 1991/06/01 22:19:42 deyke Exp $ */

#ifndef _MAIN_H
#define _MAIN_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

extern char Badhost[];
extern char *Hostname;
extern char Nospace[];                  /* Generic malloc fail message */

void keyboard __ARGS((void *v));

#endif  /* _MAIN_H */
