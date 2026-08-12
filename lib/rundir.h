/* @(#) $Id$ */

#ifndef _RUNDIR_H
#define _RUNDIR_H

struct sockaddr;

/* In rundir.c: */

/* The directory holding the sockets everyone may reach.  Every daemon that
 * binds one of them calls this - whoever starts first creates it.
 */
void create_rundir(void);

/* The directory holding the command channel, reachable by its owner only.
 * That is net's alone, so net alone creates it.
 */
void create_admin_rundir(void);

/* bind(), and for a unix socket clean up after a daemon that died without
 * removing its socket.  A socket somebody is still listening on is left
 * alone: bind() then fails with EADDRINUSE, which is the truth.
 */
int bind_socket(int fd, const struct sockaddr *addr, int addrlen);

#endif  /* _RUNDIR_H */
