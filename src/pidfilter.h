/* Which protocols may cross a port, and in which direction - see pidfilter.c. */

#ifndef _PIDFILTER_H
#define _PIDFILTER_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#define PF_IN           0       /* frames arriving for us */
#define PF_OUT          1       /* frames we send */
#define PF_NDIR         2

/* 256 protocol ids, one bit each, per direction. */
#define PF_WORDS        8

/* The help, here so that the Ifcmds entry in iface.c - which is what "ifconfig
 * <iface> pid ?" prints - and the command's own error messages say the same
 * thing.
 */
extern char Pid_filter_usage[];

int pid_blocked(const struct iface *ifp, int dir, int pid);
/* Fuer "ifconfig <iface> verbose": eine Zeile, aber nur wenn etwas gesetzt
 * ist.  Ein Gatter, das vor dem Parsen verwirft, darf nicht ausgerechnet
 * dort unsichtbar sein, wo man nachsieht, was ein Port tut (Thomas).
 */
void pid_show_verbose(const struct iface *ifp);

/* The mapping, and the only one in the node.  pid_number() takes a name or a
 * number and answers -1 for neither; pid_name() answers a name or writes the
 * number into buf.  Every place that reads a "pid=" or shows a protocol id
 * goes through these, or they drift apart - which they had.
 */
int pid_number(const char *word);
const char *pid_name(int pid, char *buf, size_t buflen);

int pid_info(int argc, char *argv[], void *p);
int if_pid_filter(int argc, char *argv[], void *p);

#endif /* _PIDFILTER_H */
