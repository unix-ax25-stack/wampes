/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.h,v 1.3 1991/02/24 20:17:53 deyke Exp $ */

#ifndef _TIMER_H
#define _TIMER_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* Software timers
 * There is one of these structures for each simulated timer.
 * Whenever the timer is running, it is on a doubly-linked list
 * pointed to by "Timers". The list is sorted in ascending order of
 * expiration, with the next timer to expire at the head. Each timer's
 * count field contains only the additional INCREMENT over all preceeding
 * timers; this allows the hardware timer interrupt to decrement only the
 * first timer on the chain until it hits zero.
 *
 * Stopping a timer or letting it expire causes it to be removed
 * from the list. Starting a timer puts it on the list at the right
 * place. These operations have to be careful about adjusting the count
 * field of the next entry in the list.
 */
struct timer {
	struct timer *next;     /* Doubly-linked-list pointers */
	struct timer *prev;
	int32 start;            /* Period of counter (load value) */
	int32 count;            /* Ticks to go until expiration */
	void (*func) __ARGS((void *));  /* Function to call at expiration */
	void *arg;              /* Arg to pass function */
	char state;             /* Timer state */
#define TIMER_STOP      0
#define TIMER_RUN       1
#define TIMER_EXPIRE    2
};
#define NULLTIMER       (struct timer *)0
/* Useful user macros that hide the timer structure internals */
#define dur_timer(t)    ((t)->start)
#define run_timer(t)    ((t)->state == TIMER_RUN)

/* In timer.c: */
void timerproc __ARGS((void));
int32 read_timer __ARGS((struct timer *t));
#define set_timer(t,x)  ((t)->start = (x))
void start_timer __ARGS((struct timer *t));
void stop_timer __ARGS((struct timer *t));
int32 next_timer_event __ARGS((void));
char *tformat __ARGS((int32 t));

extern int32 Msclock;
extern int32 Secclock;
#define msclock()       (Msclock)
#define secclock()      (Secclock)

#endif  /* _TIMER_H */
