/* @(#) $Id: remote_net.h,v 1.4 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _REMOTE_NET_H
#define _REMOTE_NET_H

/* In remote_net.c: */
void remote_net_initialize(void);

/* "start axtcp [<port>]" and "stop axtcp": the loopback listeners for the
 * AX.25 service socket.  Off by default - a TCP port carries no rights.
 */
/* Close a client of the service socket, named by its descriptor. */
void remote_net_drop_client(int fd);

int axtcpstart(int argc, char *argv[], void *p);
int axtcp0(int argc, char *argv[], void *p);

/* "axsock [group <name>|mode <octal>]": the sysop's say over the mode and
 * group of the service socket, which bind() recreates at every start.
 */
int doaxsock(int argc, char *argv[], void *p);

#endif  /* _REMOTE_NET_H */
