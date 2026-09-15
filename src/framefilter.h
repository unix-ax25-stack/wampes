/* Which AX.25 frame classes may cross a port, and in which direction - see
 * framefilter.c.
 */

#ifndef _FRAMEFILTER_H
#define _FRAMEFILTER_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* The two classes, one bit each.  FRF_CONN is not "I frames": it is
 * everything that is not UI - SABM/SABME, I, S, UA, DM, FRMR, DISC - the
 * whole connected world, because that is where the frame type splits the
 * incoming path (ax_recv) and because a UI-only port has no business with
 * any of it.
 */
#define FRF_UI    0x01    /* connectionless UI frames */
#define FRF_CONN  0x02    /* everything else: the connected-mode world */

/* The help, here so that the Ifcmds entry in iface.c - which is what
 * "ifconfig <iface> frame ?" prints - and the command's own error messages
 * say the same thing.
 */
extern char Frame_filter_usage[];

/* Is this class of frame blocked in this direction on this port?  ifp may be
 * null - the loopback and the netrom pseudo interface have none - and then
 * nothing is blocked.
 */
int frame_blocks(const struct iface *ifp, int dir, int class);

/* Fuer "ifconfig <iface> verbose": eine Zeile, aber nur wenn etwas gesetzt
 * ist - dasselbe, was pid_show_verbose() fuer die Protokolle tut
 * (Thomas).
 */
void frame_show_verbose(const struct iface *ifp);

int if_frame_filter(int argc, char *argv[], void *p);

#endif /* _FRAMEFILTER_H */