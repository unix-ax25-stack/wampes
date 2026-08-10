/* @(#) $Id: buildsaddr.h,v 1.5 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _BUILDSADDR_H
#define _BUILDSADDR_H

#include "configure.h"

/* -DNO_AF_INET6 forces IPv6 off whatever configure found.  Kept here rather
 * than in buildsaddr.c so that everyone who deals with the addresses this
 * builds sees the same answer.
 */
#ifdef NO_AF_INET6
#undef HAS_AF_INET6
#define HAS_AF_INET6 0
#endif

/* In buildsaddr.c: */
struct sockaddr *build_sockaddr(const char *name, int *addrlen);

/* Same, but for a bare address with the port supplied separately - the
 * encapsulations take the peer from the routing command and the port from
 * the interface.  A bare IPv6 literal is accepted here; brackets still mean
 * "resolve this name as IPv6".
 */
struct sockaddr *build_sockaddr_host(const char *name, int port, int *addrlen);

#endif  /* _BUILDSADDR_H */
