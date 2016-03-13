/* @(#) $Id: kernel.c,v 1.31 2016/03/13 14:44:58 dl9sau Exp $ */

/* Non pre-empting synchronization kernel, machine-independent portion
 * Copyright 1992 Phil Karn, KA9Q
 */

#ifdef HAS_UCONTEXT
#include <ucontext.h>
#else
#ifndef ibm032
#include <setjmp.h>
#endif
#endif
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "socket.h"
#include "daemon.h"

#ifndef HAS_UCONTEXT
#if defined __hpux || defined ULTRIX_RISC || defined macII
#define setjmp          _setjmp
#define longjmp         _longjmp
#elif defined _AIX
#define setjmp          _setjmp
#define longjmp         aix_longjmp
#endif

#ifdef RISCiX
EXTERN_C void setstack(uint16 *);
#else
EXTERN_C void setstack(void);
#endif

uint16 *newstackptr;
#endif

struct proc *Curproc;           /* Currently running process */
struct proc *Rdytab;            /* Processes ready to run (not including curproc) */
struct proc *Waittab[PHASH];    /* Waiting process list */
struct proc *Susptab;           /* Suspended processes */
struct mbuf *Killq;
struct ksig Ksig;
int Kdebug;             /* Control display of current task on screen */

static void addproc(struct proc *entry);
static void delproc(struct proc *entry);

static void ksig(void *event,int n);
static int procsigs(void);

#ifdef	HAS_UCONTEXT
static void (*func)(int, void *, void *);
static void trampoline(void)
{
	(*func)(Curproc->iarg, Curproc->parg1, Curproc->parg2);
	killself();
}
#else
#ifdef _AIX

#pragma alloca

static void aix_dummy_function(void *p)
{
}

static void aix_longjmp(jmp_buf jmpenv, int val)
{

#define LOW_STACK_SIZE 2000

  char local;
  static char low_stack[LOW_STACK_SIZE];

  aix_dummy_function(alloca(&local - (low_stack + LOW_STACK_SIZE / 2)));
  _longjmp(jmpenv, val);
}

#endif
#endif

/* Create a process descriptor for the main function. Must be actually
 * called from the main function, and must be called before any other
 * tasking functions are called!
 *
 * Note that standard I/O is NOT set up here.
 */
struct proc *
mainproc(char *name)
{
	struct proc *pp;

	/* Create process descriptor */
	pp = (struct proc *)callocw(1,sizeof(struct proc));

	/* Create name */
	pp->name = strdup(name);
	pp->stksize = 0;

	/* Make current */
	pp->flags.suspend = pp->flags.waiting = 0;
	Curproc = pp;

	return pp;
}
#ifndef SINGLE_THREADED
/* Create a new, ready process and return pointer to descriptor.
 * The general registers are not initialized, but optional args are pushed
 * on the stack so they can be seen by a C function.
 */
struct proc *
newproc(
char *name,             /* Arbitrary user-assigned name string */
unsigned int stksize,   /* Stack size in words to allocate */
void (*pc)(int,void *,void *),
			/* Initial execution address */
int iarg,               /* Integer argument (argc) */
void *parg1,            /* Generic pointer argument #1 (argv) */
void *parg2,            /* Generic pointer argument #2 (session ptr) */
int freeargs            /* If set, free arg list on parg1 at termination */
){
	static struct proc *pp;
#ifndef	HAS_UCONTEXT
	static void (*func)(int,void *,void *);
#endif
	struct proc *oldproc = Curproc;
	int i;

	/* Create process descriptor */
	pp = (struct proc *)callocw(1,sizeof(struct proc));

	/* Create name */
	pp->name = strdup(name);

 	/* Assign function name */
	func = pc;

	/* Allocate stack */
	pp->stksize = (stksize + 3) & ~3;
	if((pp->stack = (uint16 *)malloc(sizeof(uint16) * pp->stksize)) == NULL){
		free(pp->name);
		free(pp);
		return NULL;
	}
	/* Initialize stack for high-water check */
	for(i=0 ;i < pp->stksize; i++)
		pp->stack[i] = STACKPAT;

	pp->flags.freeargs = freeargs;
	pp->iarg = iarg;
	pp->parg1 = parg1;
	pp->parg2 = parg2;

	pp->flags.suspend = pp->flags.waiting = 0;

#ifndef	HAS_UCONTEXT
	if (setjmp(Curproc->env))
		return pp;
#endif

	addproc(Curproc);
	Curproc = pp;

#ifdef	HAS_UCONTEXT
	ucontext_t env;
	getcontext(&env);
	env.uc_stack.ss_sp = (void *)pp->stack;
	env.uc_stack.ss_size = sizeof(uint16) * pp->stksize;
	env.uc_link = 0;
	makecontext(&env, trampoline, 0);

	swapcontext(&oldproc->env, &env);
	return pp;

#else

#ifdef __hp9000s800
	newstackptr = pp->stack + 128;
#else
	newstackptr = pp->stack + (pp->stksize - 128);
#endif
#if defined _AIX
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[3] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif defined __sgi
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[JB_SP] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif defined sun
#if _JBLEN == 9 || _JBLEN == 58
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[2] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif _JBLEN == 12
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[1] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#else
#error error: unknown jmp_buf size
#endif
#elif defined ULTRIX_RISC
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[32] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif defined macII
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[12] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif defined ibm032
	{
	  jmp_buf jmpenv;
	  if (!setjmp(jmpenv)) {
	    jmpenv[0] = (int) newstackptr;
	    longjmp(jmpenv, 1);
	  }
	}
#elif defined RISCiX
	setstack(newstackptr);
#else
	setstack();
#endif
	(*func)(pp->iarg, pp->parg1, pp->parg2);
	killself();
	return 0;
#endif
}
#endif

