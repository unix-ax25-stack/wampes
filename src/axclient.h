/* @(#) $Id: axclient.h,v 1.6 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _AXCLIENT_H
#define _AXCLIENT_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _LAPB_H
#include "lapb.h"
#endif

#ifndef _SESSION_H
#include "session.h"
#endif

void axclient_send_upcall(struct ax25_cb *cp, int cnt);
void axclient_recv_upcall(struct ax25_cb *cp, int cnt);
int doconnect(int argc, char *argv[], void *p);

#endif  /* _AXCLIENT_H */
