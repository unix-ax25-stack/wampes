/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/hpux.h,v 1.3 1990/03/09 15:41:50 deyke Exp $ */

extern long  currtime;

extern int  chkread[];
extern int  actread[];
extern void (*readfnc[])();
extern char  *readarg[];

extern int  chkwrite[];
extern int  actwrite[];
extern void (*writefnc[])();
extern char  *writearg[];

extern int  chkexcp[];
extern int  actexcp[];
extern void (*excpfnc[])();
extern char  *excparg[];

#define setmask(mask, fd) ((mask)[(fd)>>5] |=  (1 << ((fd) & 31)))
#define clrmask(mask, fd) ((mask)[(fd)>>5] &= ~(1 << ((fd) & 31)))
#define maskset(mask, fd) ((mask)[(fd)>>5] &   (1 << ((fd) & 31)))

