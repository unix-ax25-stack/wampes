#include <sys/types.h>

#include <ctype.h>
#include <memory.h>
#include <string.h>
#include <sys/socket.h>
#ifdef hpux
#include <netinet/in.h>
#include <netdb.h>
#endif

struct sockaddr *build_sockaddr(name, addrlen)
char  *name;
int  *addrlen;
{

  char  *host_name, *serv_name;
  char  buf[1024];
  static char  sa[1024];
  struct sockaddr *addr;

  *addrlen = 0;
  addr = (struct sockaddr *) memset(sa, 0, sizeof(sa));

  host_name = strcpy(buf, name);
  serv_name = strchr(buf, ':');
  if (!serv_name) return 0;
  *serv_name++ = '\0';
  if (!*host_name || !*serv_name) return 0;

  if (!strcmp(host_name, "local") || !strcmp(host_name, "unix")) {
    addr->sa_family = AF_UNIX;
    addr->sa_data[0] = '\0';
    if (*serv_name != '/') strcpy(addr->sa_data, "/usr/spool/sockets/");
    strcat(addr->sa_data, serv_name);
    *addrlen = sizeof(addr->sa_family) + strlen(addr->sa_data);
    return addr;
  }

#ifdef hpux

  {

    struct sockaddr_in *addr_in;

    addr_in = (struct sockaddr_in *) addr;
    addr_in->sin_family = AF_INET;

    if (!strcmp(host_name, "*")) {
      addr_in->sin_addr.s_addr = INADDR_ANY;
    } else if (!strcmp(host_name, "loopback")) {
      addr_in->sin_addr.s_addr = 0x7f000001;
    } else if (isdigit(*host_name & 0xff)) {
      int  i;
      for (i = 24; i >= 0; i -= 8) {
	addr_in->sin_addr.s_addr |= (atoi(host_name) << i);
	if (!(host_name = strchr(host_name, '.'))) break;
	host_name++;
      }
    } else {
      struct hostent *hp = gethostbyname(host_name);
      endhostent();
      if (!hp) return 0;
      addr_in->sin_addr.s_addr = ((struct in_addr *) (hp->h_addr))->s_addr;
    }

    if (isdigit(*serv_name & 0xff)) {
      addr_in->sin_port = atoi(serv_name);
    } else {
      struct servent *sp = getservbyname(serv_name, (char *) 0);
      endservent();
      if (!sp) return 0;
      addr_in->sin_port = sp->s_port;
    }

    *addrlen = sizeof(struct sockaddr_in );
    return addr;

  }

#else

  return 0;

#endif

}

