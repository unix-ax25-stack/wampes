/* @(#) $Id: cmdparse.c,v 1.20 1999/01/22 21:20:07 deyke Exp $ */

/* Parse command line, set up command arguments Unix-style, and call function.
 * Note: argument is modified (delimiters are overwritten with nulls)
 *
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Improved error handling by Brian Boesch of Stanford University
 * Feb '91 - Bill Simpson
 *              bit16cmd for PPP
 * Mar '91 - Glenn McGregor
 *              handle string escaped sequences
 */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include "strtoul.h"

#include "global.h"
#include "proc.h"
#include "cmdparse.h"

struct boolcmd {
	char *str;      /* Token */
	int val;        /* Value */
};

static struct boolcmd Boolcmds[] = {
	{ "y",            1 },      /* Synonyms for "true" */
	{ "yes",          1 },
	{ "true",         1 },
	{ "on",           1 },
	{ "1",            1 },
	{ "set",          1 },
	{ "enable",       1 },

	{ "n",            0 },      /* Synonyms for "false" */
	{ "no",           0 },
	{ "false",        0 },
	{ "off",          0 },
	{ "0",            0 },
	{ "clear",        0 },
	{ "disable",      0 },
	{ NULL }
};

static int print_help(struct cmds *cmdp);
static char *stringparse(char *line);

/*---------------------------------------------------------------------------*/

/* "Usage:" for one entry, and the one place that knows what to do when there
 * is no usage text: say something rather than hand printf a null pointer,
 * which is what the two argcmin checks below used to do.
 */

static int print_usage(struct cmds *cmdp)
{
	if (cmdp->argc_errmsg != NULL)
		printf("Usage: %s\n", cmdp->argc_errmsg);
	else
		printf("%s: argument missing\n", cmdp->name);
	return -1;
}

/*---------------------------------------------------------------------------*/

/* "<command> ?" prints the usage text, and the command is not called at all.
 * This is what makes a usage text worth writing: without it the table had
 * texts that only ever appeared when too few arguments were given, so "flexnet
 * filter ?" - the natural way to ask - answered nothing.
 *
 * ONLY where there is a text, and that is not laziness.  An entry without one
 * is usually a multiplexer, and subcmd() answers "?" for those by listing the
 * subcommands, which is the better answer.  Taking the question away from them
 * would replace a list of what exists with a line saying nothing.
 *
 * Not calling the command matters in its own right.  "?" used to travel on as
 * an argument, where setint() and friends ran it through atoi(), got a zero,
 * and quietly changed the setting - measured with "ifconfig ax0 paclen ?",
 * which removed the port's paclen without a word.  Passing it on as "no
 * arguments" instead is no better: for a setting that means "show me", but
 * "reset" and "close" with no argument act on the CURRENT session.  A question
 * must not be answered with an action.
 *
 * Returns 1 when the question is answered and the caller is done.
 */

static int query_arg(struct cmds *cmdp, int argc, char *argv[])
{
	if (argc < 2 || strcmp(argv[1], "?"))
		return 0;
	/* Where the entry hands the line on, the list of what it accepts is
	 * the answer, not the syntax line above it - and it is the answer the
	 * question was asking for.  Before this, an entry with both a text and
	 * subcommands printed the text, and the list appeared only for a word
	 * that was WRONG: "attach ?" said "attach <hardware>", "attach xxx"
	 * named the six drivers this build has.
	 */
	if (cmdp->subtab != NULL) {
		printf("\"%s\" subcommands:\n", cmdp->name);
		return print_help(cmdp->subtab) == 0;
	}
	if (cmdp->argc_errmsg == NULL)
		return 0;
	printf("Usage: %s\n", cmdp->argc_errmsg);
	return 1;
}

/*---------------------------------------------------------------------------*/

/* One integer out of one word, or -1.  strtol() with the end pointer checked,
 * because atoi() answers zero for anything it does not understand and cannot
 * be asked whether it understood - so a typing mistake became a setting.
 *
 * Base 10 on purpose, not strtol's base 0: "010" has always meant ten here and
 * turning it into eight would change existing net.rc files without a word.
 */

int cmd_getnum(const char *s, long *val)
{
	char *end;

	if (s == NULL || *s == '\0')
		return -1;
	errno = 0;
	*val = strtol(s, &end, 10);
	while (*end == ' ' || *end == '\t')
		end++;
	if (*end != '\0' || errno == ERANGE)
		return -1;
	return 0;
}

