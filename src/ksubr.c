/* @(#) $Id: ksubr.c,v 1.37 2016/03/13 14:44:58 dl9sau Exp $ */

/* Machine or compiler-dependent portions of kernel
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <sys/types.h>
#ifdef HAS_UCONTEXT
#include <ucontext.h>
#else
#ifndef ibm032
#include <setjmp.h>
#endif
#endif
#include <stdio.h>
#include <time.h>
#include "global.h"
#include "proc.h"
#include "commands.h"
#include "main.h"

#ifndef HAS_UCONTEXT
#if defined __hp9000s300
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
#elif defined __hp9000s800
struct env {
	long    jb_rp;          /* Return Pointer */
	long    jb_sp;          /* Marker SP */
	long    jb_sm;          /* Signal Mask */
	long    jb_os;          /* On Sigstack */
	long    jb_gr3;         /* Entry Save General Registers */
	long    jb_gr4;
	long    jb_gr5;
	long    jb_gr6;
	long    jb_gr7;
	long    jb_gr8;
	long    jb_gr9;
	long    jb_gr10;
	long    jb_gr11;
	long    jb_gr12;
	long    jb_gr13;
	long    jb_gr14;
	long    jb_gr15;
	long    jb_gr16;
	long    jb_gr17;
	long    jb_gr18;
	long    jb_gr19;
	long    jb_sr3;         /* Entry Save Space Register */
	double  jb_fr12;        /* Entry Save Floating Point Registers */
	double  jb_fr13;
	double  jb_fr14;
	double  jb_fr15;
	long    jb_save;        /* Restore Mask? (see sigsetjmp/siglongjmp) */
	/* alignment hole */
	double  jb_fr16;
	double  jb_fr17;
	double  jb_fr18;
	double  jb_fr19;
	double  jb_fr20;
	double  jb_fr21;
	long    jb_rp_prime;  /* rp prime from frame marker */
	long    jb_ext_dp;    /* external_dp from frame marker */
};
#define getstackptr(ep) ((ep)->jb_sp)
#elif defined _AIX
struct env {
	long    i_dont_know_0;
	long    i_dont_know_1;
	long    i_dont_know_2;
	long    sp;
};
#define getstackptr(ep) ((ep)->sp)
#elif defined __sgi
struct env {
	int     jb_onsigstk;    /* onsigstack flag */
	int     jb_sigmask;     /* signal mask */
	int     jb_sp;          /* stack pointer */
	int     jb_pc;          /* program counter */
	int     jb_v0;          /* longjmp retval */
	int     jb_s0;          /* callee saved regs.... */
	int     jb_s1;
	int     jb_s2;
	int     jb_s3;
	int     jb_s4;
	int     jb_s5;
	int     jb_s6;
	int     jb_s7;
	int     jb_s8;          /* frame pointer */
	int     jb_f20;         /* callee save regs */
	int     jb_f21;
	int     jb_f22;
	int     jb_f23;
	int     jb_f24;
	int     jb_f25;
	int     jb_f26;
	int     jb_f27;
	int     jb_f28;
	int     jb_f29;
	int     jb_f30;
	int     jb_f31;
	int     jb_fpc_csr;     /* fp control and status register */
	int     jb_magic;
};
#define getstackptr(ep) ((ep)->jb_sp)
#elif defined sun
#if _JBLEN == 9 || _JBLEN == 58
struct env {
	long    onsstack;
	long    sigmask;
	long    sp;
	long    pc;
	long    npc;
	long    psr;
	long    g1;
	long    o0;
	long    wbcnt;
};
#define getstackptr(ep) ((ep)->sp)
#elif _JBLEN == 12
struct env {
	long    i_dont_know;
	long    sp;
};
#define getstackptr(ep) ((ep)->sp)
#else
#error error: unknown jmp_buf size
#endif
#elif defined ULTRIX_RISC
struct env {
	long    on;
	long    sig;
	long    pc;
	long    regs;
	long    zero;
	long    magic;
	long    at;
	long    v0;
	long    v1;
	long    a0;
	long    a1;
	long    a2;
	long    a3;
	long    t0;
	long    t1;
	long    t2;
	long    t3;
	long    t4;
	long    t5;
	long    t6;
	long    t7;
	long    s0;
	long    s1;
	long    s2;
	long    s3;
	long    s4;
	long    s5;
	long    s6;
	long    s7;
	long    t8;
	long    t9;
	long    k0;
	long    k1;
	long    gp;
	long    sp;
	long    s8;
	long    ra;
	long    fregs;
	long    f0;
	long    f1;
	long    f2;
	long    f3;
	long    f4;
	long    f5;
	long    f6;
	long    f7;
	long    f8;
	long    f9;
	long    f10;
	long    f11;
	long    f12;
	long    f13;
	long    f14;
	long    f15;
	long    f16;
	long    f17;
	long    f18;
	long    f19;
	long    f20;
	long    f21;
	long    f22;
	long    f23;
	long    f24;
	long    f25;
	long    f26;
	long    f27;
	long    f28;
	long    f29;
	long    f30;
	long    f31;
	long    fpc;
	long    mdlo;
	long    mdhi;
	long    flags;
	long    fmagic;
	long    pad;
	long    nbjregs;
};
#define getstackptr(ep) ((ep)->sp)
#elif defined linux
struct env {
	long    ebx;
	long    esi;
	long    edi;
	long    ebp;
	long    esp;
	long    epc;
};
#define getstackptr(ep) ((ep)->esp)
#elif defined __386BSD__
struct env {
	long    unknown0;
	long    unknown1;
	long    esp;
	long    unknown3;
	long    unknown4;
	long    unknown5;
	long    unknown6;
	long    unknown7;
	long    unknown8;
	long    unknown9;
};
#define getstackptr(ep) ((ep)->esp)
#elif defined __NetBSD__
struct env {
	long    unknown0;
	long    unknown1;
	long    esp;
	long    unknown3;
	long    unknown4;
	long    unknown5;
	long    unknown6;
	long    unknown7;
	long    unknown8;
	long    unknown9;
};
#define getstackptr(ep) ((ep)->esp)
#elif defined __FreeBSD__
struct env {
	long    unknown0;
	long    unknown1;
	long    esp;
	long    unknown3;
	long    unknown4;
	long    unknown5;
	long    unknown6;
	long    unknown7;
	long    unknown8;
	long    unknown9;
};
#define getstackptr(ep) ((ep)->esp)
#elif defined __bsdi__
struct env {
	long    unknown0;
	long    unknown1;
	long    esp;
	long    unknown3;
	long    unknown4;
	long    unknown5;
	long    unknown6;
	long    unknown7;
	long    unknown8;
	long    unknown9;
};
#define getstackptr(ep) ((ep)->esp)
#elif defined macII
struct env {
	long    d2;
	long    d3;
	long    d4;
	long    d5;
	long    d6;
	long    d7;
	long    a1;
	long    a2;
	long    a3;
	long    a4;
	long    a5;
	long    a6;
	long    a7;
};
#define getstackptr(ep) ((ep)->a7)
#elif defined ibm032
struct env {
	unsigned        r1;
	unsigned        r6;
	unsigned        r7;
	unsigned        r8;
	unsigned        r9;
	unsigned        r10;
	unsigned        r11;
	unsigned        r12;
	unsigned        r13;
	unsigned        r14;
	unsigned        r15;
	unsigned        sigmask;
	unsigned        resv1;
	unsigned        resv2;
	unsigned        resv3;
	unsigned        resv4;
};
#define getstackptr(ep) ((ep)->r1)
#elif defined __NeXT__
struct env {
	long    ebx;
	long    esi;
	long    edi;
	long    ebp;
	long    esp;
	long    epc;
};
#define getstackptr(ep) ((ep)->esp)
#else
struct env {
	long    dummy;
};
#define getstackptr(ep) (0L)
#endif
#endif

