#include "global.h"
#include "timer.h"

static struct timer *timers;

/*---------------------------------------------------------------------------*/

stop_timer(t)
register struct timer *t;
{
  register struct timer *p, *q;

  if (!t) return;
  t->state = TIMER_STOP;
  for (q = NULLTIMER, p = timers; p; q = p, p = p->next)
    if (p == t) {
      if (q)
	q->next = p->next;
      else
	timers = p->next;
      return;
    }
}

/*---------------------------------------------------------------------------*/

start_timer(t)
register struct timer *t;
{
  register struct timer *p;

  if (!t) return;
  if (t->start <= 0)
    stop_timer(t);
  else {
    t->state = TIMER_RUN;
    t->count = t->start;
    for (p = timers; p; p = p->next)
      if (p == t) return;
    t->next = timers;
    timers = t;
  }
}

/*---------------------------------------------------------------------------*/

tick()
{
  register struct timer *t;

  for (t = timers; t; t = t->next) (t->count)--;
retry:
  for (t = timers; t; t = t->next)
    if (t->count <= 0) {
      stop_timer(t);
      t->state = TIMER_EXPIRE;
      if (t->func) (*t->func)(t->arg);
      goto retry;
    }
}

