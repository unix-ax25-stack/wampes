#if 0
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/udpbridge.c,v 1.2 1996/02/13 15:30:55 deyke Exp $";
#endif

#include <sys/types.h>

#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define PORT 20004

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

static struct route *find_subnet_route(unsigned long a_addr)
{

  int i;
  struct route *rp;
  unsigned long mask;

  a_addr = ntohl(a_addr);
  for (mask = 0xfffffffe; mask; mask <<= 1) {
    a_addr &= mask;
    for (i = 0; i < 256; i++) {
      for (rp = Routes[i]; rp; rp = rp->next) {
	if ((ntohl(rp->a_addr) & mask) == a_addr)
	  return rp;
      }
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static void dump_routes(void)
{

  int i;
  struct route *rp;
  unsigned long a_addr;
  unsigned long i_addr;
  unsigned short i_port;

  for (i = 0; i < 256; i++) {
    for (rp = Routes[i]; rp; rp = rp->next) {
      a_addr = ntohl(rp->a_addr);
      i_addr = ntohl(rp->i_addr);
      i_port = ntohs(rp->i_port);
      printf("%ld.%ld.%ld.%ld: %ld.%ld.%ld.%ld:%d\n",
	     (a_addr >> 24) & 0xff,
	     (a_addr >> 16) & 0xff,
	     (a_addr >>  8) & 0xff,
	     (a_addr      ) & 0xff,
	     (i_addr >> 24) & 0xff,
	     (i_addr >> 16) & 0xff,
	     (i_addr >>  8) & 0xff,
	     (i_addr      ) & 0xff,
	     i_port);
    }
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  struct packet {
    unsigned long word1;
    unsigned long word2;
    unsigned long word3;
    unsigned long src_addr;
    unsigned long dst_addr;
    unsigned char rest[8172];
  };

  int addrlen;
  int fd;
  int i;
  int n;
  struct packet packet;
  struct route *rp;
  struct sockaddr_in addr;

  if (argc >= 2) {
    load_routes();
    dump_routes();
    exit(0);
  }

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    halt("socket");
  memset((char *) &addr, 0, sizeof(addr));
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
    n = recvfrom(fd, (char *) &packet, sizeof(struct packet), 0, (struct sockaddr *) &addr, &addrlen);
    if (n < 20)
      continue;
    i = (int) (packet.src_addr & 0xff);
    for (rp = Routes[i]; rp && rp->a_addr != packet.src_addr; rp = rp->next) ;
    if (!rp) {
      rp = (struct route *) malloc(sizeof(struct route));
      if (rp) {
	rp->a_addr = packet.src_addr;
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
    i = (int) (packet.dst_addr & 0xff);
    for (rp = Routes[i]; rp && rp->a_addr != packet.dst_addr; rp = rp->next) ;
    if (!rp)
      rp = find_subnet_route(packet.dst_addr);
    if (rp) {
      addr.sin_addr.s_addr = rp->i_addr;
      addr.sin_port = rp->i_port;
      sendto(fd, (char *) &packet, n, 0, (struct sockaddr *) &addr, sizeof(addr));
    }
  }
}
