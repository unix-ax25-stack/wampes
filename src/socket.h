/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/socket.h,v 1.3 1991/02/24 20:17:41 deyke Exp $ */

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

/* Local IP wildcard address */
#define INADDR_ANY      0x0L

/* IP protocol numbers */
#define IPPROTO_ICMP    1
#define IPPROTO_TCP     6
#define IPPROTO_UDP     17

/* TCP port numbers */
#define IPPORT_ECHO     7       /* Echo data port */
#define IPPORT_DISCARD  9       /* Discard data port */
#define IPPORT_FTPD     20      /* FTP Data port */
#define IPPORT_FTP      21      /* FTP Control port */
#define IPPORT_TELNET   23      /* Telnet port */
#define IPPORT_SMTP     25      /* Mail port */
#define IPPORT_FINGER   79      /* Finger port */
#define IPPORT_TTYLINK  87      /* Chat port */

/* UDP port numbers */
#define IPPORT_DOMAIN   53
#define IPPORT_RIP      520
#define IPPORT_REMOTE   1234    /* Pulled out of the air */

#define AF_INET         0
#define AF_AX25         1
#define AF_NETROM       2
#define AF_LOCAL        3

#define SOCK_STREAM     0
#define SOCK_DGRAM      1
#define SOCK_RAW        2
#define SOCK_SEQPACKET  3

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

/* Berkeley format socket address structures. These things were rather
 * poorly thought out, but compatibility is important (or so they say).
 * Note that all the sockaddr variants must be of the same size, 16 bytes
 * to be specific. Although attempts have been made to account for alignment
 * requirements (notably in sockaddr_ax), porters should check each
 * structure.
 */

/* Generic socket address structure */
struct sockaddr {
	short sa_family;
	char sa_data[14];
};

/* This is a structure for "historical" reasons (whatever they are) */
struct in_addr {
	unsigned long s_addr;
};

/* Socket address, DARPA Internet style */
struct sockaddr_in {
	short sin_family;
	unsigned short sin_port;
	struct in_addr sin_addr;
	char sin_zero[8];
};

/* AX.25 sockaddr stuff */
#define AXALEN          7

/* Number of chars in interface field. The involved definition takes possible
 * alignment requirements into account, since ax25_addr is of an odd size.
 */
#define ILEN    (sizeof(struct sockaddr) - sizeof(short) - AXALEN)

/* Socket address, AX.25 style */
struct sockaddr_ax {
	short sax_family;               /* 2 bytes */
	char ax25_addr[AXALEN];
	char iface[ILEN];               /* Interface name */
};

/* Netrom socket address. */
#ifndef NR4MAXCIRC
struct nr4_addr {
	char user[AXALEN];
	char node[AXALEN];
};
#endif

struct sockaddr_nr {
	short nr_family;
	struct nr4_addr nr_addr;
};

#define SOCKSIZE        (sizeof(struct sockaddr))
#define MAXSOCKSIZE     SOCKSIZE /* All sockets are of the same size for now */

extern int32 Ip_addr;

#endif  /* _SOCKET_H */
