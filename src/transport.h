/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/transport.h,v 1.7 1991/06/04 11:35:01 deyke Exp $ */

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
  void *cp;                     /* Pointer to connection control block */
  int (*recv) __ARGS((void *cp, struct mbuf **bpp, int cnt));
				/* Recv function */
  int (*send) __ARGS((void *cp, struct mbuf *bp));
				/* Send function */
  int (*send_space) __ARGS((void *cp));
				/* Send space function */
  int (*close) __ARGS((void *cp));
				/* Close function */
  int (*del) __ARGS((void *cp));
				/* Delete function */
  void (*r_upcall) __ARGS((struct transport_cb *tp, int cnt));
				/* Called when data arrives */
  void (*t_upcall) __ARGS((struct transport_cb *tp, int cnt));
				/* Called when ok to send more data */
  void (*s_upcall) __ARGS((struct transport_cb *tp));
				/* Called when connection is terminated */
  void *user;                   /* User parameter (e.g., for mapping to an
				 * application control block
				 */
  struct timer timer;           /* No activity timer */
  int  recv_mode;               /* Recv EOL mode */
  int  recv_char;               /* Last char received */
  int  send_mode;               /* Send EOL mode */
  int  send_char;               /* Last char sent */
};

/* In transport.c: */
struct transport_cb *transport_open __ARGS((
  char *protocol,
  char *address,
  void (*r_upcall) __ARGS((struct transport_cb *tp, int cnt)),
  void (*t_upcall) __ARGS((struct transport_cb *tp, int cnt)),
  void (*s_upcall) __ARGS((struct transport_cb *tp)),
  void *user));
int transport_recv __ARGS((struct transport_cb *tp, struct mbuf **bpp, int cnt));
int transport_send __ARGS((struct transport_cb *tp, struct mbuf *bp));
int transport_send_space __ARGS((struct transport_cb *tp));
void transport_set_timeout __ARGS((struct transport_cb *tp, int timeout));
int transport_close __ARGS((struct transport_cb *tp));
int transport_del __ARGS((struct transport_cb *tp));

#endif  /* _TRANSPORT_H */
