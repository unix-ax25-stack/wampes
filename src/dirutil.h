/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.h,v 1.2 1990/10/12 19:25:32 deyke Exp $ */

#include "global.h"

/* In dirutil.c: */
FILE *dir __ARGS((char *path,int full));
int filedir __ARGS((char *name,int times,char *ret_str));
int getdir __ARGS((char *path,int full,FILE *file));

/* In pathname.c: */
char *pathname __ARGS((char *cd,char *path));
