/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/telnet.h,v 1.10 1996/08/11 18:16:09 deyke Exp $ */

#ifndef _TELNET_H
#define _TELNET_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _TCP_H
#include "tcp.h"
#endif

#define LINESIZE        256     /* Length of local editing buffer */

/* Telnet command characters */
#define IAC             255     /* Interpret as command */
#define WILL            251
#define WONT            252
#define DO              253
#define DONT            254

/* Telnet options */
#define TN_TRANSMIT_BINARY      0
#define TN_ECHO                 1
#define TN_SUPPRESS_GA          3
#define TN_STATUS               5
#define TN_TIMING_MARK          6
#define NOPTIONS                6

/* Telnet protocol control block */
struct telnet {
	struct tcb *tcb;
	char state;

#define TS_DATA 0       /* Normal data state */
#define TS_IAC  1       /* Received IAC */
#define TS_WILL 2       /* Received IAC-WILL */
#define TS_WONT 3       /* Received IAC-WONT */
#define TS_DO   4       /* Received IAC-DO */
#define TS_DONT 5       /* Received IAC-DONT */

	char local[NOPTIONS+1];   /* Local option settings */
	char remote[NOPTIONS+1];  /* Remote option settings */

	struct session *session;        /* Pointer to session structure */
};
extern int Refuse_echo;
extern int Tn_cr_mode;

/* In telnet.c: */
void rcv_char(struct tcb *tcb, int32 cnt);

#endif  /* _TELNET_H */
