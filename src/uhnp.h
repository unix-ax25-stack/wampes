/* udp src port learning for axudp and ipudp */

#ifndef	_UHNP_H
#define	_UHNP_H

#include <sys/types.h>
#include <sys/socket.h>

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int family;                   /* AF_INET or AF_INET6 of the socket */
  int port;
  int fd;
  struct udp_host_nat_port *uhnp;
  time_t uhnp_time;
};

struct udp_host_nat_port {
  struct sockaddr_storage addr;
  time_t time;
  struct udp_host_nat_port *next;
};

#define	UHNP_LEASETIME 60*60L

struct sockaddr *search_udp_host_nat_port(const struct sockaddr *addr,
	struct edv_t *edv);
void learn_udp_host_nat_port(const struct sockaddr *addr, struct edv_t *edv);
void uhnp_cleanup(struct edv_t *edv);
#endif
