/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/axclient.h,v 1.1 1990/10/12 19:25:19 deyke Exp $ */

#ifndef AXCLIENT_INCLUDED
#define AXCLIENT_INCLUDED

#include "global.h"
#include "axproto.h"
#include "session.h"

void axclient_send_upcall __ARGS((struct ax25_cb *cp, int cnt));
void axclient_recv_upcall __ARGS((struct ax25_cb *cp, int cnt));
int doconnect __ARGS((int argc, char *argv[], void *p));

#endif  /* AXCLIENT_INCLUDED */

