/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/finger.h,v 1.8 1996/01/04 19:11:41 deyke Exp $ */

#ifndef _FINGER_H
#define _FINGER_H

/*
 *
 *      Finger support...
 *
 *      Finger header defines.  Written by Michael T. Horne - KA7AXD.
 *      Copyright 1988 by Michael T. Horne, All Rights Reserved.
 *      Permission granted for non-commercial use and copying, provided
 *      that this notice is retained.
 *
 */

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _TCP_H
#include "tcp.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

/* finger protocol control block */
struct finger {
	struct tcb      *tcb;           /* pointer to associated tcb */
	struct session  *session;       /* pointer to finger session */
	char            *user;          /* name of user to finger */
};

/* In finger.c: */
void fingcli_rcv(struct tcb *tcb, int32 cnt);

#endif  /* _FINGER_H */
