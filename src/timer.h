/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.h,v 1.2 1990/08/23 17:34:22 deyke Exp $ */

#ifndef TIMER_STOP

#include "global.h"

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
#define MAX_TIME        (int32)0x7fffffff       /* Max long integer */
#define MSPTICK         1000            /* Milliseconds per tick */
/* Useful user macros that hide the timer structure internals */
#define set_timer(t,x)  (((t)->start) = (x)/MSPTICK)
#define dur_timer(t)    ((t)->start)
#define run_timer(t)    ((t)->state == TIMER_RUN)

extern int32 Clock;     /* Count of ticks since start up */

/* In timer.c: */
int32 read_timer __ARGS((struct timer *t));
void start_timer __ARGS((struct timer *t));
void stop_timer __ARGS((struct timer *t));

#endif  /* TIMER_STOP */

