/* DAMA slave - see dama.c. */

#ifndef _DAMA_H
#define _DAMA_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _LAPB_H
#include "lapb.h"
#endif

/* Roles.  Only OFF and SLAVE are built; the master needs what a KISS line
 * does not give it, see TODO.txt.
 */
#define DAMA_OFF        0
#define DAMA_SLAVE      1

void dama_heard_frame(struct iface *ifp);
int  dama_holds(struct ax25_cb *axp);
void dama_poll_begin(struct ax25_cb *axp,int ispoll);
void dama_poll_end(struct ax25_cb *axp);
void dama_wait(struct ax25_cb *axp);
int  ifdama(int argc, char *argv[], void *p);
int  ifdamatimeout(int argc, char *argv[], void *p);
void dama_show(struct iface *ifp);

#endif /* _DAMA_H */
