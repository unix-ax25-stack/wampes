/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.c,v 1.15 1994/09/05 12:47:24 deyke Exp $ */

/* General purpose software timer facilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <sys/time.h>
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "mbuf.h"
#include "commands.h"
#include "daemon.h"
#include "hardware.h"
#include "socket.h"

int32 Msclock;
int32 Secclock;

/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of expiration;
 * i.e., the first timer to expire is always at the head of the list.
 */
static struct timer *Timers;

static void t_alarm(void *x);

/* Process that handles clock ticks */
void
timerproc(i,v1,v2)
int i;
void *v1,*v2;
{
	register struct timer *t;
	int32 bugfix;
	struct timeval tv;

	for(;;){

		fflush(stdout); /* And flush out stdout too */

		gettimeofday(&tv, (struct timezone *) 0);
		Secclock = tv.tv_sec;
		Msclock = 1000 * Secclock + tv.tv_usec / 1000;

		while((t = Timers) && (bugfix = t->expiration - Msclock) <= 0) {
			if ((Timers = t->next))
				Timers->prev = NULLTIMER;
			t->state = TIMER_EXPIRE;
			if(t->func)
				(*t->func)(t->arg);
		}
#ifndef SINGLE_THREADED
		pwait(NULL);    /* Let them run before handling more ticks */
#else
		return;
#endif
	}
}
/* Start a timer */
void
start_timer(t)
struct timer *t;
{
	register struct timer *tnext;
	struct timer *tprev = NULLTIMER;
	int32 bugfix;

	if(t == NULLTIMER)
		return;
	if(t->state == TIMER_RUN)
		stop_timer(t);
	if(t->duration <= 0)
		return;         /* A duration value of 0 disables the timer */

	t->expiration = Msclock + t->duration;
	t->state = TIMER_RUN;

	/* Find right place on list for this guy. Once again, note use
	 * of subtraction and comparison with zero rather than direct
	 * comparison of expiration times.
	 */
	for(tnext = Timers;tnext != NULLTIMER;tprev=tnext,tnext = tnext->next){
		if((bugfix = tnext->expiration - t->expiration) >= 0)
			break;
	}
	/* At this point, tprev points to the entry that should go right
	 * before us, and tnext points to the entry just after us. Either or
	 * both may be null.
	 */
	if((t->prev = tprev) == NULLTIMER)
		Timers = t;             /* Put at beginning */
	else
		tprev->next = t;

	if ((t->next = tnext))
		tnext->prev = t;
}
/* Stop a timer */
void
stop_timer(timer)
struct timer *timer;
{

	if(timer == NULLTIMER || timer->state != TIMER_RUN)
		return;

	if(timer->prev)
		timer->prev->next = timer->next;
	else
		Timers = timer->next;   /* Was first on list */

	if(timer->next)
		timer->next->prev = timer->prev;

	timer->state = TIMER_STOP;
}
/* Return milliseconds remaining on this timer */
int32
read_timer(t)
struct timer *t;
{
	int32 remaining;

	if(t == NULLTIMER || t->state != TIMER_RUN)
		return 0;

	if((remaining = t->expiration - Msclock) <= 0)
		return 0;       /* Already expired */
	else
		return remaining;
}
int32
next_timer_event()
{
	if (Timers)
		return read_timer(Timers);
	else
		return 0x7fffffff;
}
/* Delay process for specified number of milliseconds.
 * Normally returns 0; returns -1 if aborted by alarm.
 */
int
Xpause(ms)
int32 ms;
{
	int val = 0;

	if(Curproc == NULLPROC || ms <= 0)
		return 0;
	Xalarm(ms);
	/* The actual event doesn't matter, since we'll be alerted */
	while(Curproc->alarm.state == TIMER_RUN){
		if((val = pwait(Curproc)) != 0)
			break;
	}
	Xalarm(0L); /* Make sure it's stopped, in case we were killed */
	return (val == EALARM) ? 0 : -1;
}
static void
t_alarm(x)
void *x;
{
	alert((struct proc *)x,EALARM);
}
/* Send signal to current process after specified number of milliseconds */
void
Xalarm(ms)
int32 ms;
{
	if(Curproc != NULLPROC){
		set_timer(&Curproc->alarm,ms);
		Curproc->alarm.func = t_alarm;
		Curproc->alarm.arg = (char *)Curproc;
		start_timer(&Curproc->alarm);
	}
}
/* Convert time count in seconds to printable days:hr:min:sec format */
char *
tformat(t)
int32 t;
{
	static char buf[17],*cp;
	unsigned int days,hrs,mins,secs;
	int minus;

	if(t < 0){
		t = -t;
		minus = 1;
	} else
		minus = 0;

	secs = t % 60;
	t /= 60;
	mins = t % 60;
	t /= 60;
	hrs = t % 24;
	t /= 24;
	days = t;
	if(minus){
		cp = buf+1;
		buf[0] = '-';
	} else
		cp = buf;
	sprintf(cp,"%u:%02u:%02u:%02u",days,hrs,mins,secs);

	return buf;
}

