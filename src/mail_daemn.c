/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/mail_daemn.c,v 1.24 1995/12/20 09:46:49 deyke Exp $ */

/* Mail Daemon, checks for outbound mail and starts mail delivery agents */

#include <sys/types.h>

#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef ibm032
#include <sys/dir.h>
#define dirent direct
#else
#include <dirent.h>
#endif

#include "configure.h"

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

static const struct mailers Mailers[] = {
	{ "smtp",       mail_smtp },
	{ 0,            0 }
};

/*---------------------------------------------------------------------------*/

static struct cmds Mail_cmds[] = {
/*      "gateway",      dogateway,      0,      0,      NULL,           */
/*      "mode",         setsmtpmode,    0,      0,      NULL,           */
	"kick",         domail_kick,    0,      0,      NULL,
/*      "kill",         dosmtpkill,     0,      2,      "kill <jobnumber>", */
	"list",         domail_list,    0,      0,      NULL,
	"maxclients",   domail_maxcli,  0,      0,      NULL,
	"timer",        domail_timer,   0,      0,      NULL,
#ifdef SMTPTRACE
/*      "trace",        dosmtptrace,    0,      0,      NULL,           */
#endif
	NULL,
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
    *waittime = 0;
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
	sprintf(waittime, "%ld sec", sp->nexttime - secclock());
      break;
    case MS_TRYING:
      state = "Trying";
      break;
    case MS_TALKING:
      state = "Talking";
      break;
    default:
      state = "?";
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
    printf("%ld/%ld\n",
	    read_timer(&Mail_timer) / 1000,
	    dur_timer(&Mail_timer) / 1000);
    return 0;
  }
  Mail_timer.func = (void (*)(void *)) mail_tick;
  Mail_timer.arg = 0;
  set_timer(&Mail_timer, atol(argv[1]) * 1000L);
  start_timer(&Mail_timer);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int domail_kick(int argc, char *argv[], void *p)
{
  mail_tick((argc < 2) ? NULL : argv[1]);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void strtrim(char *s)
{
  char *p = s;

  while (*p) p++;
  while (--p >= s && isspace(*p & 0xff)) ;
  p[1] = 0;
}

/*---------------------------------------------------------------------------*/

static void read_configuration(void)
{

  FILE *fp;
  char *cp;
  char *sysname, *mailername, *protocol, *address;
  char line[1024];
  const struct mailers *mailer;
  static int lastmtime;
  struct mailsys *sp;
  struct stat statbuf;

  for (sp = Systems; sp; sp = sp->next)
    if (sp->state >= MS_TRYING) return;
  if (stat(CONFFILE, &statbuf)) return;
  if (lastmtime == statbuf.st_mtime || statbuf.st_mtime > secclock() - 5) return;
  if (!(fp = fopen(CONFFILE, "r"))) return;
  while ((sp = Systems)) {
    Systems = Systems->next;
    free(sp->sysname);
    free(sp->protocol);
    free(sp->address);
    free(sp);
  }
  while (fgets(line, sizeof(line), fp)) {
    if ((cp = strchr(line, '#'))) *cp = 0;
    if (!(sysname = strtok(line, ":"))) continue;
    if (!(mailername = strtok(NULL, ":"))) continue;
    if (!(protocol = strtok(NULL, ":"))) continue;
    if (!(address = strtok(NULL, ":"))) continue;
    strtrim(address);
    for (mailer = Mailers; mailer->name; mailer++)
      if (!strcmp(mailer->name, mailername)) break;
    if (!mailer->name) continue;
    sp = (struct mailsys *) calloc(1, sizeof(struct mailsys));
    sp->sysname = strdup(sysname);
    sp->mailer = mailer;
    sp->protocol = strdup(protocol);
    sp->address = strdup(address);
    sp->next = Systems;
    Systems = sp;
  }
  fclose(fp);
  lastmtime = (int) statbuf.st_mtime;
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
  char tmp3[1024];
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

  if (!*UUCP_DIR) return;

  start_timer(&Mail_timer);
  read_configuration();

  for (clients = 0, sp = Systems; sp; sp = sp->next)
    if (sp->state >= MS_TRYING) clients++;
  for (sp = Systems; sp && clients < Maxclients; sp = sp->next) {
    if (sysname && !strcmp(sp->sysname, sysname)) sp->nexttime = 0;
    if (sp->state >= MS_TRYING || sp->nexttime > secclock()) continue;
    sprintf(spooldir, "%s/%s", UUCP_DIR, sp->sysname);
    if (!(dirp = opendir(spooldir))) continue;
    filelist = 0;
    cnt = 0;
    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
      if (*dp->d_name != 'C') continue;
      p = (struct filelist *) malloc(sizeof(struct filelist));
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
	for (q = 0, p = filelist; p->next; q = p, p = p->next) ;
	q->next = 0;
	free(p);
	cnt--;
      }
    }
    closedir(dirp);
    tail = 0;
    for (; (p = filelist); filelist = p->next, free(p)) {
      memset(&mj, 0, sizeof(mj));
      sprintf(mj.cfile, "%s/%s", spooldir, p->name);
      if (!(fp = fopen(mj.cfile, "r"))) continue;
      while (fgets(line, sizeof(line), fp)) {
	if (*line == 'E' && sscanf(line, "%*s %*s %*s %*s %*s %s %*s %*s %*s %s %s", tmp1, tmp2, tmp3) == 3 && *tmp1 == 'D' && !strcmp(tmp2, "rmail")) {
	  sprintf(mj.dfile, "%s/%s", spooldir, tmp1);
	  sprintf(mj.to, "%s!%s", sp->sysname, tmp3);
	  strtrim(mj.to);
	}
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'D')
	  sprintf(mj.dfile, "%s/%s", spooldir, tmp2);
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'X')
	  sprintf(mj.xfile, "%s/%s", spooldir, tmp2);
      }
      fclose(fp);
      if (!*mj.dfile) continue;
      if (*mj.xfile) {
	if (!(fp = fopen(mj.xfile, "r"))) continue;
	while (fgets(line, sizeof(line), fp))
	  if (!strncmp(line, "C rmail ", 8)) {
	    sprintf(mj.to, "%s!%s", sp->sysname, line + 8);
	    strtrim(mj.to);
	    break;
	  }
	fclose(fp);
      }
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
	sprintf(mj.return_reason, "520 %s... Cannot connect for %ld days\n", sp->sysname, RETURNTIME / (60L*60*24));
	mail_return(&mj);
      } else {
	jp = (struct mailjob *) malloc(sizeof(struct mailjob));
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
