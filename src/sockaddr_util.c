/* Small helpers for working with a sockaddr whose family is not known at
 * compile time.  See sockaddr_util.h.
 */

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <arpa/inet.h>

#include "global.h"
#include "sockaddr_util.h"

/*---------------------------------------------------------------------------*/

int sockaddr_addr_bytes(const struct sockaddr *sa, const unsigned char **pp,
	int *plen)
{
  if (!sa || !pp || !plen) return 0;

  switch (sa->sa_family) {
  case AF_INET:
    *pp = (const unsigned char *) &((const struct sockaddr_in *) sa)->sin_addr;
    *plen = 4;
    return 1;
#if HAS_AF_INET6
  case AF_INET6:
    *pp = (const unsigned char *) &((const struct sockaddr_in6 *) sa)->sin6_addr;
    *plen = 16;
    return 1;
#endif
  default:
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

int sockaddr_addr_eq(const struct sockaddr *a, const struct sockaddr *b)
{
  const unsigned char *pa;
  const unsigned char *pb;
  int la;
  int lb;

  if (!a || !b || a->sa_family != b->sa_family) return 0;
  if (!sockaddr_addr_bytes(a, &pa, &la)) return 0;
  if (!sockaddr_addr_bytes(b, &pb, &lb)) return 0;
  return la == lb && !memcmp(pa, pb, (size_t) la);
}

/*---------------------------------------------------------------------------*/

int sockaddr_port(const struct sockaddr *sa)
{
  if (!sa) return -1;

  switch (sa->sa_family) {
  case AF_INET:
    return ntohs(((const struct sockaddr_in *) sa)->sin_port);
#if HAS_AF_INET6
  case AF_INET6:
    return ntohs(((const struct sockaddr_in6 *) sa)->sin6_port);
#endif
  default:
    break;
  }
  return -1;
}

/*---------------------------------------------------------------------------*/

int sockaddr_set_port(struct sockaddr *sa, int port)
{
  if (!sa || port < 0 || port > 65535) return 0;

  switch (sa->sa_family) {
  case AF_INET:
    ((struct sockaddr_in *) sa)->sin_port = htons((unsigned short) port);
    return 1;
#if HAS_AF_INET6
  case AF_INET6:
    ((struct sockaddr_in6 *) sa)->sin6_port = htons((unsigned short) port);
    return 1;
#endif
  default:
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

socklen_t sockaddr_len(const struct sockaddr *sa)
{
  if (!sa) return 0;

  switch (sa->sa_family) {
  case AF_INET:
    return (socklen_t) sizeof(struct sockaddr_in);
#if HAS_AF_INET6
  case AF_INET6:
    return (socklen_t) sizeof(struct sockaddr_in6);
#endif
  default:
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

char *sockaddr_to_string(const struct sockaddr *sa, char *buf, size_t buflen)
{
  if (!buf || !buflen) return buf;
  buf[0] = 0;
  if (!sa) return buf;

  switch (sa->sa_family) {
  case AF_INET:
    {
      const struct sockaddr_in *si = (const struct sockaddr_in *) sa;
      char t[INET_ADDRSTRLEN];

      if (inet_ntop(AF_INET, &si->sin_addr, t, sizeof(t)))
	snprintf(buf, buflen, "%s", t);
    }
    break;
#if HAS_AF_INET6
  case AF_INET6:
    {
      const struct sockaddr_in6 *s6 = (const struct sockaddr_in6 *) sa;
      char t[INET6_ADDRSTRLEN];

      /* Brackets, the way the configuration writes it */
      if (inet_ntop(AF_INET6, &s6->sin6_addr, t, sizeof(t)))
	snprintf(buf, buflen, "[%s]", t);
    }
    break;
#endif
  default:
    snprintf(buf, buflen, "<af %d>", (int) sa->sa_family);
    break;
  }
  return buf;
}
