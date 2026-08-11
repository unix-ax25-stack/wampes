/* udp src port learning for axudp and ipudp */

#include "global.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "timer.h"

#include "sockaddr_util.h"
#include "uhnp.h"

struct sockaddr *search_udp_host_nat_port(const struct sockaddr *addr,
	struct edv_t *edv)
{
  struct udp_host_nat_port *up;
  struct udp_host_nat_port *up_prev = 0;
  for (up = edv->uhnp; up; up = up->next) {
    if (sockaddr_addr_eq((struct sockaddr *) &up->addr, addr)) {
      up->time = secclock();
      /* put this element to head.  up->next has to be re-pointed at the old
       * head: without that the entry is unlinked and made the head while
       * still pointing at what used to follow it, so everything ahead of it
       * falls out of the list.  With more than one peer that quietly emptied
       * the table down to a single entry on the first lookup. */
      if (up_prev) {
        up_prev->next = up->next;
        up->next = edv->uhnp;
        edv->uhnp = up;
      }
      return (struct sockaddr *) &up->addr;
    }
    up_prev = up;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

void learn_udp_host_nat_port(const struct sockaddr *addr, struct edv_t *edv)
{
  struct udp_host_nat_port *up;
  struct udp_host_nat_port *up_prev = 0;
  int defaultport = edv->port;

  for (up = edv->uhnp; up; up = up->next) {
    if (sockaddr_addr_eq((struct sockaddr *) &up->addr, addr)) {
      if (sockaddr_port(addr) == defaultport) {
        if (up_prev)
          up_prev->next = up->next;
        else
          edv->uhnp = up->next;
        free(up);
        return;
      }
      /* learn src port */
      sockaddr_set_port((struct sockaddr *) &up->addr, sockaddr_port(addr));
      up->time = secclock();
      /* put this element to head.  up->next has to be re-pointed at the old
       * head: without that the entry is unlinked and made the head while
       * still pointing at what used to follow it, so everything ahead of it
       * falls out of the list.  With more than one peer that quietly emptied
       * the table down to a single entry on the first lookup. */
      if (up_prev) {
        up_prev->next = up->next;
        up->next = edv->uhnp;
        edv->uhnp = up;
      }
      return;
    }
    up_prev = up;
  }
  if (!(up = (struct udp_host_nat_port *) malloc(sizeof(struct udp_host_nat_port))))
    return;
  memset(&up->addr, 0, sizeof(up->addr));
  memcpy(&up->addr, addr, (size_t) sockaddr_len(addr));
  up->time = secclock();
  up->next = edv->uhnp;
  edv->uhnp = up;
}

/*---------------------------------------------------------------------------*/

void uhnp_cleanup(struct edv_t *edv)
{
  struct udp_host_nat_port *up;
  struct udp_host_nat_port *up_prev;
  if (edv->uhnp_time + UHNP_LEASETIME > secclock())
     return;
  edv->uhnp_time = secclock();
again:
  up_prev = 0;
  for (up = edv->uhnp; up; up = up->next) {
     if (up->time + UHNP_LEASETIME < secclock()) {
       if (up_prev)
         up_prev->next = up->next;
       else
         edv->uhnp = up->next;
       free(up);
       goto again;
     }
     up_prev = up;
  }
}
