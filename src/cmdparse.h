/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/cmdparse.h,v 1.2 1990/08/23 17:32:43 deyke Exp $ */

#ifndef NARG

#define NARG            40      /* Max number of args to commands */

struct cmds {
	char *name;             /* Name of command */
	int (*func) __ARGS((int argc,char *argv[],void *p));
				/* Function to execute command */
	int stksize;            /* Size of stack if subprocess, 0 if synch */
	int  argcmin;           /* Minimum number of args */
	char *argc_errmsg;      /* Message to print if insufficient args */
};
#ifndef NULLCHAR
#define NULLCHAR        (char *)0
#endif

/* In cmdparse.c: */
int cmdparse __ARGS((struct cmds cmds[],char *line,void *p));
int subcmd __ARGS((struct cmds tab[],int argc,char *argv[],void *p));
int setbool __ARGS((int *var,char *label,int argc,char *argv[]));
int setlong __ARGS((int32 *var,char *label,int argc,char *argv[]));
int setshort __ARGS((unsigned short *var,char *label,int argc,char *argv[]));

#endif  /* NARG */