/* Free resources allocated to specified process. If a process wants to kill
 * itself, the reaper is called to do the dirty work. This avoids some
 * messy situations that would otherwise occur, like freeing your own stack.
 */
void
killproc(struct proc **ppp)
{
	char **argv;
	struct proc *pp;

	if(ppp == NULL || (pp = *ppp) == NULL)
		return;

	*ppp = NULL;
	if(pp == Curproc)
		killself();     /* Doesn't return */

	/* Stop alarm clock in case it's running */
	stop_timer(&pp->alarm);

	/* Alert everyone waiting for this proc to die */
	ksignal(pp,0);

	/* Remove from appropriate table */
	delproc(pp);

	/* Free allocated memory resources */
	if(pp->flags.freeargs){
		argv = (char **) pp->parg1;
		while(pp->iarg-- != 0)
			free(*argv++);
		free(pp->parg1);
	}
	free(pp->name);
	free(pp->stack);
	free(pp);
	*ppp = NULL;
}
/* Terminate current process by sending a request to the killer process.
 * Automatically called when a process function returns. Does not return.
 */
void
killself(void)
{
	struct mbuf *bp = NULL;

	pushdown(&bp,&Curproc,sizeof(Curproc));
	enqueue(&Killq,&bp);

	/* "Wait for me; I will be merciful and quick." */
	for(;;)
		kwait(NULL);
}
/* Process used by processes that want to kill themselves */
void
killer(int i,void *v1,void *v2)
{
	struct proc *pp;
	struct mbuf *bp;

	for(;;){
		while(Killq == NULL)
			kwait(&Killq);
		bp = dequeue(&Killq);
		pullup(&bp,&pp,sizeof(pp));
		free_p(&bp);
		if(pp != Curproc)       /* We're immortal */
			killproc(&pp);
	}
}

/* Inhibit a process from running */
void
suspend(struct proc *pp)
{
	if(pp == NULL)
		return;
	if(pp != Curproc)
		delproc(pp);    /* Running process isn't on any list */
	pp->flags.suspend = 1;
	if(pp != Curproc)
		addproc(pp);    /* kwait will do it for us */
	else
		kwait(NULL);
}
/* Restart suspended process */
void
resume(struct proc *pp)
{
	if(pp == NULL)
		return;
	delproc(pp);    /* Can't be Curproc! */
	pp->flags.suspend = 0;
	addproc(pp);
}

/* Wakeup waiting process, regardless of event it's waiting for. The process
 * will see a return value of "val" from its kwait() call.
 */
void
alert(struct proc *pp,int val)
{
	if(pp == NULL)
		return;
	if(pp != Curproc)
		delproc(pp);
	pp->flags.waiting = 0;
	pp->retval = val;
	pp->event = NULL;
	if(pp != Curproc)
		addproc(pp);
}

/* Post a wait on a specified event and give up the CPU until it happens. The
 * null event is special: it means "I don't want to block on an event, but let
 * somebody else run for a while". It can also mean that the present process
 * is terminating; in this case the wait never returns.
 *
 * Kwait() returns 0 if the event was signaled; otherwise it returns the
 * arg in an alert() call.
 */
int
kwait(void *event)
{
	struct proc *oldproc;
	int tmp;

	Ksig.kwaits++;

	if(event != NULL){
		/* Post a wait for the specified event */
		Curproc->event = event;
		Curproc->flags.waiting = 1;
		addproc(Curproc);       /* Put us on the wait list */
	}
	/* If the signal queue contains a signal for the event that we're
	 * waiting for, this will wake us back up
	 */
	procsigs();
	if(event == NULL){
		/* We remain runnable */
		if(Rdytab == NULL){
			/* Nothing else is ready, so just return */
			Ksig.kwaitnops++;
			return 0;
		}
		addproc(Curproc); /* Put us on the end of the ready list */
	}
	/* Look for a ready process and run it. If there are none,
	 * exit.
	 */
	while(Rdytab == NULL){
		exit(0);
	}
	/* Remove first entry from ready list */
	oldproc = Curproc;
	Curproc = Rdytab;
	delproc(Curproc);

	/* Now do the context switch. */
#ifdef	HAS_UCONTEXT
	swapcontext(&oldproc->env, &Curproc->env);
#else
	/*
	 * This technique was inspired by Rob, PE1CHL, and is a bit tricky.
	 *
	 * First save the current process's state. Then if
	 * this is still the old process, load the new environment. Since the
	 * new task will "think" it's returning from the setjmp() with a return
	 * value of 1, the comparison with 0 will bypass the longjmp(), which
	 * would otherwise cause an infinite loop.
	 */
	if(setjmp(oldproc->env) == 0){
		/* We're still running in the old task; load new task context.
		 */
		longjmp(Curproc->env,1);
	}
#endif

	/* At this point, we're running in the newly dispatched task */
	tmp = Curproc->retval;
	Curproc->retval = 0;

#ifndef	HAS_UCONTEXT
	/* If an exception signal was sent and we're prepared, take it */
	if((Curproc->flags.sset) && tmp == Curproc->signo)
		longjmp(Curproc->sig,1);
#endif

	/* Otherwise return normally to the new task */
	return tmp;
}

