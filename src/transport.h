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

extern struct transport_cb *transport_open();
extern int  transport_recv();
extern int  transport_send();
extern int  transport_send_space();
extern void transport_set_timeout();
extern int  transport_close();
extern int  transport_del();
