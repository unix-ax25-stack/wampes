#ifndef __lint
/* static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/udpbridge.c,v 1.1 1996/02/08 11:58:17 deyke Exp $"; */
#endif

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

struct route {
  unsigned long a_addr;
  unsigned long i_addr;
  unsigned short i_port;
  struct route *next;
};

static const char datafile[] = "data";
static const char tempfile[] = "temp";
static struct route *Routes[256];

/*---------------------------------------------------------------------------*/

static void halt(const char *msg)
{
  perror(msg);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void load_routes(void)
{

  FILE *fp;
  int i;
  struct route *rp;

  fp = fopen(datafile, "r");
  if (!fp)
    return;
  rp = (struct route *) malloc(sizeof(struct route));
  if (!rp)
    goto Done;
  while (fread(rp, sizeof(struct route), 1, fp) == 1) {
    i = (int) (rp->a_addr & 0xff);
    rp->next = Routes[i];
    Routes[i] = rp;
    rp = (struct route *) malloc(sizeof(struct route));
    if (!rp)
      goto Done;
  }
  free(rp);
Done:
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void save_routes(void)
{

  FILE *fp;
  int i;
  struct route *rp;

  fp = fopen(tempfile, "w");
  if (!fp)
    return;
  for (i = 0; i < 256; i++) {
    for (rp = Routes[i]; rp; rp = rp->next) {
      if (fwrite(rp, sizeof(struct route), 1, fp) != 1) {
	fclose(fp);
	return;
      }
    }
  }
  fclose(fp);
  rename(tempfile, datafile);
}

/*---------------------------------------------------------------------------*/

int main()
{

#define PORT 20004

  char buffer[8192];
  int addrlen;
  int fd;
  int i;
  int n;
  struct route *rp;
  struct sockaddr_in addr;
  unsigned long a_addr;

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    halt("socket");
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(PORT);
  if (bind(fd, (struct sockaddr *) &addr, sizeof(addr)))
    halt("bind");
  for (n = 0; n < 60; n++) {
    if (n != fd)
      close(n);
  }
  if (fork())
    exit(0);
  setsid();
  if (fork())
    exit(0);
  load_routes();
  for (;;) {
    addrlen = sizeof(addr);
    n = recvfrom(fd, buffer, sizeof(buffer), 0, (struct sockaddr *) &addr, &addrlen);
    if (n <= 0)
      continue;
    a_addr = *((unsigned long *) (buffer + 12));
    i = (int) (a_addr & 0xff);
    for (rp = Routes[i]; rp && rp->a_addr != a_addr; rp = rp->next) ;
    if (!rp) {
      rp = (struct route *) malloc(sizeof(struct route));
      if (rp) {
	rp->a_addr = a_addr;
	rp->i_addr = 0;
	rp->i_port = 0;
	rp->next = Routes[i];
	Routes[i] = rp;
      }
    }
    if (rp &&
	(rp->i_addr != addr.sin_addr.s_addr ||
	 rp->i_port != addr.sin_port)) {
      rp->i_addr = addr.sin_addr.s_addr;
      rp->i_port = addr.sin_port;
      save_routes();
    }
    a_addr = *((unsigned long *) (buffer + 16));
    i = (int) (a_addr & 0xff);
    for (rp = Routes[i]; rp; rp = rp->next) {
      if (rp->a_addr == a_addr) {
	addr.sin_addr.s_addr = rp->i_addr;
	addr.sin_port = rp->i_port;
	sendto(fd, buffer, n, 0, (struct sockaddr *) &addr, sizeof(addr));
      }
    }
  }
  return 0;
}
