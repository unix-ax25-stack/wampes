/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/socket.h,v 1.6 1991/06/18 17:27:31 deyke Exp $ */

#ifndef _SOCKET_H
#define _SOCKET_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifdef  ANSIPROTO
#include <stdarg.h>
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _PROC_H
#include "proc.h"
#endif

/* Local IP wildcard address */
#ifndef INADDR_ANY
#define INADDR_ANY      0x0L
#endif

/* IP protocol numbers */
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17
#define IPPROTO_IP      94

/* TCP port numbers */
#define IPPORT_ECHO     7       /* Echo data port */
#define IPPORT_DISCARD  9       /* Discard data port */
#define IPPORT_FTPD     20      /* FTP Data port */
#define IPPORT_FTP      21      /* FTP Control port */
#define IPPORT_TELNET   23      /* Telnet port */
#define IPPORT_SMTP     25      /* Mail port */
#define IPPORT_FINGER   79      /* Finger port */
#define IPPORT_TTYLINK  87      /* Chat port */
#define IPPORT_POP      109     /* pop2 port */
#define IPPORT_NNTP     119     /* Netnews port */

/* UDP port numbers */
#define IPPORT_DOMAIN   53
#define IPPORT_BOOTPS   67
#define IPPORT_BOOTPC   68
#define IPPORT_RIP      520
#define IPPORT_REMOTE   1234    /* Pulled out of the air */

#if 0

#define AF_INET         0
#define AF_AX25         1
#define AF_NETROM       2
#define AF_LOCAL        3

#define SOCK_STREAM     0
#define SOCK_DGRAM      1
#define SOCK_RAW        2
#define SOCK_SEQPACKET  3

/* Socket flag values - controls newline mapping */
#define SOCK_BINARY     0       /* socket in raw (binary) mode */
#define SOCK_ASCII      1       /* socket in cooked (newline mapping) mode */
#define SOCK_QUERY      2       /* Return setting without change */

#define EMFILE  1
#define EBADF   2
#define EINVAL  3
#define ESOCKTNOSUPPORT 4
#define EAFNOSUPPORT    5
#define EOPNOTSUPP      6
#define EFAULT          7
#define ENOTCONN        8
#define ECONNREFUSED    9
#define EAFNOSUPP       10
#define EISCONN         11
#define EWOULDBLOCK     12
#define EINTR           13
#define EADDRINUSE      14
#define ENOMEM          15
#define EMSGSIZE        16
#define EALARM          17
#define EABORT          18

extern int32 Ip_addr;

/* In socket.c: */
extern int Axi_sock;    /* Socket listening to AX25 (there can be only one) */

int accept __ARGS((int s,char *peername,int *peernamelen));
int bind __ARGS((int s,char *name,int namelen));
int close_s __ARGS((int s));
int connect __ARGS((int s,char *peername,int peernamelen));
void freesock __ARGS((struct proc *pp));
int getpeername __ARGS((int s,char *peername,int *peernamelen));
int getsockname __ARGS((int s,char *name,int *namelen));
int listen __ARGS((int s,int backlog));
int recv_mbuf __ARGS((int s,struct mbuf **bpp,int flags,char *from,int *fromlen));
int send_mbuf __ARGS((int s,struct mbuf *bp,int flags,char *to,int tolen));
int setflush __ARGS((int s,int c));
int shutdown __ARGS((int s,int how));
int socket __ARGS((int af,int type,int protocol));
void sockinit __ARGS((void));
int sockkick __ARGS((int s));
int socklen __ARGS((int s,int rtx));
struct proc *sockowner __ARGS((int s,struct proc *newowner));
int usesock __ARGS((int s));
int socketpair __ARGS((int af,int type,int protocol,int sv[]));

/* In sockuser.c: */
void flushsocks __ARGS((void));
int keywait __ARGS((char *prompt,int flush));
int recv __ARGS((int s,char *buf,int len,int flags));
int recvchar __ARGS((int s));
int recvfrom __ARGS((int s,char *buf,int len,int flags,char *from,int *fromlen));
int recvline __ARGS((int s,char *buf,unsigned len));
int rrecvchar __ARGS((int s));
int send __ARGS((int s,char *buf,int len,int flags));
int sendto __ARGS((int s,char *buf,int len,int flags,char *to,int tolen));
int seteol __ARGS((int s,char *seq));
int sockmode __ARGS((int s,int mode));
void tflush __ARGS((void));
int tprintf __ARGS((char *fmt,...));
int tputc __ARGS((char c));
int tputs __ARGS((char *s));
int usflush __ARGS((int s));
int usprintf __ARGS((int s,char *fmt,...));
int usputc __ARGS((int s,char c));
int usputs __ARGS((int s,char *x));
int usvprintf __ARGS((int s,char *fmt, va_list args));

/* In file sockutil.c: */
char *psocket __ARGS((void *p));
char *sockerr __ARGS((int s));
char *sockstate __ARGS((int s));

#endif

#endif  /* _SOCKET_H */
