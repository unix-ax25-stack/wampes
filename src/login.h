/* @(#) $Id: login.h,v 1.12 1996/08/12 18:51:17 deyke Exp $ */

#ifndef _LOGIN_H
#define _LOGIN_H

/* In login.c: */
void fixutmpfile(void);
struct passwd *getpasswdentry(const char *name, int create);
struct login_cb *login_open(const char *user, const char *protocol,
	void (*read_upcall)(void *arg),
	void (*close_upcall)(void *arg),
	void *upcall_arg);
void login_close(struct login_cb *tp);
struct mbuf *login_read(struct login_cb *tp, int cnt);
void login_write(struct login_cb *tp, struct mbuf **bpp);

#endif  /* _LOGIN_H */
