/* Small helpers for working with a sockaddr whose family is not known at
 * compile time.  Only what the encapsulations need: compare two addresses,
 * read and set the port, and print one.
 *
 * Modelled on sockaddr_util.c/.h in conversd-saupp, trimmed to this use.
 */

#ifndef _SOCKADDR_UTIL_H
#define _SOCKADDR_UTIL_H

#include <sys/types.h>
#include <sys/socket.h>

#include "buildsaddr.h"         /* settles HAS_AF_INET6 vs NO_AF_INET6 */

/* Enough for an IPv6 address in brackets plus a port */
#define SOCKADDR_STRLEN 64

/* Point *pp at the raw address bytes of *sa and store their length in *plen
 * (4 for AF_INET, 16 for AF_INET6).  Returns 1 on success, 0 for families
 * without a comparable address.
 */
int sockaddr_addr_bytes(const struct sockaddr *sa, const unsigned char **pp,
	int *plen);

/* Do the two describe the same host?  The port is not looked at. */
int sockaddr_addr_eq(const struct sockaddr *a, const struct sockaddr *b);

/* Port in host byte order, or -1 for families that have none. */
int sockaddr_port(const struct sockaddr *sa);

/* Set the port, given in host byte order.  1 on success, 0 if the family has
 * no port.
 */
int sockaddr_set_port(struct sockaddr *sa, int port);

/* Length of the sockaddr for this family, or 0 if unknown. */
socklen_t sockaddr_len(const struct sockaddr *sa);

/* Printable form: "44.130.1.2" or "[2001:db8::1]".  Never fails, always NUL
 * terminates.  buflen should be at least SOCKADDR_STRLEN.
 */
char *sockaddr_to_string(const struct sockaddr *sa, char *buf, size_t buflen);

#endif  /* _SOCKADDR_UTIL_H */
