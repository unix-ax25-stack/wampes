/* @(#) $Id: global.h,v 1.49 2002/01/12 15:55:53 dl9sau Exp $ */

#ifndef _GLOBAL_H
#define _GLOBAL_H

#ifdef ibm032
#include <stddef.h>
typedef int pid_t;
#endif

#include <sys/types.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _CONFIGURE_H
#include "configure.h"
#endif

#ifndef _STRDUP_H
#include "strdup.h"
#endif

#if defined sun

#define memcmp(s1, s2, n) \
	memcmp((char *) (s1), (char *) (s2), n)

#define memcpy(s1, s2, n) \
	memcpy((char *) (s1), (char *) (s2), n)

#define memchr(s, c, n) \
	memchr((char *) s, c, n)

#define memset(s, c, n) \
	memset((char *) (s), c, n)

#endif

/* Global definitions used by every source file.
 * Some may be compiler dependent.
 *
 * This file depends only on internal macros or those defined on the
 * command line, so it may be safely included first.
 */

/* Resolve C++ name conflicts */

#ifdef __cplusplus
#define EXTERN_C        extern "C"
#define class           class_
#define new             new_
#define private         private_
#define signed
#define this            this_
#ifdef __hpux
#define volatile
#endif
#else
#define EXTERN_C
#endif

#define READ_BINARY     "r"
#define WRITE_BINARY    "w"
#define APPEND_BINARY   "a+"
#define READ_TEXT       "r"
#define WRITE_TEXT      "w"
#define APPEND_TEXT     "a+"

/* These two lines assume that your compiler's longs are 32 bits and
 * shorts are 16 bits. It is already assumed that chars are 8 bits,
 * but it doesn't matter if they're signed or unsigned.
 *
 * On 64-bit systems (LP64) 'long' is 64 bits, which silently breaks
 * all 32-bit wrap-around arithmetic in the TCP sequence numbers and
 * the VJ compression deltas (see axhc.c, tcpin.c, tcp.h). Use 'int'
 * which is 32 bits on every platform this code runs on.
 */
#if !HAS_INT32
typedef int int32;              /* 32-bit signed integer */
#endif
#if !HAS_UINT
typedef unsigned int uint;      /* 16 or 32-bit unsigned integer */
#endif
typedef unsigned int uint32;    /* 32-bit unsigned integer */
typedef unsigned short uint16;  /* 16-bit unsigned integer */
typedef unsigned char byte_t;   /*  8-bit unsigned integer */
typedef unsigned char uint8;    /* 8-bit unsigned integer */
#define MAXINT16 0xffff         /* Largest 16-bit integer */
#define MAXINT32 0x7fffffff     /* Largest 32-bit integer */

#define HASHMOD 7               /* Modulus used by hash_ip() function */

#ifdef	AX25_VJCOMP
/* Since not all compilers support structure assignment, the ASSIGN()
 * macro is used. This controls how it's actually implemented.
 */
#ifdef  NOSTRUCTASSIGN  /* Version for old compilers that don't support it */
#define ASSIGN(a,b)     memcpy((char *)&(a),(char *)&(b),sizeof(b));
#else                   /* Version for compilers that do */
#define ASSIGN(a,b)     ((a) = (b))
#endif
#endif

/* standard boolean constants */
#define FALSE 0
#define TRUE 1
#define NO 0
#define YES 1

/* Extract a short from a long */
#ifndef hiword
#define hiword(x)       ((uint16)((x) >> 16))
#define loword(x)       ((uint16)(x))
#endif

/* Extract a byte from a short */
#ifndef hibyte
#define hibyte(x)       ((unsigned char)((x) >> 8))
#define lobyte(x)       ((unsigned char)(x))
#endif

/* Extract nibbles from a byte */
#define hinibble(x)     (((x) >> 4) & 0xf)
#define lonibble(x)     ((x) & 0xf)

/* Various low-level and miscellaneous functions */
void *callocw(unsigned nelem,unsigned size);
#define FREE(p)         {free(p); p = NULL;}
int htob(char c);
int htoi(char *);
int readhex(uint8 *,char *,int);
long htol(char *);
uint hash_ip(int32 addr);
void logmsg(void *tcb,const char *fmt,const char *arg);
int ilog2(uint x);
#define ltop(x) ((void *) (x))
void *mallocw(unsigned nb);
int memcnt(const uint8 *buf,uint8 c,int size);
void memxor(uint8 *,uint8 *,unsigned int);
void rip(char *);
char *smsg(char *msgs[],unsigned nmsgs,unsigned n);

int stricmp(char *s1, char *s2);
int strnicmp(char *s1, char *s2, size_t maxlen);

/* General purpose function macros */
#ifndef min
#define min(x,y)        ((x)<(y)?(x):(y))       /* Lesser of two args */
#endif
#ifndef max
#define max(x,y)        ((x)>(y)?(x):(y))       /* Greater of two args */
#endif

#define inet_ntoa Xinet_ntoa    /* Resolve name conflict */

/* Externals used by getopt */
extern int optind;
extern char *optarg;

/* System clock - count of ticks since startup */
extern int32 Clock;

/* Various useful strings */
extern char Badhost[];
extern char Nospace[];
extern char Notval[];
extern char *Hostname;
extern char *Version;
extern char Whitespace[];

struct asy;
struct ax25_cb;
struct iface;
struct mbuf;
struct session;
struct usock;

extern int Debug;
extern int Shortstatus;

/* Use functions in misc.c because some platforms are broken, eg 386BSD */
int Xtolower(int);
int Xtoupper(int);

#endif  /* _GLOBAL_H */
