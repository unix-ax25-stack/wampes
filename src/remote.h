/* @(#) $Id: remote.h,v 1.7 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _REMOTE_H
#define _REMOTE_H

/* Remote reset/restart server definitions */

extern char *Rempass;

/* Commands */
#define SYS__RESET      1
#define SYS__EXIT       2
#define KICK__ME        3

#endif  /* _REMOTE_H */
