/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/transport.h,v 1.9 1993/05/17 13:45:23 deyke Exp $ */

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

#define EOL_NONE        0       /* No EOL conversion (binary) */
#define EOL_CR          1       /* EOL is "\r" */
#define EOL_LF          2       /* EOL is "\n" */
#define EOL_CRLF        3       /* EOL is "\r\n" */

struct transport_cb {
  int type;                     /* Connection type */
#define TP_AX25         1
#define TP_NETROM       2
#define TP_TCP          3
  void *cp;                     /* Pointer to connection control block */
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
  int recv_mode;                /* Recv EOL mode */
  int recv_char;                /* Last char received */
  int send_mode;                /* Send EOL mode */
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
