/* @(#) $Id: buildsaddr.c,v 1.17 1996/08/12 18:53:41 deyke Exp $ */

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

static union {
  struct sockaddr sa;
  struct sockaddr_in si;
  struct sockaddr_un su;
} addr;

/*---------------------------------------------------------------------------*/

struct sockaddr *build_sockaddr(const char *name, int *addrlen)
{

  char *host_name;
  char *serv_name;
  char buf[1024];

  memset((char *) &addr, 0, sizeof(addr));
  *addrlen = 0;

  host_name = strcpy(buf, name);
  serv_name = strchr(buf, ':');
  if (!serv_name) return 0;
  *serv_name++ = 0;
  if (!*host_name || !*serv_name) return 0;

  if (!strcmp(host_name, "local") || !strcmp(host_name, "unix")) {
    addr.su.sun_family = AF_UNIX;
    *addr.su.sun_path = 0;
    if (*serv_name != '/') strcpy(addr.su.sun_path, TCPDIR "/sockets/");
    strcat(addr.su.sun_path, serv_name);
#ifdef RISCiX
    *addrlen = sizeof(addr.su.sun_family) + strlen(addr.su.sun_path);
#else
    *addrlen = sizeof(struct sockaddr_un);
#endif
    return &addr.sa;
  }

  addr.si.sin_family = AF_INET;

  if (!strcmp(host_name, "*")) {
    addr.si.sin_addr.s_addr = INADDR_ANY;
  } else if (!strcmp(host_name, "loopback") || !strcmp(host_name, "localhost")) {
    addr.si.sin_addr.s_addr = inet_addr("127.0.0.1");
  } else if ((addr.si.sin_addr.s_addr = inet_addr(host_name)) == -1L) {
    struct hostent *hp = gethostbyname(host_name);
    endhostent();
    if (!hp) return 0;
    addr.si.sin_addr.s_addr = ((struct in_addr *) (hp->h_addr))->s_addr;
  }

  if (isdigit(*serv_name & 0xff)) {
    addr.si.sin_port = htons(atoi(serv_name));
  } else {
    struct servent *sp = getservbyname(serv_name, 0);
    endservent();
    if (!sp) return 0;
    addr.si.sin_port = sp->s_port;
  }

  *addrlen = sizeof(struct sockaddr_in);
  return &addr.sa;
}
