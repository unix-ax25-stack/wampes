/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kernel.c,v 1.16 1993/09/19 16:21:02 deyke Exp $ */

/* Non pre-empting synchronization kernel, machine-independent portion
 * Copyright 1992 Phil Karn, KA9Q
 */
#if     defined(PROCLOG) || defined(PROCTRACE)
#include <stdio.h>
#endif
/* #include <dos.h> */
#include <setjmp.h>
#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "socket.h"
#include "daemon.h"
#include "hardware.h"

#if defined(__hpux) || defined(ULTRIX_RISC) || defined(macII)
#define setjmp          _setjmp
#define longjmp         _longjmp
#elif defined(_AIX)
#define setjmp          _setjmp
#define longjmp         aix_longjmp
#endif

#ifdef RISCiX
void setstack(uint16 *);
#else
void setstack(void);
#endif

#define DISABLE()
#define RESTORE()
#define giveup()        exit(0)
#define istate()        (1)
#define restore(x)

uint16 *newstackptr;

#ifdef  PROCLOG
FILE *proclog;
FILE *proctrace;
#endif
int Stkchk = 0;
struct proc *Curproc;           /* Currently running process */
struct proc *Rdytab;            /* Processes ready to run (not including curproc) */
struct proc *Waittab[PHASH];    /* Waiting process list */
struct proc *Susptab;           /* Suspended processes */
static struct mbuf *Killq;
struct ksig Ksig;

static void addproc(struct proc *entry);
static void delproc(struct proc *entry);

static void psig(void *event,int n);
static int procsigs(void);

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

/* Create a process descriptor for the main function. Must be actually
 * called from the main function, and must be called before any other
 * tasking functions are called!
 *
 * Note that standard I/O is NOT set up here.
 */
struct proc *
mainproc(name)
char *name;
{
	register struct proc *pp;

	/* Create process descriptor */
	pp = (struct proc *)callocw(1,sizeof(struct proc));

	/* Create name */
	pp->name = strdup(name);
#ifndef AMIGA
	pp->stksize = 0;
#else
	init_psetup(pp);
#endif
	/* Make current */
	pp->state = READY;
	Curproc = pp;

#ifdef  PROCLOG
	proclog = fopen("proclog",APPEND_TEXT);
	proctrace = fopen("proctrace",APPEND_TEXT);
#endif
	return pp;
}
/* Create a new, ready process and return pointer to descriptor.
 * The general registers are not initialized, but optional args are pushed
 * on the stack so they can be seen by a C function.
 */
#pragma OPTIMIZE OFF
struct proc *
newproc(name,stksize,pc,iarg,parg1,parg2,freeargs)
char *name;             /* Arbitrary user-assigned name string */
unsigned int stksize;   /* Stack size in words to allocate */
void (*pc)();           /* Initial execution address */
int iarg;               /* Integer argument (argc) */
void *parg1;            /* Generic pointer argument #1 (argv) */
void *parg2;            /* Generic pointer argument #2 (session ptr) */
int freeargs;           /* If set, free arg list on parg1 at termination */
{
	static struct proc *pp;
	static void (*func)();
	jmp_buf jmpenv;
	int i;

	if(Stkchk)
		chkstk();

	/* Create process descriptor */
	pp = (struct proc *)callocw(1,sizeof(struct proc));

	/* Create name */
	pp->name = strdup(name);

	/* Allocate stack */
#ifdef  AMIGA
	stksize += SIGQSIZE0;   /* DOS overhead */
#endif
	stksize = (stksize + 3) & ~3;
	pp->stksize = stksize;
	if((pp->stack = (uint16 *)malloc(sizeof(uint16)*stksize)) == NULL){
		free(pp->name);
		free((char *)pp);
		return NULLPROC;
	}
	/* Initialize stack for high-water check */
	for(i=0;i<stksize;i++)
		pp->stack[i] = STACKPAT;

#if 0
	/* Do machine-dependent initialization of stack */
	psetup(pp,iarg,parg1,parg2,pc);
#endif

	if(freeargs)
		pp->flags |= P_FREEARGS;
	pp->iarg = iarg;
	pp->parg1 = parg1;
	pp->parg2 = parg2;

#if 0
	/* Inherit creator's input and output sockets */
	pp->input = fdup(stdin);
	pp->output = fdup(stdout);
#endif

	pp->state = READY;

	if (setjmp(Curproc->env))
		return pp;

	addproc(Curproc);
	Curproc = pp;

	func = pc;
#ifdef __hp9000s800
	newstackptr = pp->stack + 128;
#else
	newstackptr = pp->stack + (pp->stksize - 128);
#endif
#ifdef _AIX
	if (!setjmp(jmpenv)) {
	  jmpenv[3] = (int) newstackptr;
	  longjmp(jmpenv, 1);
	}
#elif sun
#if _JBLEN == 9
	if (!setjmp(jmpenv)) {
	  jmpenv[2] = (int) newstackptr;
	  longjmp(jmpenv, 1);
	}
#elif _JBLEN == 12
	if (!setjmp(jmpenv)) {
	  jmpenv[1] = (int) newstackptr;
	  longjmp(jmpenv, 1);
	}
#else
#error error: unknown jmp_buf size
#endif
#elif ULTRIX_RISC
	if (!setjmp(jmpenv)) {
	  jmpenv[32] = (int) newstackptr;
	  longjmp(jmpenv, 1);
	}
#elif macII
	if (!setjmp(jmpenv)) {
	  jmpenv[12] = (int) newstackptr;
	  longjmp(jmpenv, 1);
	}
#elif RISCiX
	setstack(newstackptr);
#else
	setstack();
#endif
	(*func)(pp->iarg, pp->parg1, pp->parg2);
	killself();
	return 0;
}
#pragma OPTIMIZE ON

