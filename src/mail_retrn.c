/* @(#) $Id: mail_retrn.c,v 1.24 1996/08/12 18:51:17 deyke Exp $ */

/* Mail Delivery Agent for returned Mails */

#include <sys/types.h>

#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef ULTRIX_RISC
#include <sys/signal.h>
#endif

#include "configure.h"

#include "hpux.h"
#include "mail.h"

/*---------------------------------------------------------------------------*/

void mail_return(struct mailjob *jp)
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
  if ((fpi = fopen(jp->dfile, "r"))) {
    if (!*SENDMAIL_PROG) exit(1);
    sprintf(line, "%s -oi -oem -f '<>' %s", SENDMAIL_PROG, jp->from);
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
  remove(jp->cfile);
  remove(jp->dfile);
  if (*jp->xfile) remove(jp->xfile);
  exit(0);
}
