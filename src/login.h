/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.h,v 1.5 1991/02/24 20:17:13 deyke Exp $ */

#ifndef _LOGIN_H
#define _LOGIN_H

/* In login.c: */
int fixutmpfile __ARGS((void));
struct passwd *getpasswdentry __ARGS((char *name, int create));
struct login_cb *login_open __ARGS((char *user, char *protocol,
	void (*read_upcall) __ARGS((void *upcall_arg)),
	void (*close_upcall) __ARGS((void *upcall_arg)),
	void *upcall_arg));
void login_close __ARGS((struct login_cb *tp));
struct mbuf *login_read __ARGS((struct login_cb *tp, int cnt));
void login_write __ARGS((struct login_cb *tp, struct mbuf *bp));

#endif  /* _LOGIN_H */