void
ksignal(void *event,int n)
{
		procsigs();
		ksig(event,n);
}
static int
procsigs(void)
{
	int cnt = 0;
	int tmp;

	for(;;){
		/* Atomic read and decrement of entry count */
		tmp = Ksig.nentries;
		if(tmp != 0)
			Ksig.nentries--;
		if(tmp == 0)
			break;
		ksig(Ksig.rp->event,Ksig.rp->n);
		if(++Ksig.rp >= &Ksig.entry[SIGQSIZE])
			Ksig.rp = Ksig.entry;
		cnt++;
	}
	if(cnt > Ksig.maxentries)
		Ksig.maxentries = cnt;  /* Record high water mark */
	return cnt;
}
/* Make ready the first 'n' processes waiting for a given event. The ready
 * processes will see a return value of 0 from kwait().  Note that they don't
 * actually get control until we explicitly give up the CPU ourselves through
 * a kwait().
 */
static void
ksig(
void *event,    /* Event to signal */
int n           /* Max number of processes to wake up */
){
	struct proc *pp;
	struct proc *pnext;
	unsigned int hashval;
	int cnt = 0;

	Ksig.ksigs++;

	if(event == NULL){
		Ksig.ksignops++;
		return;         /* Null events are invalid */
	}
	/* n == 0 means "signal everybody waiting for this event" */
	if(n == 0)
		n = 65535;

	hashval = phash(event);
	for(pp = Waittab[hashval];n != 0 && pp != NULL;pp = pnext){
		pnext = pp->next;
		if(pp->event == event){
			delproc(pp);
			pp->flags.waiting = 0;
			pp->retval = 0;
			pp->event = NULL;
			addproc(pp);
			n--;
			cnt++;
		}
	}
	for(pp = Susptab;n != 0 && pp != NULL;pp = pnext){
		pnext = pp->next;
		if(pp->event == event){
			delproc(pp);
			pp->flags.waiting = 0;
			pp->event = 0;
			pp->retval = 0;
			addproc(pp);
			n--;
			cnt++;
		}
	}
	if(cnt == 0)
		Ksig.ksignops++;
	else
		Ksig.ksigwakes += cnt;
}

/* Rename a process */
void
chname(struct proc *pp,char *newname)
{
	free(pp->name);
	pp->name = strdup(newname);
}
/* Remove a process entry from the appropriate table */
static void
delproc(struct proc *entry)     /* Pointer to entry */
{
	if(entry == NULL)
		return;

	if(entry->next != NULL)
		entry->next->prev = entry->prev;
	if(entry->prev != NULL){
		entry->prev->next = entry->next;
	} else {
		if(entry->flags.suspend){
			Susptab = entry->next;
		} else if(entry->flags.waiting){
			Waittab[phash(entry->event)] = entry->next;
		} else {        /* Ready */
			Rdytab = entry->next;
		}
	}

	entry->prev = NULL;
	entry->next = NULL;
}
/* Append proc entry to end of appropriate list */
static void
addproc(struct proc *entry)     /* Pointer to entry */
{
	struct proc *pp;
#if	0
	struct proc **head;
#endif

	if(entry == NULL)
		return;

#if 0
	if(entry->flags.suspend){
		head = &Susptab;
	} else if(entry->flags.waiting){
		head = &Waittab[phash(entry->event)];
	} else {        /* Ready */
		head = &Rdytab;
	}
	entry->next = NULL;
	if(*head == NULL){
		/* Empty list, stick at beginning */
		entry->prev = NULL;
		*head = entry;
	} else {
		/* Find last entry on list */
		for(pp = *head;pp->next != NULL;pp = pp->next)
			;
		pp->next = entry;
		entry->prev = pp;
	}
#else
	if(entry->flags.suspend){
		if ((pp = Susptab) == NULL)
			Susptab = entry;
	} else if(entry->flags.waiting){
		if ((pp = Waittab[phash(entry->event)]) == NULL)
			Waittab[phash(entry->event)] = entry;
	} else {        /* Ready */
		if ((pp = Rdytab) == NULL)
			Rdytab = entry;
	}
	if (pp != NULL) {
		/* Find last entry on list */
		while (pp->next != NULL) pp = pp->next;
		pp->next = entry;
		entry->prev = pp;
	}
#endif
}
