/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/remote.h,v 1.2 1990/04/12 17:51:56 deyke Exp $ */

#ifndef SYS_RESET

/* Remote reset/restart server definitions */

#define IPPORT_REMOTE   1234    /* Pulled out of the air */

extern char *Rempass;

/* Commands */
#define SYS_RESET       1
#define SYS_EXIT        2
#define KICK_ME         3

#endif  /* SYS_RESET */

