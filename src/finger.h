/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/finger.h,v 1.2 1990/08/23 17:32:49 deyke Exp $ */

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

#define FINGER_PORT     79      /* TCP port for `who' (finger) service */
#define FINGNAMELEN     8       /* max len of user name */

/* finger protocol control block */
struct finger {
	struct tcb      *tcb;           /* pointer to associated tcb */
	struct session  *session;       /* pointer to finger session */
	char            *user;          /* name of user to finger */
};
#define NULLFING        (struct finger *) 0

extern char             *fingersuf,     /* see files.c */
			*fingerpath;

