/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ksubr.c,v 1.5 1992/01/20 17:17:49 deyke Exp $ */

/* Machine or compiler-dependent portions of kernel
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <sys/types.h>
#include <stdio.h>
#include <time.h>
/* #include <dos.h> */
#include "global.h"
#include "proc.h"
/* #include "pc.h" */
#include "commands.h"

static oldNull;

#ifdef __TURBOC__
/* Template for contents of jmp_buf in Turbo C */
struct env {
	unsigned        sp;
	unsigned        ss;
	unsigned        flag;
	unsigned        cs;
	unsigned        ip;
	unsigned        bp;
	unsigned        di;
	unsigned        es;
	unsigned        si;
	unsigned        ds;
};
#define getstackptr(ep) (ptol(MK_FP((ep)->ss, (ep)->sp)))
#else
#ifdef __hp9000s300
struct env {
	long    pc;
	long    d2;
	long    d3;
	long    d4;
	long    d5;
	long    d6;
	long    d7;
	long    a2;
	long    a3;
	long    a4;
	long    a5;
	long    a6;
	long    a7;
};
#define getstackptr(ep) ((ep)->a7)
#else
#ifdef __hp9000s800
struct env {
	long    i_dont_know;
	long    sp;
};
#define getstackptr(ep) ((ep)->sp)
#else
#ifdef ISC
struct env {
	long    esp;
	long    ebp;
	long    eax;
	long    ebx;
	long    ecx;
	long    edx;
	long    edi;
	long    esi;
	long    eip;
	long    flag;
};
#define getstackptr(ep) ((ep)->esp)
#else
struct env {
	long    dummy;
};
#define getstackptr(ep) (0L)
#endif
#endif
#endif
#endif

static int stkutil __ARGS((struct proc *pp));

void
kinit()
{
	/* Remember location 0 pattern to detect null pointer derefs */
	oldNull = *(unsigned short *)NULL;

}
/* Print process table info
 * Since things can change while ps is running, the ready proceses are
 * displayed last. This is because an interrupt can make a process ready,
 * but a ready process won't spontaneously become unready. Therefore a
 * process that changes during ps may show up twice, but this is better
 * than not having it showing up at all.
 */
int
ps(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct proc *pp;
	register struct env *ep;
	int i;

	extern time_t StartTime;

	tprintf("Uptime %s",tformat(secclock()-StartTime));
	tprintf("\n");

	tprintf("PID       SP        stksize   maxstk    event     fl  in  out  name\n");

	for(pp = Susptab;pp != NULLPROC;pp = pp->next){
		ep = (struct env *)&pp->env;
		if(tprintf("%08lx  %08lx  %-10u%-10u%08lx  %c%c%c %3d %3d  %s\n",
		 ptol(pp),
		 getstackptr(ep),
		 pp->stksize,
		 stkutil(pp),
		 ptol(pp->event),
		 pp->i_state ? 'I' : ' ',
		 (pp->state & WAITING) ? 'W' : ' ',
		 (pp->state & SUSPEND) ? 'S' : ' ',
		 pp->input, pp->output,
		 pp->name) == EOF)
			return 0;
	}
	for(i=0;i<PHASH;i++){
		for(pp = Waittab[i];pp != NULLPROC;pp = pp->next){
			ep = (struct env *)&pp->env;
			if(tprintf("%08lx  %08lx  %-10u%-10u%08lx  %c%c%c %3d %3d  %s\n",
			 ptol(pp),getstackptr(ep),pp->stksize,stkutil(pp),
			 ptol(pp->event),
			 pp->i_state ? 'I' : ' ',
			 (pp->state & WAITING) ? 'W' : ' ',
			 (pp->state & SUSPEND) ? 'S' : ' ',
			 pp->input,pp->output,
			 pp->name) == EOF)
				return 0;
		}
	}
	for(pp = Rdytab;pp != NULLPROC;pp = pp->next){
		ep = (struct env *)&pp->env;
		if(tprintf("%08lx  %08lx  %-10u%-10u          %c%c%c %3d %3d  %s\n",
		 ptol(pp),getstackptr(ep),pp->stksize,stkutil(pp),
		 pp->i_state ? 'I' : ' ',
		 (pp->state & WAITING) ? 'W' : ' ',
		 (pp->state & SUSPEND) ? 'S' : ' ',
		 pp->input,pp->output,
		 pp->name) == EOF)
			return 0;
	}
	if(Curproc != NULLPROC){
		ep = (struct env *)&Curproc->env;
		tprintf("%08lx  %08lx  %-10u%-10u          %c   %3d %3d  %s\n",
		 ptol(Curproc),getstackptr(ep),Curproc->stksize,
		 stkutil(Curproc),
		 Curproc->i_state ? 'I' : ' ',
		 Curproc->input,Curproc->output,
		 Curproc->name);
	}
	return 0;
}
static int
stkutil(pp)
struct proc *pp;
{
	unsigned i;
	register int16 *sp;

	if(pp->stksize == 0)
		return 0;       /* Main task -- too hard to check */
	i = pp->stksize;
#ifndef __hp9000s800
	for(sp = pp->stack;*sp == STACKPAT && sp < pp->stack + pp->stksize;sp++)
#else
	for(sp = pp->stack + (pp->stksize-1);*sp == STACKPAT && sp >= pp->stack;sp--)
#endif
		i--;
	return i;
}

/* Verify that stack pointer for current process is within legal limits;
 * also check that no one has dereferenced a null pointer
 */
void
chkstk()
{
	int16 *sbase;
	int16 *stop;
	int16 *sp;

#ifdef __TURBOC__
	sp = MK_FP(_SS,_SP);
	if(_SS == _DS){
		/* Probably in interrupt context */
		return;
	}
#else
	sp = (int16 *) &sp;
#endif
	sbase = Curproc->stack;
	if(sbase == NULL)
		return; /* Main task -- too hard to check */

	stop = sbase + Curproc->stksize;
	if(sp < sbase || sp >= stop){
		printf("Stack violation, process %s\n",Curproc->name);
		printf("SP = %lx, legal stack range [%lx,%lx)\n",
		ptol(sp),ptol(sbase),ptol(stop));
		fflush(stdout);
		killself();
	}
	if(*(unsigned short *)NULL != oldNull){
		printf("WARNING: Location 0 smashed, process %s\n",Curproc->name);
		*(unsigned short *)NULL = oldNull;
		fflush(stdout);
	}
}
unsigned
phash(event)
void *event;
{
	register unsigned x;

	/* Fold the two halves of the pointer */
#ifdef __TURBOC__
	x = FP_SEG(event) ^ FP_OFF(event);
#else
	x = (((int) event >> 16) ^ (int) event) & 0xffff;
#endif

	/* If PHASH is a power of two, this will simply mask off the
	 * higher order bits
	 */
	return x % PHASH;
}
