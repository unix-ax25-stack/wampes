/* @(#) $Id: timer.c,v 1.24 2016/03/13 07:14:39 dl9sau Exp $ */

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
#include "socket.h"

int32 volatile Msclock;
int32 volatile Secclock;

/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of expiration;
 * i.e., the first timer to expire is always at the head of the list.
 */
static struct timer *Timers;

static void t_alarm(void *x);

/* Process that handles clock ticks */
void
timerproc(int i,void *v1,void *v2)
{
	struct timer *t;
	int32 bugfix;
	struct timeval tv;

	for(;;){

		fflush(stdout); /* And flush out stdout too */

		gettimeofday(&tv, 0);
		Secclock = tv.tv_sec;
		Msclock = (int32)(1000 * (long) Secclock + tv.tv_usec / 1000);

		while((t = Timers) && (bugfix = t->expiration - Msclock) <= 0) {
			if ((Timers = t->next))
				Timers->prev = NULL;
			t->state = TIMER_EXPIRE;
			if(t->func){
				(*t->func)(t->arg);
			}
		}
#ifndef SINGLE_THREADED
		kwait(NULL);    /* Let them run before handling more ticks */
#else
		return;
#endif
	}
}
/* Start a timer */
void
start_timer(struct timer *t)
{
	struct timer *tnext, *tprev = NULL;
	int32 bugfix;

	if(t == NULL)
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
	for(tnext = Timers;tnext != NULL;tprev=tnext,tnext = tnext->next){
		if((bugfix = tnext->expiration - t->expiration) >= 0)
			break;
	}
	/* At this point, tprev points to the entry that should go right
	 * before us, and tnext points to the entry just after us. Either or
	 * both may be null.
	 */
	if((t->prev = tprev) == NULL)
		Timers = t;             /* Put at beginning */
	else
		tprev->next = t;

	if ((t->next = tnext))
		tnext->prev = t;
}
/* Stop a timer */
void
stop_timer(struct timer *timer)
{

	if(timer == NULL || timer->state != TIMER_RUN)
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
read_timer(struct timer *t)
{
	int32 remaining;

	if(t == NULL || t->state != TIMER_RUN)
		return 0;
	remaining = t->expiration - Msclock;
	if(remaining <= 0)
		return 0;       /* Already expired */
	else
		return remaining;
}
int32
next_timer_event(void)
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
ppause(int32 ms)
{
	int val = 0;

	if(Curproc == NULL || ms <= 0)
		return 0;
	kalarm(ms);
	/* The actual event doesn't matter, since we'll be alerted */
	while(Curproc->alarm.state == TIMER_RUN){
		if((val = kwait(Curproc)) != 0)
			break;
	}
	kalarm(0L); /* Make sure it's stopped, in case we were killed */
	return (val == EALARM) ? 0 : -1;
}
static void
t_alarm(void *x)
{
	alert((struct proc *)x,EALARM);
}
/* Send signal to current process after specified number of milliseconds */
void
kalarm(int32 ms)
{
	if(Curproc != NULL){
		set_timer(&Curproc->alarm,ms);
		Curproc->alarm.func = t_alarm;
		Curproc->alarm.arg = (char *)Curproc;
		start_timer(&Curproc->alarm);
	}
}
/* Convert time count in seconds to printable days:hr:min:sec format */
char *
tformat(int32 t)
{
	static char buf[17],*cp;
	unsigned int days,hrs,mins,secs;
	int minus;

	if(t < 0){
		t = -t;
		minus = 1;
	} else
		minus = 0;

	secs = (unsigned int) (t % 60);
	t /= 60;
	mins = (unsigned int) (t % 60);
	t /= 60;
	hrs = (unsigned int) (t % 24);
	t /= 24;
	days = (unsigned int) t;
	if(minus){
		cp = buf+1;
		buf[0] = '-';
	} else
		cp = buf;
	sprintf(cp,"%u:%02u:%02u:%02u",days,hrs,mins,secs);

	return buf;
}

