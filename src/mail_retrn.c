/* Mail Delivery Agent for returned Mails */

#include <stdio.h>
#include <sys/rtprio.h>

#include "global.h"
#include "mail.h"

extern void _exit();

/*---------------------------------------------------------------------------*/

void mail_return(jp)
struct mailjob *jp;
{

  FILE * fpi, *fpo;
  char  line[1024];
  int  i;

  if (fork()) return;
  rtprio(0, RTPRIO_RTOFF);
  for (i = 0; i < _NFILE; i++) close(i);
  setpgrp();
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  fopen("/dev/null", "r+");
  sprintf(line, "/usr/lib/sendmail -oi -oem -f '<>' %s", jp->from);
  if (!(fpo = popen(line, "w"))) _exit(1);
  fprintf(fpo, "From: Mail Delivery Subsystem <MAILER-DAEMON>\n");
  fprintf(fpo, "To: %s\n", jp->from);
  fprintf(fpo, "Subject: Returned mail\n");
  putc('\n', fpo);
  fprintf(fpo, "   ----- Transcript of session follows -----\n");
  fprintf(fpo, "%s\n", jp->return_reason);
  putc('\n', fpo);
  fprintf(fpo, "   ----- Unsent message follows -----\n");
  fpi = fopen(jp->dfile, "r");
  fgets(line, sizeof(line), fpi);
  while (fgets(line, sizeof(line), fpi)) fputs(line, fpo);
  fclose(fpi);
  pclose(fpo);
  unlink(jp->cfile);
  unlink(jp->dfile);
  unlink(jp->xfile);
  _exit(0);
}

