/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/main.h,v 1.2 1991/10/11 18:56:30 deyke Exp $ */

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

void keyboard __ARGS((int,void*,void*));

#endif  /* _MAIN_H */
