/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.h,v 1.3 1991/02/24 20:16:42 deyke Exp $ */

#ifndef _DIRUTIL_H
#define _DIRUTIL_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

/* In dirutil.c */
FILE *dir __ARGS((char *path,int full));
int filedir __ARGS((char *name,int times,char *ret_str));
int getdir __ARGS((char *path,int full,FILE *file));

/* In pathname.c: */
char *pathname __ARGS((char *cd,char *path));

#endif /* _DIRUTIL_H */

