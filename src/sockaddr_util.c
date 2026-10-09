/* Small helpers for working with a sockaddr whose family is not known at
 * compile time.  See sockaddr_util.h.
 */

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>

#include <arpa/inet.h>
#include <net/if.h>			/* if_nametoindex(), for a zone index */

#include "global.h"
#include "sockaddr_util.h"
#include "../lib/buildsaddr.h"

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

      /* Brackets, the way the configuration writes it - and the zone with it,
       * so that a link-local address comes back out of "ifconfig verbose" the
       * way it went in.  Without the number a reader cannot tell which of two
       * links the address is on, and this word is the only thing on the node
       * that says it. */
      if (inet_ntop(AF_INET6, &s6->sin6_addr, t, sizeof(t)))
	snprintf(buf, buflen, s6->sin6_scope_id ? "[%s%%%u]" : "[%s]", t,
		 (unsigned int) s6->sin6_scope_id);
    }
    break;
#endif
  default:
    snprintf(buf, buflen, "<af %d>", (int) sa->sa_family);
    break;
  }
  return buf;
}

/*---------------------------------------------------------------------------*/

/* ONE ENTRY OF A "bind=" LIST.  See sockaddr_util.h for the spelling and for
 * why the brackets come off before the zone is looked for.
 *
 * THE SCOPE IS NOT LOOKED UP HERE, only carried: if_nametoindex() needs
 * <net/if.h>, and a name that means nothing to the kernel is a mistake of the
 * sysop's that the caller is better placed to report against the command he
 * wrote.  sin6_scope_id comes back 0 here, which the kernel reads as "no
 * scope", and the caller sets it when it has a number to set.
 */
int sockaddr_from_bindword(const char *word, int port, struct sockaddr_storage *sa,
			   socklen_t *sl)
{
  char buf[SOCKADDR_STRLEN * 4];
  char *addrpart;
  char *pct = NULL;
  struct sockaddr *sp;
#if HAS_AF_INET6
  uint32_t scope;
#endif
  int len;

  if (sa == NULL || sl == NULL) return -1;
  *sl = 0;
  if (word == NULL || *word == '\0') {
    printf("an address is expected\n");
    return -1;
  }
  if (strlen(word) >= sizeof(buf)) {
    printf("\"%s\" is too long for an address\n", word);
    return -1;
  }
  memcpy(buf, word, strlen(word) + 1);
  addrpart = buf;
  if (buf[0] == '[') {
    char *close = strchr(buf, ']');

    if (close == NULL || close[1] != '\0') {
      printf("\"%s\": an IPv6 literal is written in brackets, and nothing "
	     "follows the closing one\n", word);
      return -1;
    }
    *close = '\0';
    addrpart = buf + 1;
  }
  if ((pct = strchr(addrpart, '%')) != NULL)
    *pct++ = '\0';

  if (!*addrpart) {
    printf("\"%s\" is not an address\n", word);
    return -1;
  }

  if (pct == NULL) {
    /* Without a zone the word goes as it was written, brackets and all: the
     * brackets are what build_sockaddr_host() reads to know that a bare
     * "::1" is a literal and not a name. */
    if (!(sp = build_sockaddr_host(word, port, &len)) || len <= 0) {
      printf("cannot look up \"%s\"\n", word);
      return -1;
    }
  } else {
#if HAS_AF_INET6
    char bare[SOCKADDR_STRLEN * 4];
    char wrapped[SOCKADDR_STRLEN * 4 + 3];

    /* A ZONE WITHOUT AN ADDRESS IS NOT A ZONE, and a zone on an IPv4 address is
     * not a thing: both would be a word with a "%" in it that means something
     * else entirely. */
    if (!*pct || strchr(pct, '%')) {
      printf("\"%s\": one zone, and it has a name after the %s\n", word, "%");
      return -1;
    }
    /* The address without its zone goes in brackets, so that a literal stays a
     * literal and the resolver is asked for IPv6 rather than for either. */
    if (strlen(addrpart) + 3 >= sizeof(wrapped)) {
      printf("\"%s\" is too long for an address\n", word);
      return -1;
    }
    snprintf(bare, sizeof(bare), "%s", addrpart);
    snprintf(wrapped, sizeof(wrapped), "[%s]", bare);
    if (!(sp = build_sockaddr_host(wrapped, port, &len)) || len <= 0 ||
	sp->sa_family != AF_INET6) {
      printf("\"%s\": only an IPv6 address takes a zone, and \"%s\" is not one\n",
	     word, bare);
      return -1;
    }
    /* AND THE ZONE IS LOOKED UP HERE, not by the caller: build_sockaddr_host()
     * has no room for a scope - it copies sin6_addr and stops - and a caller
     * that forgot to set sin6_scope_id would bind a link-local address on no
     * link at all, which answers nowhere and says nothing.  A name the kernel
     * does not know is refused rather than taken for zero, and it is the one
     * message here that quotes the name the sysop wrote (Thomas).
     */
    if ((scope = if_nametoindex(pct)) == 0) {
      printf("\"%s\" is not an interface of this node\n", pct);
      return -1;
    }
    ((struct sockaddr_in6 *) sp)->sin6_scope_id = scope;
#else
    printf("this build has no IPv6 support, so \"%s\" has no address\n", word);
    return -1;
#endif
  }

  memcpy(sa, sp, (size_t) len);
  *sl = (socklen_t) len;
  return 0;
}
