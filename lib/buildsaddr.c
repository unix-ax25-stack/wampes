/* @(#) $Id: buildsaddr.c,v 1.17 1996/08/12 18:53:41 deyke Exp $ */

/* Turn "host:service" into a struct sockaddr.
 *
 *   unix:/tmp/foo          absolute path
 *   unix:netcmd            relative, taken under TCPDIR "/sockets/"
 *   *:4713                 any IPv4 address
 *   localhost:3600
 *   db0sao.ampr.org:ttylink
 *   [2001:db8::1]:3600     IPv6 literal - the brackets keep its colons apart
 *                          from the one in front of the service
 *   [db0sao.ampr.org]:3600 resolve that name as IPv6
 *
 * Without brackets an A record still wins over AAAA, so every existing
 * configuration resolves exactly as it did before, while a peer that only
 * publishes AAAA becomes reachable without touching the configuration.
 */

#include <sys/types.h>

#include <ctype.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <arpa/inet.h>

#include "configure.h"
#include "buildsaddr.h"

/* -DNO_AF_INET6 forces IPv6 off whatever configure found */
#ifdef NO_AF_INET6
#undef HAS_AF_INET6
#define HAS_AF_INET6 0
#endif

static union {
  struct sockaddr sa;
  struct sockaddr_in si;
#if HAS_AF_INET6
  struct sockaddr_in6 si6;
#endif
  struct sockaddr_un su;
} addr;

/*---------------------------------------------------------------------------*/

/* Resolve a host name or a literal to one address.
 *
 * want_family is AF_INET6 when the caller wrote brackets, AF_UNSPEC
 * otherwise.  Stores the raw address (4 or 16 bytes) in dst and the family in
 * *got_family.  Returns 0 on success, -1 on failure.
 */

static int resolve_addr(const char *host, int want_family, unsigned char *dst,
	int *got_family)
{

#if HAS_AF_INET6

  struct addrinfo hints;
  struct addrinfo *res = 0;
  struct addrinfo *ai;
  struct addrinfo *chosen = 0;

  memset((char *) &hints, 0, sizeof(hints));
  hints.ai_family = (want_family == AF_INET6) ? AF_INET6 : AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  /* getaddrinfo() parses literals too, so this replaces inet_addr() as well */
  if (getaddrinfo(host, (char *) 0, &hints, &res) || !res)
    return -1;

  for (ai = res; ai; ai = ai->ai_next) {
    if (ai->ai_family == AF_INET && want_family != AF_INET6) {
      chosen = ai;
      break;                    /* IPv4 preferred, stop looking */
    }
    if (ai->ai_family == AF_INET6 && !chosen)
      chosen = ai;              /* remember it, but keep looking for an A */
  }

  if (!chosen) {
    freeaddrinfo(res);
    return -1;
  }

  if (chosen->ai_family == AF_INET) {
    memcpy((char *) dst,
	   (char *) &((struct sockaddr_in *) chosen->ai_addr)->sin_addr, 4);
    *got_family = AF_INET;
  } else {
    struct in6_addr *a6 = &((struct sockaddr_in6 *) chosen->ai_addr)->sin6_addr;

    /* The brackets asked for IPv6.  Some resolvers hand back a v4-mapped
     * address for an IPv4 literal or an A-only name; letting that through
     * would make an IPv4 peer show up as ::ffff:a.b.c.d and slip past every
     * rule written for IPv4.
     */
    if (want_family == AF_INET6 && IN6_IS_ADDR_V4MAPPED(a6)) {
      freeaddrinfo(res);
      return -1;
    }
    memcpy((char *) dst, (char *) a6, 16);
    *got_family = AF_INET6;
  }

  freeaddrinfo(res);
  return 0;

#else

  struct hostent *hp;
  unsigned long l;

  if (want_family == AF_INET6)
    return -1;                  /* built without IPv6 support */

  /* inet_addr() reports failure as -1, which is also the value of the
   * perfectly good address 255.255.255.255 - hence the extra comparison.
   */
  l = inet_addr(host);
  if (l == (unsigned long) -1L && strcmp(host, "255.255.255.255")) {
    hp = gethostbyname(host);
    endhostent();
    if (!hp)
      return -1;
    memcpy((char *) dst, hp->h_addr, 4);
  } else {
    memcpy((char *) dst, (char *) &l, 4);
  }
  *got_family = AF_INET;
  return 0;

#endif

}