/*---------------------------------------------------------------------------*/

static int badnum(const char *label, const char *word)
{
	printf("%s: \"%s\" is not a number\n", label, word);
	return 1;
}

static char *
stringparse(
char *line)
{
	char *cp = line;
	unsigned long num;

	while ( *line != '\0' && *line != '\"' ) {
		if ( *line == '\\' ) {
			line++;
			switch ( *line++ ) {
			case 'n':
				*cp++ = '\n';
				break;
			case 't':
				*cp++ = '\t';
				break;
			case 'v':
				*cp++ = '\v';
				break;
			case 'b':
				*cp++ = '\b';
				break;
			case 'r':
				*cp++ = '\r';
				break;
			case 'f':
				*cp++ = '\f';
				break;
			case 'a':
				*cp++ = '\007';
				break;
			case '\\':
				*cp++ = '\\';
				break;
			case '\'':
				*cp++ = '\'';
				break;
			case '\"':
				*cp++ = '\"';
				break;
			case 'x':
				num = strtoul( --line, &line, 16 );
				*cp++ = (char) num;
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				num = strtoul( --line, &line, 8 );
				*cp++ = (char) num;
				break;
			case '\0':
				return NULL;
			default:
				*cp++ = *(line - 1);
				break;
			};
		} else {
			*cp++ = *line++;
		}
	}

	if ( *line == '\"' )
		line++;         /* skip final quote */
	*cp = '\0';             /* terminate string */
	return line;
}

int
cmdparse(
struct cmds cmds[],
 char *line,
void *p
){
	struct cmds *cmdp;
	char *argv[NARG];
	char **pargv;
	int argc,i;

	/* Remove cr/lf */
	rip(line);

	for(argc = 0;argc < NARG;argc++)
		argv[argc] = NULL;

	for(argc = 0;argc < NARG;){
		int qflag = FALSE;

		/* Skip leading white space */
		while(isspace(*line & 0xff))
			line++;
		if(*line == '\0')
			break;
		/* '#' is start of comment */
		if(*line == '#')
			break;
		/* Check for quoted token */
		if(*line == '"'){
			line++; /* Suppress quote */
			qflag = TRUE;
		}
		argv[argc++] = line;    /* Beginning of token */

		if(qflag){
			/* Find terminating delimiter */
			if((line = stringparse(line)) == NULL){
				return -1;
			}
		} else {
			/* Find space or tab. If not present,
			 * then we've already found the last
			 * token.
			 */
			while(*line && !isspace(*line & 0xff))
				line++;
		}
		if(*line)
			*line++ = 0;
	}
	if (argc < 1) {         /* empty command line */
		argc = 1;
		argv[0] = "";
	}
	if (argv[0][0] == '?') return print_help(cmds);
	/* Look up command in table; prefix matches are OK */
	for(cmdp = cmds;cmdp->name != NULL;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0)
			break;
	}
	if(cmdp->name == NULL) {
		if(cmdp->argc_errmsg != NULL)
			printf("%s\n",cmdp->argc_errmsg);
		return -1;
	}
	argv[0] = cmdp->name;
	if (query_arg(cmdp, argc, argv))
		return 0;
	if(argc < cmdp->argcmin) {
		/* Insufficient arguments */
		return print_usage(cmdp);
	}
	if(cmdp->func == NULL)
		return 0;
	if(cmdp->stksize == 0){
		return (*cmdp->func)(argc,argv,p);
	} else {
#ifndef SINGLE_THREADED
		/* Make private copy of argv and args,
		 * spawn off subprocess and return.
		 */
		pargv = (char **)callocw(argc,sizeof(char *));
		for(i=0;i<argc;i++)
			pargv[i] = strdup(argv[i]);
		newproc(cmdp->name,cmdp->stksize,
		(void (*)(int,void *,void *))cmdp->func,argc,pargv,p,1);
#endif
		return 0;
	}
}

