/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.c,v 1.2 1990/08/23 17:34:20 deyke Exp $ */

#include "global.h"
#include "timer.h"

/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of interval;
 * i.e., the first timer to expire is always at the head of the list.
 * For performance reasons, the intervals are incremental, so that only
 * the first entry needs to be decremented on each tick. Hence there are
 * special routines for reading the number of ticks remaining on a
 * running timer; you can't just look at the field directly.
 */
static struct timer *Timers;

/* This variable keeps a running count of ticks since program startup */
int32 Clock;

/*---------------------------------------------------------------------------*/

/* Process that handles clock ticks */

void tick()
{
  register struct timer *t;

  Clock++;
  if (!Timers) return;
  Timers->count--;
  while ((t = Timers) && t->count <= 0) {
    if (Timers = t->next) Timers->prev = 0;
    t->state = TIMER_EXPIRE;
    if (t->func) (*t->func)(t->arg);
  }
}

/*---------------------------------------------------------------------------*/

/* Stop a timer */

void stop_timer(t)
register struct timer *t;
{
  if (t->state == TIMER_RUN) {
    if (t->prev)
      t->prev->next = t->next;
    else
      Timers = t->next;
    if (t->next) {
      t->next->prev = t->prev;
      t->next->count += t->count;
    }
  }
  t->state = TIMER_STOP;
}

/*---------------------------------------------------------------------------*/

/* Start a timer */

void start_timer(t)
register struct timer *t;
{
  register struct timer *q, *p;

  if (t->state == TIMER_RUN) stop_timer(t);
  if (t->start <= 0) return;
  t->state = TIMER_RUN;
  t->count = t->start;
  for (q = 0, p = Timers; p && p->count < t->count; q = p, p = p->next)
    t->count -= p->count;
  if (t->prev = q)
    q->next = t;
  else
    Timers = t;
  if (t->next = p) {
    p->prev = t;
    p->count -= t->count;
  }
}

/*---------------------------------------------------------------------------*/

/* Return count of ticks remaining on this timer */

int32 read_timer(t)
struct timer *t;
{

  register int32 tot;
  register struct timer *tp;

  if (t->state != TIMER_RUN) return 0;
  tot = 0;
  for (tp = Timers; tp; tp = tp->next) {
    tot += tp->count;
    if (tp == t) break;
  }
  return tot;
}

