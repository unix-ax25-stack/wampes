/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axclient.h,v 1.2 1991/02/24 20:16:36 deyke Exp $ */

#ifndef _AXCLIENT_H
#define _AXCLIENT_H

#include "global.h"
#include "lapb.h"
#include "session.h"

void axclient_send_upcall __ARGS((struct ax25_cb *cp, int cnt));
void axclient_recv_upcall __ARGS((struct ax25_cb *cp, int cnt));
int doconnect __ARGS((int argc, char *argv[], void *p));

#endif  /* _AXCLIENT_H */
