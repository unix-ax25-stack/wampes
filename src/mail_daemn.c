/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_daemn.c,v 1.13 1993/05/17 13:45:08 deyke Exp $ */

/* Mail Daemon, checks for outbound mail and starts mail delivery agents */

#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "global.h"
#include "timer.h"
#include "mail.h"
#include "cmdparse.h"
#include "commands.h"

static int Maxclients = 10;
static struct mailsys *Systems;
static struct timer Mail_timer;

static int domail_maxcli(int argc, char *argv[], void *p);
static int domail_list(int argc, char *argv[], void *p);
static int domail_timer(int argc, char *argv[], void *p);
static int domail_kick(int argc, char *argv[], void *p);
static void strtrim(char *s);
static void read_configuration(void);
static void mail_tick(char *sysname);

/*---------------------------------------------------------------------------*/

static struct mailers Mailers[] = {
	"bbs",          mail_bbs,
	"smtp",         mail_smtp,
	NULLCHAR
};

/*---------------------------------------------------------------------------*/

static struct cmds Mail_cmds[] = {
/*      "gateway",      dogateway,      0,      0,      NULLCHAR,           */
/*      "mode",         setsmtpmode,    0,      0,      NULLCHAR,           */
	"kick",         domail_kick,    0,      0,      NULLCHAR,
/*      "kill",         dosmtpkill,     0,      2,      "kill <jobnumber>", */
	"list",         domail_list,    0,      0,      NULLCHAR,
	"maxclients",   domail_maxcli,  0,      0,      NULLCHAR,
	"timer",        domail_timer,   0,      0,      NULLCHAR,
#ifdef SMTPTRACE
/*      "trace",        dosmtptrace,    0,      0,      NULLCHAR,           */
#endif
	NULLCHAR,
};

int dosmtp(int argc, char *argv[], void *p)
{
  read_configuration();
  return subcmd(Mail_cmds, argc, argv, p);
}

/*---------------------------------------------------------------------------*/

static int domail_maxcli(int argc, char *argv[], void *p)
{
  return setint(&Maxclients, "Max clients", argc, argv);
}

/*---------------------------------------------------------------------------*/

static int domail_list(int argc, char *argv[], void *p)
{

  char *state;
  char waittime[16];
  struct mailsys *sp;

  printf("System     Mailer  Transport  State    Wait time\n");
  for (sp = Systems; sp; sp = sp->next) {
    *waittime = '\0';
    switch (sp->state) {
    case MS_NEVER:
      state = "";
      break;
    case MS_SUCCESS:
      state = "Success";
      break;
    case MS_FAILURE:
      state = "Failure";
      if (sp->nexttime > secclock())
	sprintf(waittime, "%d sec", sp->nexttime - secclock());
      break;
    case MS_TRYING:
      state = "Trying";
      break;
    case MS_TALKING:
      state = "Talking";
      break;
    }
    printf("%-10s %-7s %-10s %-8s %9s\n", sp->sysname, sp->mailer->name, sp->protocol, state, waittime);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* Set outbound spool scan interval */

static int domail_timer(int argc, char *argv[], void *p)
{
  if (argc < 2) {
    printf("%lu/%lu\n",
	    read_timer(&Mail_timer) / 1000,
	    dur_timer(&Mail_timer) / 1000);
    return 0;
  }
  Mail_timer.func = (void (*)()) mail_tick;
  Mail_timer.arg = 0;
  set_timer(&Mail_timer, atol(argv[1]) * 1000L);
  start_timer(&Mail_timer);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int domail_kick(int argc, char *argv[], void *p)
{
  mail_tick((argc < 2) ? NULLCHAR : argv[1]);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void strtrim(char *s)
{
  char *p = s;

  while (*p) p++;
  while (--p >= s && isspace(uchar(*p))) ;
  p[1] = '\0';
}

/*---------------------------------------------------------------------------*/

static void read_configuration(void)
{

  FILE *fp;
  char *sysname, *mailername, *protocol, *address;
  char line[1024];
  static long lastmtime;
  struct mailers *mailer;
  struct mailsys *sp;
  struct stat statbuf;

  for (sp = Systems; sp; sp = sp->next)
    if (sp->state >= MS_TRYING) return;
  if (stat(CONFFILE, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > secclock() - 5) return;
  if (!(fp = fopen(CONFFILE, "r"))) return;
  while (sp = Systems) {
    Systems = Systems->next;
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
    for (mailer = Mailers; mailer->name; mailer++)
      if (!strcmp(mailer->name, mailername)) break;
    if (!mailer->name) continue;
    sp = calloc(1, sizeof(*sp));
    sp->sysname = strdup(sysname);
    sp->mailer = mailer;
    sp->protocol = strdup(protocol);
    sp->address = strdup(address);
    sp->next = Systems;
    Systems = sp;
  }
  fclose(fp);
  lastmtime = statbuf.st_mtime;
}

/*---------------------------------------------------------------------------*/

static void mail_tick(char *sysname)
{

  DIR *dirp;
  FILE *fp;
  char line[1024];
  char spooldir[80];
  char tmp1[1024];
  char tmp2[1024];
  int clients;
  int cnt;
  struct dirent *dp;
  struct mailjob mj, *jp, *tail;
  struct mailsys *sp;
  struct stat statbuf;

  struct filelist {
    struct filelist *next;
    char name[16];
  } *filelist, *p, *q;

  start_timer(&Mail_timer);
  read_configuration();

  for (clients = 0, sp = Systems; sp; sp = sp->next)
    if (sp->state >= MS_TRYING) clients++;
  for (sp = Systems; sp && clients < Maxclients; sp = sp->next) {
    if (sysname && !strcmp(sp->sysname, sysname)) sp->nexttime = 0;
    if (sp->state >= MS_TRYING || sp->nexttime > secclock()) continue;
    sprintf(spooldir, "%s/%s", SPOOLDIR, sp->sysname);
    if (!(dirp = opendir(spooldir))) continue;
    filelist = 0;
    cnt = 0;
    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
      if (*dp->d_name != 'C') continue;
      p = malloc(sizeof(*p));
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
      memset((char *) &mj, 0, sizeof(mj));
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
	if (!strcmp(tmp1, "MAILER-DAEMON") || !strcmp(tmp1, "!"))
	  strcpy(tmp1, Hostname);
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
      if (statbuf.st_mtime + RETURNTIME < secclock()) {
	sprintf(mj.return_reason, "520 %s... Cannot connect for %d days\n", sp->sysname, RETURNTIME / (60L*60*24));
	mail_return(&mj);
      } else {
	jp = malloc(sizeof(*jp));
	*jp = mj;
	if (!sp->jobs)
	  sp->jobs = jp;
	else
	  tail->next = jp;
	tail = jp;
      }
    }
    if (sp->jobs) {
      sp->state = MS_TRYING;
      clients++;
      (*sp->mailer->func)(sp);
    }
  }
}

