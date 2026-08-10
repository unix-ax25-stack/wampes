/* @(#) $Id: main.h,v 1.8 1996/08/12 18:51:17 deyke Exp $ */

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
extern long StartTime;                  /* time that NOS was started */

void keyboard(void *arg);

#endif  /* _MAIN_H */
