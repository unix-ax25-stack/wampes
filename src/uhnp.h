/* udp src port learning for axudp and ipudp */

#ifndef	_UHNP_H
#define	_UHNP_H

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;
  int fd;
  struct udp_host_nat_port *uhnp;
  time_t uhnp_time;
};

struct udp_host_nat_port {
  struct sockaddr_in addr;
  time_t time;
  struct udp_host_nat_port *next;
};

#define	UHNP_LEASETIME 60*60L

struct sockaddr_in *search_udp_host_nat_port(int32 addr, struct edv_t *edv);
void learn_udp_host_nat_port(struct sockaddr_in *addr, struct edv_t *edv);
void uhnp_cleanup(struct edv_t *edv);
#endif
