/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.c,v 1.12 1992/09/01 16:52:45 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include "global.h"
#include "hpux.h"
#include "dirutil.h"
#include "commands.h"

#ifdef LINUX
extern const char* const sys_errlist[];
#else
extern char *sys_errlist[];
#endif

/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */
FILE *
dir(path,full)
char *path;
int full;
{
	int fd[2];

	if(pipe(fd))
		return NULLFILE;
	switch(dofork()) {
	case -1:
		close(fd[0]);
		close(fd[1]);
		return NULLFILE;
	case 0:
		close(fd[0]);
		dup2(fd[1],1);
		dup2(fd[1],2);
		close(fd[1]);
		execl("/bin/ls",
		      "ls",
#if defined(ISC) || defined(SCO)
		      full ? "-l" : "-x",
#else
		      full ? "-Al" : "-A",
#endif
		      path,
		      (char *) 0);
		exit(1);
	default:
		close(fd[1]);
		return fdopen(fd[0],"r");
	}
}

/* Create directory */
int
domkd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(mkdir(argv[1],0777) == -1)
		printf("Can't make %s: %s\n",argv[1],sys_errlist[errno]);
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
		printf("Can't remove %s: %s\n",argv[1],sys_errlist[errno]);
	return 0;
}
