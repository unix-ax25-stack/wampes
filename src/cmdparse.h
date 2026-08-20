/* @(#) $Id: cmdparse.h,v 1.14 1996/08/19 16:30:14 deyke Exp $ */

#ifndef _CMDPARSE_H
#define _CMDPARSE_H

#define NARG            20      /* Max number of args to commands */

struct cmds {
	char *name;             /* Name of command */
	int (*func)(int argc,char *argv[],void *p);
				/* Function to execute command */
	int stksize;            /* Size of stack if subprocess, 0 if synch */
	int  argcmin;           /* Minimum number of args */
	char *argc_errmsg;      /* Message to print if insufficient args */
	/* The table this entry hands the rest of the line to, where it is one
	 * of those - and it is only here so that "<command> ?" can answer with
	 * the LIST rather than with a usage line that names no name.  An entry
	 * that carries both a usage text and subcommands otherwise loses the
	 * list: the text wins, and only a WRONG word ever showed what exists.
	 * Left out everywhere else, which makes it a null pointer.
	 */
	struct cmds *subtab;
};

/* In cmdparse.c: */
int cmd_getnum(const char *s,long *val);
int cmdparse(struct cmds cmds[],char *line,void *p);
int subcmd(struct cmds tab[],int argc,char *argv[],void *p);
int setbool(int *var,char *label,int argc,char *argv[]);
int bitcmd(uint *bits, uint mask, char *label, int argc, char *argv[]);
int setint(int *var,char *label,int argc,char *argv[]);
int setlong(int32 *var,char *label,int argc,char *argv[]);
int setshort(unsigned short *var,char *label,int argc,char *argv[]);
int setuns(unsigned *var,char *label,int argc,char *argv[]);
int setintrc(int *var,char *label,int argc,char *argv[],int minval,int maxval);

#endif  /* _CMDPARSE_H */
