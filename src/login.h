/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/login.h,v 1.2 1990/01/29 09:37:05 deyke Exp $ */

extern struct passwd *getpasswdentry();
extern struct login_cb *login_open();
extern void login_close();
extern struct mbuf *login_read();
extern void login_write();

