/* udp src port learning for axudp and ipudp */

#include "global.h"

#include <netinet/in.h>
#include <sys/socket.h>

#include "timer.h"

#include "uhnp.h"

struct sockaddr_in *search_udp_host_nat_port(int32 addr, struct edv_t *edv) 
{
  struct udp_host_nat_port *up;
  struct udp_host_nat_port *up_prev = 0;
  for (up = edv->uhnp; up; up = up->next) {
    if (up->addr.sin_addr.s_addr == addr) {
      up->time = secclock();
      /* put this element to head */
      if (up_prev) {
        up_prev->next = up->next;
        edv->uhnp = up;
      }
      return &up->addr;
    }
    up_prev = up;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

void learn_udp_host_nat_port(struct sockaddr_in *addr, struct edv_t *edv)
{
  struct udp_host_nat_port *up;
  struct udp_host_nat_port *up_prev = 0;
  int defaultport = htons(edv->port);

  for (up = edv->uhnp; up; up = up->next) {
    if (up->addr.sin_addr.s_addr == addr->sin_addr.s_addr) {
      if (addr->sin_port == defaultport) {
        if (up_prev)
          up_prev->next = up->next;
        else
          edv->uhnp = up->next;
        free(up);
        return;
      }
      /* learn src port */
      up->addr.sin_port = addr->sin_port;
      up->time = secclock();
      /* put this element to head */
      if (up_prev) {
        up_prev->next = up->next;
        edv->uhnp = up;
      }
      return;
    }
    up_prev = up;
  }
  if (!(up = (struct udp_host_nat_port *) malloc(sizeof(struct udp_host_nat_port))))
    return;
  memcpy(&up->addr, addr, sizeof(struct sockaddr_in));
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
