/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/cmdparse.c,v 1.3 1990/09/11 13:45:10 deyke Exp $ */

/* Parse command line, set up command arguments Unix-style, and call function.
 * Note: argument is modified (delimiters are overwritten with nulls)
 * Improved error handling by Brian Boesch of Stanford University
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "cmdparse.h"

struct boolcmd {
	char *str;      /* Token */
	int val;        /* Value */
};

static struct boolcmd Boolcmds[] = {
	"y",            1,      /* Synonyms for "true" */
	"yes",          1,
	"true",         1,
	"on",           1,
	"1",            1,
	"set",          1,
	"enable",       1,

	"n",            0,      /* Synonyms for "false" */
	"no",           0,
	"false",        0,
	"off",          0,
	"0",            0,
	"clear",        0,
	"disable",      0,
	NULLCHAR
};

static int print_help __ARGS((struct cmds *cmdp));
static int strcmpi __ARGS((char *s1, char *s2));

int
cmdparse(cmds,line,p)
struct cmds cmds[];
register char *line;
void *p;
{
	struct cmds *cmdp;
	char *argv[NARG];
	int argc,qflag;

	/* Remove cr/lf */
	rip(line);

	for(argc = 0;argc < NARG;argc++)
		argv[argc] = NULLCHAR;

	for(argc = 0;argc < NARG;){
		qflag = 0;
		/* Skip leading white space */
		while(isspace(uchar(*line)))
			line++;
		if(*line == '\0')
			break;
		/* '#' is start of comment */
		if(*line == '#')
			break;
		/* Check for quoted token */
		if(*line == '"' || *line == '\'')
			qflag = *line++; /* Suppress quote */

		argv[argc++] = line;    /* Beginning of token */
		/* Find terminating delimiter */
		if(qflag){
			/* Find quote */
			if((line = strchr(line, qflag)) == NULLCHAR){
				line = "";
			}
		} else {
			/* Find space or tab. If not present,
			 * then we've already found the last
			 * token.
			 */
			while(*line && !isspace(uchar(*line)))
				line++;
		}
		if(*line)
			*line++ = '\0';
	}
	if (argc < 1) {         /* empty command line */
		argc = 1;
		argv[0] = "";
	}
	if (argv[0][0] == '?') return print_help(cmds);
	/* Look up command in table; prefix matches are OK */
	for(cmdp = cmds;cmdp->name != NULLCHAR;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0)
			break;
	}
	if(cmdp->name == NULLCHAR) {
		if(cmdp->argc_errmsg != NULLCHAR)
			tprintf("%s\n",cmdp->argc_errmsg);
		return -1;
	} else {
		argv[0] = cmdp->name;
		if(argc < cmdp->argcmin) {
			/* Insufficient arguments */
			tprintf("Usage: %s\n",cmdp->argc_errmsg);
			return -1;
		} else {
			return (*cmdp->func)(argc,argv,p);
		}
	}
}

/* Call a subcommand based on the first token in an already-parsed line */
int
subcmd(tab,argc,argv,p)
struct cmds tab[];
int argc;
char *argv[];
void *p;
{
	register struct cmds *cmdp;
	int found = 0;

	/* Strip off first token and pass rest of line to subcommand */
	if (argc < 2) {
		if (argc < 1)
			tprintf("SUBCMD - Don't know what to do?\n");
		else {
			tprintf("\"%s\" subcommands:\n", argv[0]);
			print_help(tab);
		}
		return -1;
	}
	argc--;
	argv++;
	for(cmdp = tab;cmdp->name != NULLCHAR;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0){
			found = 1;
			break;
		}
	}
	if(!found){
		tprintf("\"%s\" subcommands:\n", argv[-1]);
		print_help(tab);
		return -1;
	}
	argv[0] = cmdp->name;
	if(argc < cmdp->argcmin){
		if(cmdp->argc_errmsg != NULLCHAR)
			tprintf("Usage: %s\n",cmdp->argc_errmsg);
		return -1;
	}
	return (*cmdp->func)(argc,argv,p);
}

static int print_help(cmdp)
register struct cmds *cmdp;
{
	register int  i;

	for (i = 0; cmdp->name; cmdp++, i++)
		tprintf((i % 5) < 4 ? "%-16s" : "%s\n", cmdp->name);
	if (i % 5) putchar('\n');
	putchar('\n');
	return 0;
}

static int  strcmpi(s1, s2)
char  *s1, *s2;
{
  while (tolower(uchar(*s1)) == tolower(uchar(*s2++)))
    if (!*s1++) return 0;
  return tolower(uchar(*s1)) - tolower(uchar(s2[-1]));
}

/* Subroutine for setting and displaying boolean flags */
int
setbool(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
	struct boolcmd *bc;

	if(argc < 2){
		tprintf("%s: %s\n",label,*var ? "on":"off");
		return 0;
	}
	for(bc = Boolcmds;bc->str != NULLCHAR;bc++){
		if(strcmpi(argv[1],bc->str) == 0){
			*var = bc->val;
			return 0;
		}
	}
	tprintf("Valid options:");
	for(bc = Boolcmds;bc->str != NULLCHAR;bc++)
		if(tprintf(" %s",bc->str) == EOF)
			return 1;
	tprintf("\n");
	return 1;
}
/* Subroutine for setting and displaying long variables */
int
setlong(var,label,argc,argv)
int32 *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		tprintf("%s: %ld\n",label,*var);
	else
		*var = atol(argv[1]);

	return 0;
}
/* Subroutine for setting and displaying short variables */
int
setshort(var,label,argc,argv)
unsigned short *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		tprintf("%s: %u\n",label,*var);
	else
		*var = atoi(argv[1]);

	return 0;
}
/* Subroutine for setting and displaying integer variables */
int
setint(var,label,argc,argv)
int *var;
char *label;
int argc;
char *argv[];
{
	if(argc < 2)
		tprintf("%s: %u\n",label,*var);
	else
		*var = atoi(argv[1]);

	return 0;
}

/* Subroutine for setting and displaying int variables (with range check) */

int  setintrc(var, label, argc, argv, minval, maxval)
int  *var;
char  *label;
int  argc;
char  *argv[];
int  minval;
int  maxval;
{
  if (argc < 2)
    tprintf("%s: %i\n", label, *var);
  else {
    int  tmp = atoi(argv[1]);
    if (tmp < minval || tmp > maxval) {
      tprintf("%s must be %i..%i\n", label, minval, maxval);
      return 1;
    }
    *var = tmp;
  }
  return 0;
}

