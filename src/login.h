/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.h,v 1.7 1992/09/01 16:52:53 deyke Exp $ */

#ifndef _LOGIN_H
#define _LOGIN_H

/* In login.c: */
void fixutmpfile __ARGS((void));
struct passwd *getpasswdentry __ARGS((const char *name, int create));
struct login_cb *login_open __ARGS((const char *user, const char *protocol,
	void (*read_upcall)__ARGS ((void *arg)),
	void (*close_upcall)__ARGS ((void *arg)),
	void *upcall_arg));
void login_close __ARGS((struct login_cb *tp));
struct mbuf *login_read __ARGS((struct login_cb *tp, int cnt));
void login_write __ARGS((struct login_cb *tp, struct mbuf *bp));

/* In netlog.c: */
void write_log __ARGS((int fd, char *buf, int cnt));

#endif  /* _LOGIN_H */
