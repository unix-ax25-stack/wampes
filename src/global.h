/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/global.h,v 1.15 1992/05/14 13:20:02 deyke Exp $ */

#ifndef _GLOBAL_H
#define _GLOBAL_H

/* Global definitions used by every source file.
 * Some may be compiler dependent.
 */

#if     defined(__TURBOC__) || defined(__STDC__) || defined(LATTICE)
#define ANSIPROTO       1
#else
#define const
#endif

#ifndef __ARGS
#ifdef  ANSIPROTO
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#endif
#endif

#if     !defined(AMIGA) && (defined(LATTICE) || defined(MAC) || defined(__TURBOC__))
/* These compilers require special open modes when reading binary files.
 *
 * "The single most brilliant design decision in all of UNIX was the
 * choice of a SINGLE character as the end-of-line indicator" -- M. O'Dell
 *
 * "Whoever picked the end-of-line conventions for MS-DOS and the Macintosh
 * should be shot!" -- P. Karn's corollary to O'Dells' declaration
 */
#define READ_BINARY     "rb"
#define WRITE_BINARY    "wb"
#define APPEND_BINARY   "ab+"
#define READ_TEXT       "rt"
#define WRITE_TEXT      "wt"
#define APPEND_TEXT     "at+"

#else

#define READ_BINARY     "r"
#define WRITE_BINARY    "w"
#define APPEND_BINARY   "a+"
#define READ_TEXT       "r"
#define WRITE_TEXT      "w"
#define APPEND_TEXT     "a+"

#endif

/* These two lines assume that your compiler's longs are 32 bits and
 * shorts are 16 bits. It is already assumed that chars are 8 bits,
 * but it doesn't matter if they're signed or unsigned.
 */
typedef int int32;              /* 32-bit signed integer */
typedef unsigned short int16;   /* 16-bit unsigned integer */
typedef unsigned char byte_t;   /*  8-bit unsigned integer */
#define uchar(x) ((x) & 0xff)
#define MAXINT16 65535          /* Largest 16-bit integer */
#define MAXINT32 4294967295L    /* Largest 32-bit integer */

#define HASHMOD 7               /* Modulus used by hash_ip() function */

/* The "interrupt" keyword is non-standard, so make it configurable */
#if     defined(__TURBOC__) && defined(MSDOS)
#define INTERRUPT       void interrupt
#else
#define INTERRUPT       void
#endif

/* Note that these definitions are on by default if none of the Turbo-C style
 * memory model definitions are on; this avoids having to change them when
 * porting to 68K environments.
 */
#if     !defined(__TINY__) && !defined(__SMALL__) && !defined(__MEDIUM__)
#define LARGEDATA       1
#endif

#if     !defined(__TINY__) && !defined(__SMALL__) && !defined(__COMPACT__)
#define LARGECODE       1
#endif

/* Since not all compilers support structure assignment, the ASSIGN()
 * macro is used. This controls how it's actually implemented.
 */
#ifdef  NOSTRUCTASSIGN  /* Version for old compilers that don't support it */
#define ASSIGN(a,b)     memcpy((char *)&(a),(char *)&(b),sizeof(b));
#else                   /* Version for compilers that do */
#define ASSIGN(a,b)     ((a) = (b))
#endif

/* Define null object pointer in case stdio.h isn't included */
#ifndef NULL
/* General purpose NULL pointer */
#define NULL (void *)0
#endif
#define NULLCHAR (char *)0      /* Null character pointer */
#define NULLCHARP (char **)0    /* Null character pointer pointer */
#define NULLINT (int *)0        /* Null integer pointer */
#define NULLFP   (int (*)())0   /* Null pointer to function returning int */
#define NULLVFP  (void (*)())0  /* Null pointer to function returning void */
#define NULLVIFP (INTERRUPT (*)())0
#define NULLFILE (FILE *)0      /* Null file pointer */

/* standard boolean constants */
#define FALSE 0
#define TRUE 1
#define NO 0
#define YES 1

/* string equality shorthand */
#define STREQ(x,y) (strcmp(x,y) == 0)

/* Extract a short from a long */
#define hiword(x)       ((int16)((x) >> 16))
#define loword(x)       ((int16)(x))

/* Extract a byte from a short */
#define hibyte(x)       ((unsigned char)((x) >> 8))
#define lobyte(x)       ((unsigned char)(x))

/* Extract nibbles from a byte */
#define hinibble(x)     (((x) >> 4) & 0xf)
#define lonibble(x)     ((x) & 0xf)

/* Various low-level and miscellaneous functions */
unsigned long availmem __ARGS((void));
void *callocw __ARGS((unsigned nelem,unsigned size));
int dirps __ARGS((void));
int getopt();
int htoi __ARGS((char *));
long htol __ARGS((char *));
char *inbuf __ARGS((int16 port,char *buf,int16 cnt));
int16 hash_ip __ARGS((int32 addr));
int istate __ARGS((void));
void log();
int log2 __ARGS((int x));
#define ltop(x) ((void *) (x))
void *mallocw __ARGS((unsigned nb));
int memcnt __ARGS((char *buf,char c,int size));
char *outbuf __ARGS((int16 port,char *buf,int16 cnt));
#define ptol(x) ((long) (x))
void restore __ARGS((int));
void rip __ARGS((char *));
char *smsg __ARGS((char *msgs[],unsigned nmsgs,unsigned n));
void stktrace __ARGS((void));
#if     !defined __TURBOC__
char *strdup();
#endif
int wildmat __ARGS((char *s,char *p,char **argv));

#include <stdlib.h>
#include <string.h>

int stricmp __ARGS((char *s1, char *s2));
int strnicmp __ARGS((char *s1, char *s2, size_t maxlen));

#ifdef  AZTEC
#define rewind(fp)      fseek(fp,0L,0);
#endif

#if     defined(__TURBOC__) && defined(MSDOS)
#define movblock(so,ss,do,ds,c) movedata(ss,so,ds,do,c)

#else

/* General purpose function macros already defined in turbo C */
#ifndef min
#define min(x,y)        ((x)<(y)?(x):(y))       /* Lesser of two args */
#endif
#ifndef max
#define max(x,y)        ((x)>(y)?(x):(y))       /* Greater of two args */
#endif
#ifdef  MSDOS
#define MK_FP(seg,ofs)  ((void far *) \
			(((unsigned long)(seg) << 16) | (unsigned)(ofs)))
#endif
#endif  /* __TURBOC __ */

#ifdef  AMIGA
/* super kludge de WA3YMH */
#ifndef fileno
#include <stdio.h>
#endif
#define fclose(fp)      amiga_fclose(fp)
extern int amiga_fclose __ARGS((FILE *));
extern FILE *tmpfile __ARGS((void));

extern char *sys_errlist[];
extern int errno;
#endif

/* Externals used by getopt */
extern int optind;
extern char *optarg;

/* Threshold setting on available memory */
extern int32 Memthresh;

/* System clock - count of ticks since startup */
extern int32 Clock;

/* Various useful standard error messages */
extern char Badhost[];
extern char Nospace[];
extern char Notval[];
extern char *Hostname;
extern char Version[];

/* Your system's end-of-line convention */
extern char Eol[];

/* Your system's temp directory */
extern char *Tmpdir;

extern void (*Gcollect[])();

struct ax25_cb;
struct dirsort;
struct iface;
struct ip;
struct mbx;
struct mbuf;
struct nr4cb;
struct session;
struct slip;

extern int Debug;
extern int Shortstatus;

#endif  /* _GLOBAL_H */
