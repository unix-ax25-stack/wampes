/* Mailer Daemon, checks for outbound mail and starts mail delivery agents */

#include <sys/types.h>

#include <ctype.h>
#include <memory.h>
#include <ndir.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "timer.h"
#include "hpux.h"
#include "mail.h"

extern char  hostname[];
extern void free();

static struct mailsys *mailsys;
static struct timer timer;

/*---------------------------------------------------------------------------*/

static void strtrim(s)
register char  *s;
{
  register char  *p = s;

  while (*p) p++;
  while (--p >= s && isspace(uchar(*p))) ;
  p[1] = '\0';
}

/*---------------------------------------------------------------------------*/

static void read_configuration()
{

  FILE * fp;
  char  *sysname, *mailername, *protocol, *address;
  char  line[1024];
  static long  lastmtime;
  struct mailsys *sp;
  struct stat statbuf;
  void (*mailer)();

  for (sp = mailsys; sp; sp = sp->nextsys) if (sp->nextjob) return;
  if (stat(CONFFILE, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > currtime - 5) return;
  lastmtime = statbuf.st_mtime;
  while (sp = mailsys) {
    mailsys = mailsys->nextsys;
    free(sp->sysname);
    free(sp->protocol);
    free(sp->address);
    free((char *) sp);
  }
  if (!(fp = fopen(CONFFILE, "r"))) return;
  while (fgets(line, sizeof(line), fp)) {
    sysname = line;
    if (mailername = strchr(sysname, ':')) *mailername++ = '\0'; else continue;
    if (protocol = strchr(mailername, ':')) *protocol++ = '\0'; else continue;
    if (address = strchr(protocol, ':')) *address++ = '\0'; else continue;
    strtrim(address);
    if (!strcmp(mailername, "bbs"))
      mailer = mail_bbs;
    else if (!strcmp(mailername, "smtp"))
      mailer = mail_smtp;
    else
      continue;
    sp = (struct mailsys *) calloc(1, sizeof (struct mailsys ));
    sp->sysname = strcpy(malloc((unsigned) (strlen(sysname) + 1)), sysname);
    sp->mailer = mailer;
    sp->protocol = strcpy(malloc((unsigned) (strlen(protocol) + 1)), protocol);
    sp->address = strcpy(malloc((unsigned) (strlen(address) + 1)), address);
    sp->nextsys = mailsys;
    mailsys = sp;
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

mail_daemon()
{

  DIR * dirp;
  FILE * fp;
  char  line[1024];
  char  spooldir[80];
  char  tmp1[1024];
  char  tmp2[1024];
  struct direct *dp;
  struct mailjob mj, *jp;
  struct mailsys *sp;
  struct stat statbuf;

  struct filelist {
    struct filelist *next;
    char  name[15];
  } *filelist, *p, *q;

  read_configuration();
  if (!mailsys) return;

  timer.start = POLLTIME * 1000l / MSPTICK;
  timer.func = (void (*)()) mail_daemon;
  timer.arg = NULLCHAR;
  start_timer(&timer);

  for (sp = mailsys; sp; sp = sp->nextsys) {
    if (sp->nextjob || sp->nexttime > currtime) continue;
    sprintf(spooldir, "%s/%s", SPOOLDIR, sp->sysname);
    if (!(dirp = opendir(spooldir))) continue;
    filelist = 0;
    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
      if (*dp->d_name != 'C') continue;
      p = (struct filelist *) malloc(sizeof(struct filelist ));
      strcpy(p->name, dp->d_name);
      if (!filelist || strcmp(p->name, filelist->name) < 0) {
	p->next = filelist;
	filelist = p;
      } else {
	for (q = filelist; q->next && strcmp(p->name, q->next->name) > 0; q = q->next) ;
	p->next = q->next;
	q->next = p;
      }
    }
    closedir(dirp);
    for (; p = filelist; filelist = p->next, free((char *) p)) {
      memset((char *) & mj, 0, sizeof(struct mailjob ));
      sprintf(mj.cfile, "%s/%s", spooldir, p->name);
      if (!(fp = fopen(mj.cfile, "r"))) continue;
      while (fgets(line, sizeof(line), fp)) {
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'D')
	  sprintf(mj.dfile, "%s/%s", spooldir, tmp2);
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'X')
	  sprintf(mj.xfile, "%s/%s", spooldir, tmp2);
      }
      fclose(fp);
      if (*mj.dfile == '\0' || *mj.xfile == '\0') continue;
      if (!(fp = fopen(mj.xfile, "r"))) continue;
      while (fgets(line, sizeof(line), fp))
	if (!strncmp(line, "C rmail ", 8)) {
	  sprintf(mj.to, "%s!%s", sp->sysname, line + 8);
	  strtrim(mj.to);
	  break;
	}
      fclose(fp);
      if (!*mj.to) continue;
      if (!(fp = fopen(mj.dfile, "r"))) continue;
      if (fscanf(fp, "%*s %s", tmp1) == 1) {
	if (!strcmp(tmp1, "MAILER-DAEMON")) strcpy(tmp1, hostname);
	sprintf(mj.from, "%s!%s", hostname, tmp1);
	strtrim(mj.from);
	while (fgets(line, sizeof(line), fp))
	  if (!strncmp(line, "Subject: ", 9)) {
	    strcpy(mj.subject, line + 9);
	    strtrim(mj.subject);
	    break;
	  }
      }
      fclose(fp);
      if (!*mj.from) continue;
      stat(mj.cfile, &statbuf);
      if (statbuf.st_mtime + RETURNTIME < currtime) {
	sprintf(mj.return_reason, "520 %s... Cannot connect for %d days\n", sp->sysname, RETURNTIME / (60l*60*24));
	mail_return(&mj);
      } else {
	jp = (struct mailjob *) memcpy(malloc(sizeof(struct mailjob )), (char *) & mj, sizeof(struct mailjob ));
	if (sp->nextjob)
	  sp->lastjob = sp->lastjob->nextjob = jp;
	else
	  sp->lastjob = sp->nextjob = jp;
	jp->nextjob = (struct mailjob *) 0;
      }
    }
    if (sp->nextjob) (*sp->mailer)(sp);
  }
}

