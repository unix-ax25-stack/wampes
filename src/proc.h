/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/proc.h,v 1.5 1992/08/24 10:09:39 deyke Exp $ */

#ifndef _PROC_H
#define _PROC_H

#include <setjmp.h>

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

#define SIGQSIZE        200     /* Entries in psignal queue */

/* Kernel process control block */
#define PHASH   16              /* Number of wait table hash chains */
struct proc {
	struct proc *prev;      /* Process table pointers */
	struct proc *next;

	int flags;
#define P_ISTATE        1       /* Process has interrupts enabled */
#define P_SSET          2       /* Process has set sig */
#define P_FREEARGS      4       /* Free args on termination */

	jmp_buf env;            /* Process state */
	jmp_buf sig;            /* State for alert signal */
	int signo;              /* Arg to alert to cause signal */
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
	FILE *input;
	FILE *output;
	int iarg;               /* Copy of iarg */
	void *parg1;            /* Copy of parg1 */
	void *parg2;            /* Copy of parg2 */
};
#define NULLPROC (struct proc *)0
extern struct proc *Waittab[];  /* Head of wait list */
extern struct proc *Rdytab;     /* Head of ready list */
extern struct proc *Curproc;    /* Currently running process */
extern struct proc *Susptab;    /* Suspended processes */
extern int Stkchk;              /* Stack checking flag */

struct sigentry {
	void *event;
	int n;
};
struct ksig {
	struct sigentry entry[SIGQSIZE];
	struct sigentry *wp;
	struct sigentry *rp;
	volatile int nentries;  /* modified both by interrupts and main */
	int maxentries;
	int32 dupsigs;
	int lostsigs;
	int32 psigs;            /* Count of psignal calls */
	int32 psigwakes;        /* Processes woken */
	int32 psignops;         /* Psignal calls that didn't wake anything */
	int32 psigsqueued;      /* Psignal calls queued with ints off */
	int32 pwaits;           /* Count of pwait calls */
	int32 pwaitnops;        /* pwait calls that didn't block */
	int32 pwaitints;        /* Pwait calls from interrupt context (error) */
};
extern struct ksig Ksig;

/* Prepare for an exception signal and return 0. If after this macro
 * is executed any other process executes alert(pp,val), this will
 * invoke the exception and cause this macro to return a second time,
 * but with the return value 1. This cannot be a function since the stack
 * frame current at the time setjmp is called must still be current
 * at the time the signal is taken. Note use of comma operators to return
 * the value of setjmp as the overall macro expression value.
 */
#define SETSIG(val)     (Curproc->flags |= P_SSET,\
	Curproc->signo = (val),setjmp(Curproc->sig))

/* In  kernel.c: */
void alert __ARGS((struct proc *pp,int val));
void chname __ARGS((struct proc *pp,char *newname));
void killproc __ARGS((struct proc *pp));
void killself __ARGS((void));
struct proc *mainproc __ARGS((char *name));
struct proc *newproc __ARGS((char *name,unsigned int stksize,
	void (*pc) __ARGS((int,void *,void *)),
	int iarg,void *parg1,void *parg2,int freeargs));
#ifndef LINUX
 /* Linux: conflict with <signal.h> */
void psignal __ARGS((void *event,int n));
#endif
int pwait __ARGS((void *event));
void resume __ARGS((struct proc *pp));
int setsig __ARGS((int val));
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
