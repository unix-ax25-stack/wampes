/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.h,v 1.7 1993/05/17 13:45:22 deyke Exp $ */

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
#define NULLTIMER       (struct timer *)0
#ifndef EALARM
#define EALARM          106
#endif
/* Useful user macros that hide the timer structure internals */
#define dur_timer(t)    ((t)->duration)
#define run_timer(t)    ((t)->state == TIMER_RUN)

/* In timer.c: */
void Xalarm(int32 ms);
int Xpause(int32 ms);
int32 read_timer(struct timer *t);
#define set_timer(t,x)  ((t)->duration = (x))
void start_timer(struct timer *t);
void stop_timer(struct timer *timer);
int32 next_timer_event(void);
char *tformat(int32 t);

extern int32 Msclock;
extern int32 Secclock;
#define msclock()       (Msclock)
#define secclock()      (Secclock)

#endif  /* _TIMER_H */
