/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/transport.h,v 1.4 1990/10/12 19:27:00 deyke Exp $ */

#ifndef TRANSPORT_INCLUDED
#define TRANSPORT_INCLUDED

#define EOL_NONE        0       /* No EOL conversion (binary) */
#define EOL_CR          1       /* EOL is "\r" */
#define EOL_LF          2       /* EOL is "\n" */
#define EOL_CRLF        3       /* EOL is "\r\n" */

struct transport_cb {
  char  *cp;                    /* Pointer to connection control block */
  int  (*recv)();               /* Recv function */
  int  (*send)();               /* Send function */
  int  (*send_space)();         /* Send space function */
  int  (*close)();              /* Close function */
  int  (*del)();                /* Delete function */
  void (*r_upcall)();           /* Called when data arrives */
  void (*t_upcall)();           /* Called when ok to send more data */
  void (*s_upcall)();           /* Called when connection is terminated */
  char  *user;                  /* User parameter (e.g., for mapping to an
				 * application control block
				 */
  struct timer timer;           /* No activity timer */
  int  recv_mode;               /* Recv EOL mode */
  int  recv_char;               /* Last char received */
  int  send_mode;               /* Send EOL mode */
  int  send_char;               /* Last char sent */
};

/* In transport.c: */
struct transport_cb *transport_open __ARGS((char *protocol, char *address, void (*r_upcall )(), void (*t_upcall )(), void (*s_upcall )(), char *user));
int transport_recv __ARGS((struct transport_cb *tp, struct mbuf **bpp, int cnt));
int transport_send __ARGS((struct transport_cb *tp, struct mbuf *bp));
int transport_send_space __ARGS((struct transport_cb *tp));
void transport_set_timeout __ARGS((struct transport_cb *tp, int timeout));
int transport_close __ARGS((struct transport_cb *tp));
int transport_del __ARGS((struct transport_cb *tp));

#endif  /* TRANSPORT_INCLUDED */

