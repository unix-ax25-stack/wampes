/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timer.c,v 1.4 1991/04/12 18:35:41 deyke Exp $ */

/* General purpose software timer facilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <time.h>
#include "global.h"
#include "timer.h"

extern int gettimeofday __ARGS((struct timeval *tp, struct timezone *tzp));

int32 Msclock;
int32 Secclock;

/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of interval;
 * i.e., the first timer to expire is always at the head of the list.
 * For performance reasons, the intervals are incremental, so that only
 * the first entry needs to be decremented on each tick. Hence there are
 * special routines for reading the number of ticks remaining on a
 * running timer; you can't just look at the field directly.
 */
static struct timer *Timers;

/*---------------------------------------------------------------------------*/

/* Process that handles clock ticks */

void timerproc()
{

  static int  last_Msclock_valid;
  static int32 last_Msclock;

  int32 elapsed;
  register struct timer *t;
  struct timeval tv;
  struct timezone tz;

  gettimeofday(&tv, &tz);
  Secclock = tv.tv_sec;
  Msclock = 1000 * Secclock + (tv.tv_usec + 500) / 1000;
  elapsed = last_Msclock_valid ? Msclock - last_Msclock : 0;
  last_Msclock = Msclock;
  last_Msclock_valid = 1;

  if (!Timers) return;
  Timers->count -= elapsed;
  while ((t = Timers) && t->count <= 0) {
    if (Timers = t->next) {
      Timers->count += t->count;
      Timers->prev = 0;
    }
    t->state = TIMER_EXPIRE;
    if (t->func) (*t->func)(t->arg);
  }
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

/* Return milliseconds remaining on this timer */

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

/*---------------------------------------------------------------------------*/

int32 next_timer_event()
{
  if (Timers) return Timers->count;
  return 0x7fffffff;
}

/*---------------------------------------------------------------------------*/

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

