/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/global.h,v 1.4 1990/09/11 13:45:23 deyke Exp $ */

#ifndef MAXINT16

/* Global definitions used by every source file.
 * Some may be compiler dependent.
 */

#if     defined(__TURBOC__) || defined(__STDC__) || defined(LATTICE)
#define ANSIPROTO       1
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
#define uchar(x) ((unsigned char)(x))
#define MAXINT16 65535          /* Largest 16-bit integer */
#define MAXINT32 4294967295L    /* Largest 32-bit integer */

/* The "interrupt" keyword is non-standard, so make it configurable */
#ifdef  __TURBOC__
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
void freeargs __ARGS((int argc,char *argv[]));
int getopt __ARGS((int argc,char *argv[],char *opts));
int htoi __ARGS((char *));
long htol __ARGS((char *));
char *inbuf __ARGS((int16 port,char *buf,int16 cnt));
int istate __ARGS((void));
/* void log __ARGS((struct tcb *tcb,char *fmt, ...)); */
void log();
void *ltop __ARGS((long));
void *mallocw __ARGS((unsigned nb));
char *outbuf __ARGS((int16 port,char *buf,int16 cnt));
/* long ptol __ARGS((void *)); */
#define ptol(x) ((long) x)
void restore __ARGS((int));
void rip __ARGS((char *));
char *smsg __ARGS((char *msgs[],unsigned nmsgs,unsigned n));
int tprintf __ARGS((char *fmt,...));
#if     !defined __TURBOC__
char *strdup __ARGS((char *));
#endif
int wildmat __ARGS((char *s,char *p,char **argv));

#define tprintf printf
#include <sys/types.h>          /* for HP-UX 6.5 compatibility */
#define _SIZE_T                 /* for HP-UX 6.5 compatibility */
#include <stdlib.h>
#include <string.h>

#ifdef  AZTEC
#define rewind(fp)      fseek(fp,0L,0);
#endif

#ifdef  __TURBOC__
#define movblock(so,ss,do,ds,c) movedata(ss,so,ds,do,c)
#define outportw outport
#define inportw inport

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

/* Various useful standard error messages */
extern char Badhost[];
extern char Nospace[];
extern char Notval[];
extern char *Hostname;
extern char Version[];

/* Your system's end-of-line convention */
extern char Eol[];

extern void (*Gcollect[])();

#endif  /* MAXINT16 */

