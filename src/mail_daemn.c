/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_daemn.c,v 1.4 1990/10/12 19:26:07 deyke Exp $ */

/* Mailer Daemon, checks for outbound mail and starts mail delivery agents */

#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "timer.h"
#include "hpux.h"
#include "mail.h"

static struct mailsys *systems;
static struct timer timer;

static void strtrim __ARGS((char *s));
static void read_configuration __ARGS((void));

/*---------------------------------------------------------------------------*/

static void strtrim(s)
char  *s;
{
  char  *p = s;

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
  void (*mailer) __ARGS((struct mailsys *));

  for (sp = systems; sp; sp = sp->next)
    if (sp->jobs) return;
  if (stat(CONFFILE, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > currtime - 5) return;
  if (!(fp = fopen(CONFFILE, "r"))) return;
  while (sp = systems) {
    systems = systems->next;
    free(sp->sysname);
    free(sp->protocol);
    free(sp->address);
    free(sp);
  }
  while (fgets(line, sizeof(line), fp)) {
    if (!(sysname = strtok(line, ":"))) continue;
    if (!(mailername = strtok(NULLCHAR, ":"))) continue;
    if (!(protocol = strtok(NULLCHAR, ":"))) continue;
    if (!(address = strtok(NULLCHAR, ":"))) continue;
    strtrim(address);
    if (!strcmp(mailername, "bbs"))
      mailer = mail_bbs;
    else if (!strcmp(mailername, "smtp"))
      mailer = mail_smtp;
    else
      continue;
    sp = (struct mailsys *) calloc(1, sizeof (struct mailsys ));
    sp->sysname = strdup(sysname);
    sp->mailer = mailer;
    sp->protocol = strdup(protocol);
    sp->address = strdup(address);
    sp->next = systems;
    systems = sp;
  }
  fclose(fp);
  lastmtime = statbuf.st_mtime;
}

/*---------------------------------------------------------------------------*/

int  mail_daemon(argc, argv, argp)
int  argc;
char  *argv[];
void *argp;
{

  DIR * dirp;
  FILE * fp;
  char  line[1024];
  char  spooldir[80];
  char  tmp1[1024];
  char  tmp2[1024];
  int  cnt;
  struct dirent *dp;
  struct mailjob mj, *jp, *tail;
  struct mailsys *sp;
  struct stat statbuf;

  struct filelist {
    struct filelist *next;
    char  name[16];
  } *filelist, *p, *q;

  timer.func = (void (*)()) mail_daemon;
  timer.arg = NULLCHAR;
  set_timer(&timer, POLLTIME * 1000l);
  start_timer(&timer);

  read_configuration();

  for (sp = systems; sp; sp = sp->next) {
    if (sp->jobs || sp->nexttime > currtime) continue;
    sprintf(spooldir, "%s/%s", SPOOLDIR, sp->sysname);
    if (!(dirp = opendir(spooldir))) continue;
    filelist = 0;
    cnt = 0;
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
      if (++cnt > MAXJOBS) {
	for (p = filelist; p->next; q = p, p = p->next) ;
	q->next = 0;
	free(p);
	cnt--;
      }
    }
    closedir(dirp);
    tail = 0;
    for (; p = filelist; filelist = p->next, free(p)) {
      memset(&mj, 0, sizeof(struct mailjob ));
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
	if (!strcmp(tmp1, "MAILER-DAEMON")) strcpy(tmp1, Hostname);
	sprintf(mj.from, "%s!%s", Hostname, tmp1);
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
      if (stat(mj.cfile, &statbuf)) continue;
      if (statbuf.st_mtime + RETURNTIME < currtime) {
	sprintf(mj.return_reason, "520 %s... Cannot connect for %d days\n", sp->sysname, RETURNTIME / (60l*60*24));
	mail_return(&mj);
      } else {
	jp = (struct mailjob *) memcpy(malloc(sizeof(struct mailjob )), &mj, sizeof(struct mailjob ));
	if (!sp->jobs)
	  sp->jobs = jp;
	else
	  tail->next = jp;
	tail = jp;
      }
    }
    if (sp->jobs) (*sp->mailer)(sp);
  }
}

