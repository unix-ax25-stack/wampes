/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/transport.h,v 1.10 1994/10/06 16:15:39 deyke Exp $ */

#ifndef _TRANSPORT_H
#define _TRANSPORT_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

enum e_transporteol {
  EOL_NONE,                     /* No EOL conversion (binary) */
  EOL_CR,                       /* EOL is "\r" */
  EOL_LF,                       /* EOL is "\n" */
  EOL_CRLF                      /* EOL is "\r\n" */
};

enum e_transporttype {
  TP_AX25,
  TP_NETROM,
  TP_TCP
};

struct transport_cb {
  enum e_transporttype type;    /* Connection type */
  union {                       /* Pointer to connection control block */
    struct ax25_cb *axp;
    struct circuit *nrp;
    struct tcb *tcp;
  } cb;
  void (*r_upcall)(struct transport_cb *tp, int cnt);
				/* Called when data arrives */
  void (*t_upcall)(struct transport_cb *tp, int cnt);
				/* Called when ok to send more data */
  void (*s_upcall)(struct transport_cb *tp);
				/* Called when connection is terminated */
  void *user;                   /* User parameter (e.g., for mapping to an
				 * application control block
				 */
  struct timer timer;           /* No activity timer */
  enum e_transporteol recv_mode;/* Recv EOL mode */
  int recv_char;                /* Last char received */
  enum e_transporteol send_mode;/* Send EOL mode */
  int send_char;                /* Last char sent */
};

/* In transport.c: */
struct transport_cb *transport_open(
  const char *protocol,
  const char *address,
  void (*r_upcall)(struct transport_cb *tp, int cnt),
  void (*t_upcall)(struct transport_cb *tp, int cnt),
  void (*s_upcall)(struct transport_cb *tp),
  void *user);
int transport_recv(struct transport_cb *tp, struct mbuf **bpp, int cnt);
int transport_send(struct transport_cb *tp, struct mbuf *bp);
int transport_send_space(struct transport_cb *tp);
void transport_set_timeout(struct transport_cb *tp, int timeout);
int transport_close(struct transport_cb *tp);
int transport_del(struct transport_cb *tp);

#endif  /* _TRANSPORT_H */
