/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/socket.h,v 1.13 1993/02/23 21:34:17 deyke Exp $ */

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
/* now in internet.h */

/* TCP port numbers */
#define IPPORT_ECHO     7       /* Echo data port */
#define IPPORT_DISCARD  9       /* Discard data port */
#define IPPORT_FTPD     20      /* FTP Data port */
#define IPPORT_FTP      21      /* FTP Control port */
#define IPPORT_TELNET   23      /* Telnet port */
#define IPPORT_SMTP     25      /* Mail port */
#define IPPORT_MTP      57      /* Secondary telnet protocol */
#define IPPORT_FINGER   79      /* Finger port */
#define IPPORT_TTYLINK  87      /* Chat port */
#define IPPORT_POP      109     /* pop2 port */
#define IPPORT_NNTP     119     /* Netnews port */
#define IPPORT_LOGIN    513     /* BSD rlogin port */
#define IPPORT_TERM     5000    /* Serial interface server port */

/* UDP port numbers */
#define IPPORT_DOMAIN   53
#define IPPORT_BOOTPS   67
#define IPPORT_BOOTPC   68
#define IPPORT_RIP      520
#define IPPORT_REMOTE   1234    /* Pulled out of the air */
#define IPPORT_BSR      5000    /* BSR X10 interface server port (UDP) */

#if 0
#define AF_INET         0
#define AF_AX25         1
#define AF_NETROM       2
#define AF_LOCAL        3
#define NAF             4

#define SOCK_STREAM     0
#define SOCK_DGRAM      1
#define SOCK_RAW        2
#define SOCK_SEQPACKET  3

#define EWOULDBLOCK     36
#define ENOTCONN        37
#define ESOCKTNOSUPPORT 38
#define EAFNOSUPPORT    39
#define EISCONN         40
#define EOPNOTSUPP      41
#endif
#define EALARM          42
#define EABORT          43
#if 0
#undef  EINTR
#define EINTR           44
#define ECONNREFUSED    45
#define EMSGSIZE        46
#define EADDRINUSE      47
#define EMAX            47

extern char *Sock_errlist[];

/* In socket.c: */
extern int Axi_sock;    /* Socket listening to AX25 (there can be only one) */

int accept __ARGS((int s,char *peername,int *peernamelen));
int bind __ARGS((int s,char *name,int namelen));
int close_s __ARGS((int s));
int connect __ARGS((int s,char *peername,int peernamelen));
char *eolseq __ARGS((int s));
void freesock __ARGS((struct proc *pp));
int getpeername __ARGS((int s,char *peername,int *peernamelen));
int getsockname __ARGS((int s,char *name,int *namelen));
int listen __ARGS((int s,int backlog));
int recv_mbuf __ARGS((int s,struct mbuf **bpp,int flags,char *from,int *fromlen));
int send_mbuf __ARGS((int s,struct mbuf *bp,int flags,char *to,int tolen));
int settos __ARGS((int s,int tos));
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
int recv __ARGS((int s,char *buf,int len,int flags));
int recvfrom __ARGS((int s,char *buf,int len,int flags,char *from,int *fromlen));
int send __ARGS((int s,char *buf,int len,int flags));
int sendto __ARGS((int s,char *buf,int len,int flags,char *to,int tolen));

/* In file sockutil.c: */
char *psocket __ARGS((void *p));
char *sockerr __ARGS((int s));
char *sockstate __ARGS((int s));
#endif

/* In file tcpsock.c: */
int start_tcp __ARGS((int   port,char *name,void (*task)(),int stack));
int stop_tcp __ARGS((int   port));

#endif  /* _SOCKET_H */
