/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/proc.h,v 1.2 1991/10/11 18:56:34 deyke Exp $ */

#ifndef _PROC_H
#define _PROC_H

#include <setjmp.h>

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

#define OUTBUFSIZE      512     /* Size to be malloc'ed for outbuf */

/* Kernel process control block */
#define PHASH   16              /* Number of wait table hash chains */
struct proc {
	struct proc *prev;      /* Process table pointers */
	struct proc *next;

	jmp_buf env;            /* Process state */
	char i_state;           /* Process interrupt state */

	unsigned short state;
#define READY   0
#define WAITING 1
#define SUSPEND 2
	void *event;            /* Wait event */
	int16 *stack;           /* Process stack */
	unsigned stksize;       /* Size of same */
	char *name;             /* Arbitrary user-assigned name */
	int retval;             /* Return value from next pwait() */
	struct timer alarm;     /* Alarm clock timer */
	struct mbuf *outbuf;    /* Terminal output buffer */
	int input;              /* standard input socket */
	int output;             /* standard output socket */
	int iarg;               /* Copy of iarg */
	void *parg1;            /* Copy of parg1 */
	void *parg2;            /* Copy of parg2 */
	int freeargs;           /* Free args on termination if set */
};
#define NULLPROC (struct proc *)0
extern struct proc *Waittab[];  /* Head of wait list */
extern struct proc *Rdytab;     /* Head of ready list */
extern struct proc *Curproc;    /* Currently running process */
extern struct proc *Susptab;    /* Suspended processes */
extern int Stkchk;              /* Stack checking flag */

/* In  kernel.c: */
void alert __ARGS((struct proc *pp,int val));
void chname __ARGS((struct proc *pp,char *newname));
void killproc __ARGS((struct proc *pp));
void killself __ARGS((void));
struct proc *mainproc __ARGS((char *name));
struct proc *newproc __ARGS((char *name,unsigned int stksize,
	void (*pc) __ARGS((int,void *,void *)),
	int iarg,void *parg1,void *parg2,int freeargs));
int psignal __ARGS((void *event,int n));
int pwait __ARGS((void *event));
void resume __ARGS((struct proc *pp));
void suspend __ARGS((struct proc *pp));

/* In ksubr.c: */
void chkstk __ARGS((void));
void kinit __ARGS((void));
unsigned phash __ARGS((void *event));
void psetup __ARGS((struct proc *pp,int iarg,void *parg1,void *parg2,
	void  __ARGS(((*pc) __ARGS((int,void *,void *)) )) ));
#ifdef  AMIGA
void init_psetup __ARGS((struct proc *pp));
#endif

/* Stack background fill value for high water mark checking */
#define STACKPAT        0x55aa

/* Value stashed in location 0 to detect null pointer dereferences */
#define NULLPAT         0xdead

#endif  /* _PROC_H */
