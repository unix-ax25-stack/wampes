/* @(#) $Id: socket.h,v 1.24 1999/01/27 18:45:40 deyke Exp $ */

#ifndef _SOCKET_H
#define _SOCKET_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#include <stdarg.h>

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
#define IPPORT_POP2     109     /* pop2 port */
#define IPPORT_POP3     110     /* pop3 port */
#define IPPORT_NNTP     119     /* Netnews port */
#define IPPORT_LOGIN    513     /* BSD rlogin port */
#define IPPORT_TERM     5000    /* Serial interface server port */

/* UDP port numbers */
#define IPPORT_TIME     37      /* Time Protocol */
#define IPPORT_DOMAIN   53
#define IPPORT_BOOTPS   67
#define IPPORT_BOOTPC   68
#define IPPORT_NTP      123     /* Network Time Protocol */
#define IPPORT_PHOTURIS 468     /* Photuris Key management */
#define IPPORT_RIP      520
#define IPPORT_REMOTE   1234    /* Pulled out of the air */
#define IPPORT_BSR      5000    /* BSR X10 interface server port (UDP) */

#endif  /* _SOCKET_H */
