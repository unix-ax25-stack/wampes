/* @(#) $Id: buildsaddr.h,v 1.5 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _BUILDSADDR_H
#define _BUILDSADDR_H

#include <stddef.h>

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

/* Split "host", "host:service", "[host]" or "[host]:service" for a caller that
 * has to keep the two parts apart instead of building a sockaddr from them -
 * the encapsulations store the peer as text and dial it again on every retry.
 * The brackets mean IPv6 and are stripped, since getaddrinfo() does not take
 * them.  *family comes back AF_INET6 when they were there and AF_UNSPEC
 * otherwise, which is what the caller hands the resolver.  serv is "" when the
 * spelling carried no service.  Returns 0, or -1 for a spelling that cannot be
 * split unambiguously - a bare "2001:db8::1:3600" among them, which wants the
 * brackets.  See the description in buildsaddr.c.
 */
int build_hostport(const char *arg, char *host, size_t hostsz, char *serv,
    size_t servsz, int *family);

#endif  /* _BUILDSADDR_H */
