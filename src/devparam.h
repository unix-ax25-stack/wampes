/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/devparam.h,v 1.3 1992/01/08 13:45:06 deyke Exp $ */

#ifndef _DEVPARAM_H
#define _DEVPARAM_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* device parameter control */
#define PARAM_DATA      0
#define PARAM_TXDELAY   1
#define PARAM_PERSIST   2
#define PARAM_SLOTTIME  3
#define PARAM_TXTAIL    4
#define PARAM_FULLDUP   5
#define PARAM_HW        6
#define PARAM_MUTE      7
#define PARAM_DTR       8
#define PARAM_RTS       9
#define PARAM_SPEED     10
#define PARAM_ENDDELAY  11
#define PARAM_GROUP     12
#define PARAM_IDLE      13
#define PARAM_MIN       14
#define PARAM_MAXKEY    15
#define PARAM_WAIT      16
#define PARAM_DOWN      0x81
#define PARAM_UP        0x82
#define PARAM_BLIND     0x83    /* should be vertigo, can't tell down/up? */
#define PARAM_RETURN    0xff

/* In devparam.c: */
int devparam __ARGS((char *s));
char *parmname __ARGS((int n));

#endif  /* _DEVPARAM_H */

