/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/dirutil.c,v 1.18 1996/08/11 18:16:09 deyke Exp $ */

#include <sys/types.h>

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include "global.h"
#include "hpux.h"
#include "dirutil.h"
#include "commands.h"

#include "configure.h"

/* Create a directory listing in a temp file and return the resulting file
 * descriptor. If full == 1, give a full listing; else return just a list
 * of names.
 */
FILE *
dir(
char *path,
int full)
{
	int fd[2];

	if(!*LS_PROG)
		return NULL;
	if(pipe(fd))
		return NULL;
	switch(dofork()) {
	case -1:
		close(fd[0]);
		close(fd[1]);
		return NULL;
	case 0:
		close(fd[0]);
		dup2(fd[1],1);
		dup2(fd[1],2);
		close(fd[1]);
		execl(LS_PROG,
		      "ls",
		      full ? "-Al" : "-A",
		      path,
		      0);
		exit(1);
	default:
		close(fd[1]);
		return fdopen(fd[0],"r");
	}
}

/* Create directory */
int
domkd(
int argc,
char *argv[],
void *p)
{
	char buf[1024];

	if(mkdir(argv[1],0777) == -1){
		sprintf(buf,"Can't make %s",argv[1]);
		perror(buf);
	}
	return 0;
}
/* Remove directory */
int
dormd(
int argc,
char *argv[],
void *p)
{
	char buf[1024];

	if(rmdir(argv[1]) == -1){
		sprintf(buf,"Can't remove %s",argv[1]);
		perror(buf);
	}
	return 0;
}
