/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.h,v 1.3 1990/09/11 13:45:49 deyke Exp $ */

#ifndef LOGIN_INCLUDED
#define LOGIN_INCLUDED

/* login.c */
int fixutmpfile __ARGS((void));
struct passwd *getpasswdentry __ARGS((char *name, int create));
struct login_cb *login_open __ARGS((char *user, char *protocol,
	void (*read_upcall) __ARGS((void *upcall_arg)),
	void (*close_upcall) __ARGS((void *upcall_arg)),
	void *upcall_arg));
void login_close __ARGS((struct login_cb *tp));
struct mbuf *login_read __ARGS((struct login_cb *tp, int cnt));
void login_write __ARGS((struct login_cb *tp, struct mbuf *bp));

#endif  /* LOGIN_INCLUDED */

