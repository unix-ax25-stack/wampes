#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/import.c,v 1.2 1995/12/20 09:47:00 deyke Exp $";
#endif

/*

   This program will be called via .forward whenever mail for adm@mdddhd
   arrives.  Stdin will contain a single UNIX mail message.  The body of
   this mail message will contain one or more BBS mail messages.  Those
   will get feed to the BBS program.

 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEBUG 0

static FILE *fpinp;
static FILE *fpout;

/*---------------------------------------------------------------------------*/

static void errorstop(int line)
{
  char buf[80];

  sprintf(buf, "Fatal error in line %d", line);
  perror(buf);
  fprintf(stderr, "Program stopped.\n");
  exit(0);
}

/*---------------------------------------------------------------------------*/

#define halt() errorstop(__LINE__)

/*---------------------------------------------------------------------------*/

static void wait_for_prompt(void)
{

  char line[1024];
  int len;

  fflush(fpout);
  for (;;) {
    if (!fgets(line, sizeof(line), fpinp))
      halt();
    fputs(line, stderr);
    len = strlen(line);
    if (len >= 2 && line[len - 2] == '>')
      break;
  }
}

/*---------------------------------------------------------------------------*/

int main(void)
{

  char line[1024];
  int i;
  int ok = 0;
  int pipeinp[2];
  int pipeout[2];

#if !DEBUG
  fclose(stderr);
  fopen("/dev/null", "r+");
#endif

  /* Open bbs */

  if (pipe(pipeinp) || pipe(pipeout))
    halt();
  switch (fork()) {
  case -1:
    halt();
  case 0:
    for (i = 0; i < 60; i++)
      if (i != pipeinp[1] && i != pipeout[0])
	close(i);
    dup2(pipeout[0], 0);
    dup2(pipeinp[1], 1);
    dup2(pipeinp[1], 2);
    close(pipeinp[1]);
    close(pipeout[0]);
    execl("/usr/local/bin/bbs", "bbs", 0);
    exit(0);
  default:
    break;
  }
  close(pipeinp[1]);
  close(pipeout[0]);
  if (!(fpinp = fdopen(pipeinp[0], "r")) ||
      !(fpout = fdopen(pipeout[1], "w")))
    halt();
  wait_for_prompt();

  /* Skip mail header */

  for (;;) {
    if (!fgets(line, sizeof(line), stdin))
      halt();
    if (*line == '\n')
      break;
  }

  /* Process BBS messages */

  for (;;) {

    /* Skip to next S command, terminate on EOF */

    for (;;) {
      if (!fgets(line, sizeof(line), stdin))
	return 0;               /* Hit EOF */
      if (*line == '\n')
	continue;
      if (*line == 'S')
	break;
      halt();
    }

    /* Offer msg to BBS */

    fputs(line, fpout);
    fputs(line, stderr);

    /* Get reply */

    fflush(fpout);
    if (!fgets(line, sizeof(line), fpinp))
      halt();
    fputs(line, stderr);
    switch (*line) {
    case 'O':
    case 'o':
      ok = 1;
      break;
    case 'N':
    case 'n':
      ok = 0;
      break;
    default:
      halt();
    }

    /* Read rest of msg, forward if ok */

    for (;;) {
      if (!fgets(line, sizeof(line), stdin))
	halt();
      if (ok && strcmp(line, "NNNN\n")) {
	if (!strncmp(line, ">From", 5)) {
	  fputs(line + 1, fpout);
	  fputs(line + 1, stderr);
	} else {
	  fputs(line, fpout);
	  fputs(line, stderr);
	}
      }
      if (!strcmp(line, "/EX\n"))
	break;
    }

    wait_for_prompt();

  }

  return 0;
}
