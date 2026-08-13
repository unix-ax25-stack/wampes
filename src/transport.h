/* @(#) $Id: transport.h,v 1.13 2005/03/11 14:36:09 dl9sau Exp $ */

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
  TP_TCP,
  TP_AXFLEXTALK
};

struct ax25;
struct ax25_opts;

struct transport_cb {
  enum e_transporttype type;    /* Connection type */
  int connected;                /* Link is up.  s_upcall fires on the way in
				 * as well as on the way out, and this says
				 * which - there used to be no way to learn
				 * that a connection had come up at all */
  int pid;                      /* AX.25 protocol id to send with; the
				 * protocol keyword used to decide this, and
				 * "flextalk" is now just one value of it */
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

/* For a target that has already been parsed - with its own source call, its
 * own port and its own pid, none of which fit through an address string.
 */
struct transport_cb *transport_open_target(
  struct ax25 *hdr,
  const struct ax25_opts *opts,
  int pid,
  void (*r_upcall)(struct transport_cb *tp, int cnt),
  void (*t_upcall)(struct transport_cb *tp, int cnt),
  void (*s_upcall)(struct transport_cb *tp),
  void *user);

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
void transport_close(void *arg);
int transport_del(struct transport_cb *tp);

#endif  /* _TRANSPORT_H */