/* Free resources allocated to specified process. If a process wants to kill
 * itself, the reaper is called to do the dirty work. This avoids some
 * messy situations that would otherwise occur, like freeing your own stack.
 */
void
killproc(pp)
register struct proc *pp;
{
	char **argv;

	if(pp == NULLPROC)
		return;
	/* Don't check the stack here! Will cause infinite recursion if
	 * called from a stack error
	 */
	if(pp == Curproc)
		killself();     /* Doesn't return */
#if 0
	fclose(pp->input);
	fclose(pp->output);
#endif

	/* Stop alarm clock in case it's running */
	stop_timer(&pp->alarm);

	/* Alert everyone waiting for this proc to die */
	Xpsignal(pp,0);

	/* Remove from appropriate table */
	delproc(pp);

#ifdef  PROCLOG
	fprintf(proclog,"id %lx name %s stack %u/%u\n",ptol(pp),
		pp->name,stkutil(pp),pp->stksize);
	fclose(proclog);
	proclog = fopen("proclog",APPEND_TEXT);
	proctrace = fopen("proctrace",APPEND_TEXT);
#endif
	/* Free allocated memory resources */
	if(pp->flags & P_FREEARGS){
		argv = pp->parg1;
		while(pp->iarg-- != 0)
			free(*argv++);
		free(pp->parg1);
	}
	free(pp->name);
	free(pp->stack);
	free((char *)pp);
}
/* Terminate current process by sending a request to the killer process.
 * Automatically called when a process function returns. Does not return.
 */
void
killself()
{
	register struct mbuf *bp;

	bp = pushdown(NULLBUF,sizeof(Curproc));
	memcpy(bp->data,(char *)&Curproc,sizeof(Curproc));
	enqueue(&Killq,bp);

	/* "Wait for me; I will be merciful and quick." */
	for(;;)
		pwait(NULL);
}
/* Process used by processes that want to kill themselves */
void
killer(i,v1,v2)
int i;
void *v1;
void *v2;
{
	struct proc *pp;
	struct mbuf *bp;

	for(;;){
		while(Killq == NULLBUF)
			pwait(&Killq);
		bp = dequeue(&Killq);
		pullup(&bp,(char *)&pp,sizeof(pp));
		free_p(bp);
		if(pp != Curproc)       /* We're immortal */
			killproc(pp);
	}
}

/* Inhibit a process from running */
void
suspend(pp)
struct proc *pp;
{
	if(pp == NULLPROC)
		return;
	if(pp != Curproc)
		delproc(pp);    /* Running process isn't on any list */
	pp->state |= SUSPEND;
	if(pp != Curproc)
		addproc(pp);    /* pwait will do it for us */
	else
		pwait(NULL);
}
/* Restart suspended process */
void
resume(pp)
struct proc *pp;
{
	if(pp == NULLPROC)
		return;
	delproc(pp);    /* Can't be Curproc! */
	pp->state &= ~SUSPEND;
	addproc(pp);
}

/* Wakeup waiting process, regardless of event it's waiting for. The process
 * will see a return value of "val" from its pwait() call. Must not be
 * called from an interrupt handler.
 */
