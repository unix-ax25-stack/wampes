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

/* ONE ENTRY OF A "bind=" LIST, in the spelling the configuration allows:
 *
 *   0.0.0.0      127.0.0.1      [::1]      [fe80::1%en0]      loopback
 *
 * The brackets are the way a name is told apart from an IPv6 literal, and the
 * "%" is a zone index: a link-local address without it names no link.  Both are
 * taken off here and put into the sockaddr - build_sockaddr_host() has no room
 * for a scope, it copies sin6_addr and stops - so a caller does not have to
 * know that a sockaddr_in6 carries more than an address.
 *
 * THE BRACKETS COME OFF FIRST, and that order is the point: the "%" of a zone
 * and the colons of an IPv6 literal live in the same word, so splitting at the
 * first "%" from the left would cut "[fe80::1%en0]" in the middle of the
 * address.  A zone is therefore written inside the brackets, where it cannot be
 * confused with anything.
 *
 * Returns 0 and fills *sa (of length *sl) with the port already in it, or -1
 * with a message on the console saying which word failed and why.  The family
 * is whatever came out - a literal or a name says which one it is, and there is
 * no second word anywhere that says it again.
 */
int sockaddr_from_bindword(const char *word, int port, struct sockaddr_storage *sa,
			   socklen_t *sl);

#endif  /* _SOCKADDR_UTIL_H */
