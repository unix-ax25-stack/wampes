/* udp src port learning for axudp and ipudp */

#ifndef	_UHNP_H
#define	_UHNP_H

#include <sys/types.h>
#include <sys/socket.h>

struct iface;

/* ONE SOCKET PER ADDRESS, and the address is what says the family.
 *
 * Not one socket for both: a socket cannot be told to answer on 0.0.0.0 and on
 * ::1 at the same time, and "bind=0.0.0.0,[::1]" is two addresses a sysop
 * wrote on purpose.  So the list is a list, one entry per socket, and at most
 * one entry per family - a second IPv4 address would be a second socket with
 * the same family, and then "which socket does a route leave by" would have no
 * answer left that the sysop wrote down.
 *
 * ifp is here because this node IS the argument on_read() gets: the receive
 * path has to know which socket woke it up, and passing the interface as it
 * did before meant one fd for the whole port - which is exactly the thing
 * there is more than one of now.
 */
struct axip_sock {
  struct axip_sock *next;
  struct iface *ifp;
  int fd;
  int family;                   /* AF_INET or AF_INET6 */
};

struct edv_t {
  int type;
#define USE_IP          0
#define USE_UDP         1
  int port;                     /* the one we bind to.  With USE_IP this is
                                 * the IP PROTOCOL number instead - a raw
                                 * socket has no port. */
  int dport;                    /* where we send when nothing else says: the
                                 * route may carry its own, and a learned
                                 * source port beats both.  Equal to port
                                 * unless the sysop wrote "<src>:<dst>", which
                                 * is why everything behaved as before while
                                 * there was only one number. */
  struct axip_sock *socks;
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
