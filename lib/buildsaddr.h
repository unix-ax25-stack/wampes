/* @(#) $Id: buildsaddr.h,v 1.5 1996/08/12 18:53:41 deyke Exp $ */

#ifndef _BUILDSADDR_H
#define _BUILDSADDR_H

/* In buildsaddr.c: */
struct sockaddr *build_sockaddr(const char *name, int *addrlen);

#endif  /* _BUILDSADDR_H */