/* Call a subcommand based on the first token in an already-parsed line */
int
subcmd(
struct cmds tab[],
int argc,
char *argv[],
void *p)
{
	struct cmds *cmdp;
	char **pargv;
	int found = 0;
	int i;

	/* Strip off first token and pass rest of line to subcommand */
	if (argc < 2) {
		if (argc < 1)
			printf("SUBCMD - Don't know what to do?\n");
		else {
			printf("\"%s\" subcommands:\n", argv[0]);
			print_help(tab);
		}
		return -1;
	}
	argc--;
	argv++;
	for(cmdp = tab;cmdp->name != NULL;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0){
			found = 1;
			break;
		}
	}
	if(!found){
		printf("\"%s\" subcommands:\n", argv[-1]);
		print_help(tab);
		return -1;
	}
	if (query_arg(cmdp, argc, argv))
		return 0;
	if(argc < cmdp->argcmin){
		return print_usage(cmdp);
	}
	if(cmdp->stksize == 0){
		return (*cmdp->func)(argc,argv,p);
	} else {
#ifndef SINGLE_THREADED
		/* Make private copy of argv and args */
		pargv = (char **)callocw(argc,sizeof(char *));
		for(i=0;i<argc;i++)
			pargv[i] = strdup(argv[i]);
		newproc(cmdp->name,cmdp->stksize,
		 (void (*)(int,void *,void *))cmdp->func,argc,pargv,p,1);
#endif
		return(0);
	}
}

static int print_help(
struct cmds *cmdp)
{
	int i;

	for (i = 0; cmdp->name; cmdp++, i++)
		printf((i % 5) < 4 ? "%-16s" : "%s\n", cmdp->name);
	if (i % 5) putchar('\n');
	putchar('\n');
	return 0;
}

/* Subroutine for setting and displaying boolean flags */
int
setbool(
int *var,
char *label,
int argc,
char *argv[])
{
	struct boolcmd *bc;

	if(argc < 2){
		printf("%s: %s\n",label,*var ? "on":"off");
		return 0;
	}
	for(bc = Boolcmds;bc->str != NULL;bc++){
		if(stricmp(argv[1],bc->str) == 0){
			*var = bc->val;
			return 0;
		}
	}
	printf("Valid options:");
	for(bc = Boolcmds;bc->str != NULL;bc++)
		printf(" %s",bc->str);

	printf("\n");
	return 1;
}

/* Subroutine for setting and displaying bit values */
int
bitcmd(
uint *bits,
uint mask,
char *label,
int argc,
char *argv[])
{
	int doing = (*bits & mask);
	int result = setbool( &doing, label, argc, argv );

	if ( !result ) {
		if ( doing )
			*bits |= mask;
		else
			*bits &= ~mask;
	}
	return result;
}

/* Subroutine for setting and displaying long variables */
int
setlong(
int32 *var,
char *label,
int argc,
char *argv[])
{
	long val;

	if(argc < 2)
		printf("%s: %d\n",label,*var);
	else if(cmd_getnum(argv[1],&val))
		return badnum(label,argv[1]);
	else
		*var = (int32) val;

	return 0;
}
/* Subroutine for setting and displaying short variables */
int
setshort(
unsigned short *var,
char *label,
int argc,
char *argv[])
{
	long val;

	if(argc < 2)
		printf("%s: %u\n",label,*var);
	else if(cmd_getnum(argv[1],&val))
		return badnum(label,argv[1]);
	else
		*var = (unsigned short) val;

	return 0;
}
/* Subroutine for setting and displaying integer variables */
int
setint(
int *var,
char *label,
int argc,
char *argv[])
{
	long val;

	if(argc < 2)
		printf("%s: %d\n",label,*var);
	else if(cmd_getnum(argv[1],&val))
		return badnum(label,argv[1]);
	else
		*var = (int) val;

	return 0;
}

/* Subroutine for setting and displaying unsigned integer variables */
int
setuns(
unsigned *var,
char *label,
int argc,
char *argv[])
{
	long val;

	if(argc < 2)
		printf("%s: %u\n",label,*var);
	else if(cmd_getnum(argv[1],&val))
		return badnum(label,argv[1]);
	else
		*var = (unsigned) val;

	return 0;
}

/* Subroutine for setting and displaying int variables (with range check) */

int setintrc(
int *var,
char *label,
int argc,
char *argv[],
int minval,
int maxval)
{
  if (argc < 2)
    printf("%s: %d\n", label, *var);
  else {
    long tmp;

    /* Not atoi(): where zero is a legal value - the per-port paclen, maxframe
     * and emaxframe, where it means "take the node's" - a word that is not a
     * number came through as a zero and passed the range check.  Measured:
     * "ifconfig ax0 paclen ?" removed the port's paclen without a word.
     */
    if (cmd_getnum(argv[1], &tmp))
      return badnum(label, argv[1]);
    if (tmp < minval || tmp > maxval) {
      printf("%s must be %d..%d\n", label, minval, maxval);
      return 1;
    }
    *var = (int) tmp;
  }
  return 0;
}