/*---------------------------------------------------------------------------*/

struct sockaddr *build_sockaddr(const char *name, int *addrlen)
{

  char *host_name;
  char *serv_name;
  char buf[1024];
  int want_family = AF_UNSPEC;
  size_t namelen;

  memset((char *) &addr, 0, sizeof(addr));
  *addrlen = 0;

  if (!name) return 0;

  /* Bounded: name comes from configuration files and, for the gateway
   * entries, from the far side.  It must not be trusted to fit into buf.
   */
  namelen = strlen(name);
  if (namelen >= sizeof(buf)) return 0;
  memcpy(buf, name, namelen + 1);
  host_name = buf;

  if (*host_name == '[') {
    char *end = strchr(host_name, ']');

    if (!end || end[1] != ':') return 0;
    *end = 0;
    host_name++;
    serv_name = end + 2;
    want_family = AF_INET6;
  } else {
    serv_name = strchr(buf, ':');
    if (!serv_name) return 0;
    *serv_name++ = 0;
  }

  if (!*host_name || !*serv_name) return 0;

  if (!strcmp(host_name, "local") || !strcmp(host_name, "unix")) {
    /* A relative path is taken under TCPDIR "/sockets/" */
    const char *prefix = (*serv_name != '/') ? TCPDIR "/sockets/" : "";
    size_t plen = strlen(prefix);
    size_t slen = strlen(serv_name);

    if (plen + slen >= sizeof(addr.su.sun_path)) return 0;
    addr.su.sun_family = AF_UNIX;
    memcpy(addr.su.sun_path, prefix, plen);
    memcpy(addr.su.sun_path + plen, serv_name, slen + 1);
#ifdef RISCiX
    *addrlen = sizeof(addr.su.sun_family) + strlen(addr.su.sun_path);
#else
    *addrlen = sizeof(struct sockaddr_un);
#endif
    return &addr.sa;
  }

  /* Everything else is IP.  Work out family and address first, then build the
   * matching sockaddr and put the port into the right field.
   */
  {
    unsigned char rawaddr[16];
    int af = AF_INET;
    int port;

    memset((char *) rawaddr, 0, sizeof(rawaddr));

    if (!strcmp(host_name, "*")) {
      /* The wildcard keeps meaning "any IPv4", so existing listeners are
       * unaffected.  Write [::]:port for an IPv6 listener, which on a dual
       * stack host normally serves IPv4 as well.
       */
      af = AF_INET;                     /* rawaddr stays zero: INADDR_ANY */
    } else if (!strcmp(host_name, "loopback") || !strcmp(host_name, "localhost")) {
#if HAS_AF_INET6
      if (want_family == AF_INET6) {
	af = AF_INET6;
	rawaddr[15] = 1;                /* ::1 */
      } else
#endif
      {
	af = AF_INET;
	rawaddr[0] = 127;               /* 127.0.0.1 */
	rawaddr[3] = 1;
      }
    } else if (resolve_addr(host_name, want_family, rawaddr, &af) < 0) {
      return 0;
    }

    if (isdigit(*serv_name & 0xff)) {
      char *end;
      long p = strtol(serv_name, &end, 10);

      if (*end || p < 0 || p > 65535) return 0;
      port = (int) p;
    } else {
      /* Ask for tcp explicitly.  Without a protocol getservbyname() returns
       * whatever entry comes first in the services file, and that may be a
       * udp or ddp one with a different number - on macOS "echo 4/ddp"
       * precedes "echo 7/tcp".
       */
      struct servent *sp = getservbyname(serv_name, "tcp");
      endservent();
      if (!sp) return 0;
      port = ntohs((unsigned short) sp->s_port);
    }

#if HAS_AF_INET6
    if (af == AF_INET6) {
      addr.si6.sin6_family = AF_INET6;
      memcpy((char *) &addr.si6.sin6_addr, (char *) rawaddr, 16);
      addr.si6.sin6_port = htons((unsigned short) port);
      *addrlen = sizeof(struct sockaddr_in6);
      return &addr.sa;
    }
#endif

    addr.si.sin_family = AF_INET;
    memcpy((char *) &addr.si.sin_addr, (char *) rawaddr, 4);
    addr.si.sin_port = htons((unsigned short) port);
    *addrlen = sizeof(struct sockaddr_in);
  }

  return &addr.sa;
}
