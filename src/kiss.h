/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kiss.h,v 1.2 1990/08/23 17:33:17 deyke Exp $ */

#ifndef KISS_DATA

/* KISS TNC control */
#define KISS_DATA       0
#define KISS_TXD        1
#define KISS_P          2
#define KISS_ST         3
#define KISS_TXT        4
#define KISS_FD         5
#define KISS_HW         6
#define KISS_RETURN     0xff

/* In kiss.c: */
int kiss_raw __ARGS((struct iface *iface,struct mbuf *data));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));
int kiss_ioctl __ARGS((struct iface *iface,int argc,char *argv[]));
void kiss_recv __ARGS((struct iface *iface,struct mbuf *bp));

#endif  /* KISS_DATA */