static int stkutil(struct proc *pp);
static void pproc(struct proc *pp);

void
kinit(void)
{
	/* Initialize signal queue */
	Ksig.wp = Ksig.rp = Ksig.entry;
}
/* Print process table info
 * Since things can change while ps is running, the ready proceses are
 * displayed last. This is because an interrupt can make a process ready,
 * but a ready process won't spontaneously become unready. Therefore a
 * process that changes during ps may show up twice, but this is better
 * than not having it showing up at all.
 */
int
ps(
int argc,
char *argv[],
void *p)
{
	struct proc *pp;
	int i;

	printf("Uptime %s\n",tformat(secclock()-StartTime));

	printf("ksigs %lu queued %lu hiwat %u woken %lu nops %lu dups %lu\n",Ksig.ksigs,
	 Ksig.ksigsqueued,Ksig.maxentries,Ksig.ksigwakes,Ksig.ksignops,Ksig.duksigs);
	Ksig.maxentries = 0;
	printf("kwaits %lu nops %lu from int %lu\n",
	 Ksig.kwaits,Ksig.kwaitnops,Ksig.kwaitints);
#ifdef	HAS_UCONTEXT
	printf("PID       SP        stksize   maxstk    event     fl    name\n");
#else
	printf("PID       SP        stksize   maxstk    event     fl  in  out  name\n");
#endif

	for(pp = Susptab;pp != NULL;pp = pp->next)
		pproc(pp);

	for(i=0;i<PHASH;i++)
		for(pp = Waittab[i];pp != NULL;pp = pp->next)
			pproc(pp);

	for(pp = Rdytab;pp != NULL;pp = pp->next)
		pproc(pp);

	if(Curproc != NULL)
		pproc(Curproc);

	return 0;
}
static void
pproc(
struct proc *pp)
{
	struct env *ep;

	ep = (struct env *)&pp->env;
#ifdef	HAS_UCONTEXT
	printf("%08lx  %08lx  %7u   %6u    %08lx  %c%c%c   %s\n",
	 (long)pp,(long)pp->stack,pp->stksize,stkutil(pp),
#else
	printf("%08lx  %08lx  %7u   %6u    %08lx  %c%c%c %3d %3d  %s\n",
	 (long)pp,getstackptr(ep),pp->stksize,stkutil(pp),
#endif
	 (long)pp->event,
	 ' ',
	 pp->flags.waiting ? 'W' : ' ',
	 pp->flags.suspend ? 'S' : ' ',
#ifndef	HAS_UCONTEXT
	 (int)pp->input,(int)pp->output,
#endif
	 pp->name);
}
static int
stkutil(
struct proc *pp)
{
	unsigned i;
	uint16 *sp;

	if(pp->stksize == 0)
		return 0;       /* Main task -- too hard to check */
	i = pp->stksize;
#ifdef	HAS_UCONTEXT
	for(sp = pp->stack;*sp == STACKPAT && sp < pp->stack + pp->stksize;sp++)
#else
#ifndef __hp9000s800
	for(sp = pp->stack;*sp == STACKPAT && sp < pp->stack + pp->stksize;sp++)
#else
	for(sp = pp->stack + (pp->stksize-1);*sp == STACKPAT && sp >= pp->stack;sp--)
#endif
#endif
		i--;
	return i;
}
unsigned
phash(
void *event)
{
	/* If PHASH is a power of two, this will simply mask off the
	 * higher order bits
	 */
#ifdef HAS_UCONTEXT
	return ((long)event >> 2) % PHASH;
#else
	return (int)event % PHASH;
#endif
}
