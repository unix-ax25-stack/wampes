/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_retrn.c,v 1.9 1992/09/25 20:07:19 deyke Exp $ */

/* Mail Delivery Agent for returned Mails */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "hpux.h"
#include "mail.h"

/*---------------------------------------------------------------------------*/

void mail_return(jp)
struct mailjob *jp;
{

  FILE *fpi, *fpo;
  char line[1024];
  int i;

  if (dofork()) return;
  for (i = 0; i < FD_SETSIZE; i++) close(i);
  setsid();
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  if (fpi = fopen(jp->dfile, "r")) {
    sprintf(line, "/usr/lib/sendmail -oi -oem -f '<>' %s", jp->from);
    if (!(fpo = popen(line, "w"))) exit(1);
    fprintf(fpo, "From: Mail Delivery Subsystem <MAILER-DAEMON>\n");
    fprintf(fpo, "To: %s\n", jp->from);
    fprintf(fpo, "Subject: Returned mail\n");
    putc('\n', fpo);
    fprintf(fpo, "   ----- Transcript of session follows -----\n");
    fprintf(fpo, "%s\n", jp->return_reason);
    putc('\n', fpo);
    fprintf(fpo, "   ----- Unsent message follows -----\n");
    fgets(line, sizeof(line), fpi);
    while (fgets(line, sizeof(line), fpi)) fputs(line, fpo);
    pclose(fpo);
    fclose(fpi);
  }
  unlink(jp->cfile);
  unlink(jp->dfile);
  unlink(jp->xfile);
  exit(0);
}

