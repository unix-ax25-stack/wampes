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

/* The node's directory - "wampes -d <dir>", TCPDIR by default.  Everything a
 * node keeps while it runs belongs there: see the comment on Node_dir in
 * main.c.  A leaf name is joined with nodedir_path(), not by hand.
 */
const char *nodedir(void);
void nodedir_path(char *buf, size_t len, const char *leaf);

void keyboard(void *arg);

#endif  /* _MAIN_H */
