/* Parse command line, set up command arguments Unix-style, and call function.
 * Note: argument is modified (delimiters are overwritten with nulls)
 * Improved error handling by Brian Boesch of Stanford University
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "global.h"
#include "cmdparse.h"

int
cmdparse(cmds,line)
struct cmds cmds[];
register char *line;
{
	struct cmds *cmdp;
	char *argv[NARG],*cp;
	int argc,qflag;
	int rslt;

	/* Remove cr/lf */
	rip(line);

	for(argc = 0;argc < NARG;argc++)
		argv[argc] = NULLCHAR;

	for(argc = 0;argc < NARG;){
		qflag = 0;
		/* Skip leading white space */
		while (isspace(uchar(*line)))
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
			if((line = index(line, qflag)) == NULLCHAR){
				line = "";
			}
		} else {
			/* Find space or tab. If not present,
			 * then we've already found the last
			 * token.
			 */
			while (*line && !isspace(uchar(*line)))
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
			printf("%s\n",cmdp->argc_errmsg);
		return -1;
	} else {
		argv[0] = cmdp->name;
		if(argc < cmdp->argcmin) {
			/* Insufficient arguments */
			printf("Usage: %s\n",cmdp->argc_errmsg);
			return -1;
		} else {
			rslt = (*cmdp->func)(argc,argv);
			if ((rslt < 0) && (cmdp->exec_errmsg != NULLCHAR))
				printf("%s\n",cmdp->exec_errmsg);
			return(rslt);
		}
	}
}

/* Call a subcommand based on the first token in an already-parsed line */
int
subcmd(tab,argc,argv)
struct cmds tab[];
int argc;
char *argv[];
{
	int rslt;
	register struct cmds *cmdp;

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
	for(cmdp = tab;cmdp->name != NULLCHAR;cmdp++){
		if(strncmp(argv[0],cmdp->name,strlen(argv[0])) == 0){
			argv[0] = cmdp->name;
			if(argc < cmdp->argcmin) {
				if (cmdp->argc_errmsg != NULLCHAR)
					printf("Usage: %s\n",cmdp->argc_errmsg);
				return -1;
			} else {
				rslt = (*cmdp->func)(argc,argv);
				if ((rslt < 0) && (cmdp->exec_errmsg != NULLCHAR))
					printf("%s\n",cmdp->exec_errmsg);
				return(rslt);
			}
		}
	}
	if (cmdp->argc_errmsg != NULLCHAR)
		printf("%s\n",cmdp->argc_errmsg);
	else {
		printf("\"%s\" subcommands:\n", argv[-1]);
		print_help(tab);
	}
	return -1;
}

static int print_help(cmdp)
register struct cmds *cmdp;
{
	register int  i;

	for (i = 0; cmdp->name; cmdp++, i++)
		printf((i % 5) < 4 ? "%-16s" : "%s\n", cmdp->name);
	if (i % 5) putchar('\n');
	putchar('\n');
	return 0;
}
