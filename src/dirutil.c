/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.c,v 1.6 1991/05/17 17:06:36 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/rtprio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "global.h"
#include "dirutil.h"
#include "commands.h"

extern char *sys_errlist[];

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
	switch(fork()) {
	case -1:
		close(fd[0]);
		close(fd[1]);
		return NULLFILE;
	case 0:
		rtprio(0,RTPRIO_RTOFF);
		close(fd[0]);
		dup2(fd[1],1);
		dup2(fd[1],2);
		close(fd[1]);
		execl("/bin/ls",
		      "ls",
		      full ? "-Al" : "-A",
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
