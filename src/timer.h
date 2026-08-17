/* @(#) $Id: timer.h,v 1.11 2016/03/13 07:14:39 dl9sau Exp $ */

#ifndef _TIMER_H
#define _TIMER_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* Software timers
 * There is one of these structures for each simulated timer.
 * Whenever the timer is running, it is on a linked list
 * pointed to by "Timers". The list is sorted in ascending order of
 * expiration, with the first timer to expire at the head. This
 * allows the timer process to avoid having to scan the entire list
 * on every clock tick; once it finds an unexpired timer, it can
 * stop searching.
 *
 * Stopping a timer or letting it expire causes it to be removed
 * from the list. Starting a timer puts it on the list at the right
 * place.
 */
struct timer {
	struct timer *next;     /* Linked-list pointer */
	struct timer *prev;
	int32 duration;         /* Duration of timer, in ms */
	int32 expiration;       /* Clock time at expiration */
	void (*func)(void *);   /* Function to call at expiration */
	void *arg;              /* Arg to pass function */
	char state;             /* Timer state */
#define TIMER_STOP      0
#define TIMER_RUN       1
#define TIMER_EXPIRE    2
};
#ifndef EALARM
#define EALARM          106
#endif
/* The difference of two clock values, as a signed count of milliseconds.
 *
 * Msclock is 1000 * the wall clock truncated to 32 bits, so it runs through
 * the whole range every 49.7 days.  Two clock values may therefore straddle
 * the wrap, and subtracting them as SIGNED int32 overflows - which is
 * undefined, and the compiler acts on it: see the note in timer.c, where a
 * timer given a long duration was dropped the moment it was created.  Doing
 * the subtraction unsigned makes the wrap defined; reading the result as
 * signed then gives the interval, positive or negative, as intended.
 *
 * Use this wherever two clock values meet, not just in timer.c.
 */
#define TDIFF(a,b)      ((int32)((uint32)(a) - (uint32)(b)))

/* Useful user macros that hide the timer structure internals */
#define dur_timer(t)    ((t)->duration)
#define run_timer(t)    ((t)->state == TIMER_RUN)

/* In timer.c: */
void kalarm(int32 ms);
int ppause(int32 ms);
int32 read_timer(struct timer *t);
#define set_timer(t,x)  ((t)->duration = (x))
void start_timer(struct timer *t);
void stop_timer(struct timer *timer);
int32 next_timer_event(void);
char *tformat(int32 t);

extern volatile int32 Msclock;
extern volatile int32 Secclock;
#define msclock()       (Msclock)
#define secclock()      (Secclock)

#endif  /* _TIMER_H */