void
alert(pp,val)
struct proc *pp;
int val;
{
	if(pp == NULLPROC)
		return;
#ifdef  notdef
	if((pp->state & WAITING) == 0)
		return;
#endif
#ifdef  PROCTRACE
	log(-1,"alert(%lx,%u) [%s]",ptol(pp),val,pp->name);
#endif
	if(pp != Curproc)
		delproc(pp);
	pp->state &= ~WAITING;
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
 * Pwait() returns 0 if the event was signaled; otherwise it returns the
 * arg in an alert() call. Pwait must not be called from interrupt level.
 *
 * Before waiting and after giving up the CPU, pwait() processes the signal
 * queue containing events signaled when interrupts were off. This means
 * the process queues are no longer modified by interrupt handlers,
 * so it is no longer necessary to run with interrupts disabled here. This
 * greatly improves interrupt latencies.
 */
#pragma OPTIMIZE OFF
int
pwait(event)
void *event;
{
	register struct proc *oldproc;
	int tmp;
#if 0
	int i_state;

	if(!istate()){
		stktrace();
	}
#endif
	Ksig.pwaits++;
#if 0
	if(intcontext() == 1){
		/* Pwait must not be called from interrupt context */
		Ksig.pwaitints++;
		return 0;
	}
	/* Enable interrupts, after saving the current state.
	 * This minimizes interrupt latency since we may have a lot
	 * of work to do. This seems safe, since care has been taken
	 * here to ensure that signals from interrupt level are not lost, e.g.,
	 * if we're waiting on an event, we post it before we scan the
	 * signal queue.
	 */
	i_state = istate();
	enable();
#endif
	if(Stkchk)
		chkstk();

	if(event != NULL){
		/* Post a wait for the specified event */
		Curproc->event = event;
		Curproc->state = WAITING;
		addproc(Curproc);       /* Put us on the wait list */
	}
	/* If the signal queue contains a signal for the event that we're
	 * waiting for, this will wake us back up
	 */
	procsigs();
	if(event == NULL){
		/* We remain runnable */
		if(Rdytab == NULLPROC){
			/* Nothing else is ready, so just return */
			Ksig.pwaitnops++;
			restore(i_state);
			return 0;
		}
		addproc(Curproc); /* Put us on the end of the ready list */
	}
	/* Look for a ready process and run it. If there are none,
	 * loop or halt until an interrupt makes something ready.
	 */
	while(Rdytab == NULLPROC){
		/* Give system back to upper-level multitasker, if any.
		 * Note that this function enables interrupts internally
		 * to prevent deadlock, but it restores our state
		 * before returning.
		 */
		giveup();
		/* Process signals that occurred during the giveup() */
		procsigs();
	}
	/* Remove first entry from ready list */
	oldproc = Curproc;
	Curproc = Rdytab;
	delproc(Curproc);

	/* Now do the context switch.
	 * This technique was inspired by Rob, PE1CHL, and is a bit tricky.
	 *
	 * First save the current process's state. Then if
	 * this is still the old process, load the new environment. Since the
	 * new task will "think" it's returning from the setjmp() with a return
	 * value of 1, the comparison with 0 will bypass the longjmp(), which
	 * would otherwise cause an infinite loop.
	 */
#ifdef  PROCTRACE
	if(strcmp(oldproc->name,Curproc->name) != 0){
		log(-1,"-> %s(%d)",Curproc->name,!!(Curproc->flags & P_ISTATE));
	}
#endif
#if 0
	/* Save old state */
	oldproc->flags &= ~P_ISTATE;
	if(i_state)
		oldproc->flags |= P_ISTATE;
#endif
	if(setjmp(oldproc->env) == 0){
		/* We're still running in the old task; load new task context.
		 * The interrupt state is restored here in case longjmp
		 * doesn't do it (e.g., systems other than Turbo-C).
		 */
		restore(Curproc->flags & P_ISTATE);
		longjmp(Curproc->env,1);
	}
	/* At this point, we're running in the newly dispatched task */
	tmp = Curproc->retval;
	Curproc->retval = 0;

	/* Also restore the true interrupt state here, in case the longjmp
	 * DOES restore the interrupt state saved at the time of the setjmp().
	 * This is the case with Turbo-C's setjmp/longjmp.
	 */
	restore(Curproc->flags & P_ISTATE);

	/* If an exception signal was sent and we're prepared, take it */
	if((Curproc->flags & P_SSET) && tmp == Curproc->signo)
		longjmp(Curproc->sig,1);

	/* Otherwise return normally to the new task */
	return tmp;
}
#pragma OPTIMIZE ON

void
Xpsignal(event,n)
void *event;
int n;
{
	static void *lastevent;

	if(istate()){
		/* Interrupts are on, just call psig directly after
		 * processing the previously queued signals
		 */
		procsigs();
		psig(event,n);
		return;
	}
	/* Interrupts are off, so quickly queue event */
	Ksig.psigsqueued++;

	/* Ignore duplicate signals to protect against a mad device driver
	 * overflowing the signal queue
	 */
	if(event == lastevent && Ksig.nentries != 0){
		Ksig.dupsigs++;
		return;
	}
	if(Ksig.nentries == SIGQSIZE){
		/* It's hard to handle this gracefully */
		Ksig.lostsigs++;
		return;
	}
	lastevent = Ksig.wp->event = event;
	Ksig.wp->n = n;
	if(++Ksig.wp >= &Ksig.entry[SIGQSIZE])
		Ksig.wp = Ksig.entry;
	Ksig.nentries++;
}
static int
procsigs()
{
	int cnt = 0;
	int tmp;

	for(;;){
		/* Atomic read and decrement of entry count */
		DISABLE();
		tmp = Ksig.nentries;
		if(tmp != 0)
			Ksig.nentries--;
		RESTORE();
		if(tmp == 0)
			break;
		psig(Ksig.rp->event,Ksig.rp->n);
		if(++Ksig.rp >= &Ksig.entry[SIGQSIZE])
			Ksig.rp = Ksig.entry;
		cnt++;
	}
	if(cnt > Ksig.maxentries)
		Ksig.maxentries = cnt;  /* Record high water mark */
	return cnt;
}
/* Make ready the first 'n' processes waiting for a given event. The ready
 * processes will see a return value of 0 from pwait().  Note that they don't
 * actually get control until we explicitly give up the CPU ourselves through
 * a pwait(). psig is now called from pwait, which is never called at
 * interrupt time, so it is no longer necessary to protect the proc queues
 * against interrupts. This also helps interrupt latencies considerably.
 */
static void
psig(event,n)
void *event;    /* Event to signal */
int n;          /* Max number of processes to wake up */
{
	register struct proc *pp;
	struct proc *pnext;
	unsigned int hashval;
	int cnt = 0;

	Ksig.psigs++;
	if(Stkchk)
		chkstk();

	if(event == NULL){
		Ksig.psignops++;
		return;         /* Null events are invalid */
	}
	/* n == 0 means "signal everybody waiting for this event" */
	if(n == 0)
		n = 65535;

	hashval = phash(event);
	for(pp = Waittab[hashval];n != 0 && pp != NULLPROC;pp = pnext){
		pnext = pp->next;
		if(pp->event == event){
#ifdef  PROCTRACE
				log(-1,"psignal(%lx,%u) wake %lx [%s]",ptol(event),n,
				 ptol(pp),pp->name);
#endif
			delproc(pp);
			pp->state &= ~WAITING;
			pp->retval = 0;
			pp->event = NULL;
			addproc(pp);
			n--;
			cnt++;
		}
	}
	for(pp = Susptab;n != 0 && pp != NULLPROC;pp = pnext){
		pnext = pp->next;
		if(pp->event == event){
#ifdef  PROCTRACE
				log(-1,"psignal(%lx,%u) wake %lx [%s]",ptol(event),n,
				 ptol(pp),pp->name);
#endif /* PROCTRACE */
			delproc(pp);
			pp->state &= ~WAITING;
			pp->event = 0;
			pp->retval = 0;
			addproc(pp);
			n--;
			cnt++;
		}
	}
	if(cnt == 0)
		Ksig.psignops++;
	else
		Ksig.psigwakes += cnt;
}

/* Rename a process */
void
chname(pp,newname)
struct proc *pp;
char *newname;
{
	free(pp->name);
	pp->name = strdup(newname);
}
/* Remove a process entry from the appropriate table */
static void
delproc(entry)
register struct proc *entry;    /* Pointer to entry */
{
	if(entry == NULLPROC)
		return;

	if(entry->next != NULLPROC)
		entry->next->prev = entry->prev;
	if(entry->prev != NULLPROC){
		entry->prev->next = entry->next;
	} else {
		switch(entry->state){
		case READY:
			Rdytab = entry->next;
			break;
		case WAITING:
			Waittab[phash(entry->event)] = entry->next;
			break;
		case SUSPEND:
		case SUSPEND|WAITING:
			Susptab = entry->next;
			break;
		}
	}
}
/* Append proc entry to end of appropriate list */
static void
addproc(entry)
register struct proc *entry;    /* Pointer to entry */
{
	register struct proc *pp;
	struct proc **head;

	if(entry == NULLPROC)
		return;

	switch(entry->state){
	case READY:
		head = &Rdytab;
		break;
	case WAITING:
		head = &Waittab[phash(entry->event)];
		break;
	case SUSPEND:
	case SUSPEND|WAITING:
		head = &Susptab;
		break;
	}
	entry->next = NULLPROC;
	if(*head == NULLPROC){
		/* Empty list, stick at beginning */
		entry->prev = NULLPROC;
		*head = entry;
	} else {
		/* Find last entry on list */
		for(pp = *head;pp->next != NULLPROC;pp = pp->next)
			;
		pp->next = entry;
		entry->prev = pp;
	}
}
