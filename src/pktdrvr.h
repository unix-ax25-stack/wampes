/* @(#) $Id: pktdrvr.h,v 1.12 1999/01/27 18:45:40 deyke Exp $ */

#ifndef _PKTDRVR_H
#define _PKTDRVR_H

/* Packet driver interface classes */
#define CL_NONE         0
#define CL_ETHERNET     1
#define CL_PRONET_10    2
#define CL_IEEE8025     3
#define CL_OMNINET      4
#define CL_APPLETALK    5
#define CL_SERIAL_LINE  6
#define CL_STARLAN      7
#define CL_ARCNET       8
#define CL_AX25         9
#define CL_KISS         10
#define CL_IEEE8023     11
#define CL_FDDI         12
#define CL_INTERNET_X25 13
#define CL_LANSTAR      14
#define CL_SLFP         15
#define CL_NETROM       16
#define CL_PPP          17
#define CL_QTSO         18
#define NCLASS          19

#endif  /* _PKTDRVR_H */
