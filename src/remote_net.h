/* @(#) $Id: remote_net.h,v 1.4 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _REMOTE_NET_H
#define _REMOTE_NET_H

/* In remote_net.c: */
void remote_net_initialize(void);

/* Close a client of the service socket, named by its descriptor. */
/* One received datagram to a client: TNC2 header, byte count, then exactly
 * that many raw bytes.  See remote_net_send_frame().
 */
int remote_net_send_frame(int fd,const char *hdr,struct mbuf *bp);

void remote_net_drop_client(int fd);


/* "axsock [group <name>|mode <octal>]": the sysop's say over the mode and
 * group of the service socket, which bind() recreates at every start.
 */
int doaxsock(int argc, char *argv[], void *p);

#endif  /* _REMOTE_NET_H */
