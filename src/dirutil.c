/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.c,v 1.3 1990/10/22 11:37:42 deyke Exp $ */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "global.h"
#include "dirutil.h"
#include "commands.h"

extern char *sys_errlist[];

static char *strquote_for_shell __ARGS((char *s1, char *s2));

/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */

FILE *
dir(path,full)
char *path;
int full;
{

	FILE *fp;
	char buf[1024];
	char cmd[1024];
	char fname[L_tmpnam];

	tmpnam(fname);
	sprintf(cmd, "/bin/ls -A %s %s >%s 2>&1",
		full ? "-l" : "",
		strquote_for_shell(buf, path),
		fname);
	fp = system(cmd) ? 0 : fopen(fname, "r");
	unlink(fname);
	return fp;
}
static char *
strquote_for_shell(s1,s2)
char *s1, *s2;
{
	char *p = s1;

	while (*s2) {
		*p++ = '\\';
		*p++ = *s2++;
	}
	*p = '\0';
	return s1;
}
/* Create directory */
int
domkd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(mkdir(argv[1],0777) == -1)
		tprintf("Can't make %s: %s\n",argv[1],sys_errlist[errno]);
	return 0;
}
/* Remove directory */
int
dormd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(rmdir(argv[1]) == -1)
		tprintf("Can't remove %s: %s\n",argv[1],sys_errlist[errno]);
	return 0;
}

