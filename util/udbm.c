#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Attic/udbm.c,v 1.39 1995/06/04 09:36:44 deyke Exp $";
#endif

/* User Data Base Manager */

#define DEBUG           0

#include <sys/types.h>

#include <stdio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef ibm032

#include <sys/dir.h>

#define dirent direct

#else

#include <dirent.h>

#endif

#ifdef __cplusplus
extern "C" int putpwent(const struct passwd *p, FILE *f);
#endif

#include "calc_crc.h"
#include "callvalid.h"
#include "configure.h"

struct user {
  struct user *next;
  const char *call;
  const char *name;
  const char *street;
  const char *city;
  const char *qth;
  const char *phone;
  const char *mail;
  char alias;
};

#define NUM_USERS 8191

#if DEBUG
static const char usersfile[] = "users";
static const char userstemp[] = "users.tmp";
static const char passfile[]  = "passwd";
static const char passtemp[]  = "ptmp";
static const char spassfile[] = "spasswd";
static const char aliasfile[] = "aliases";
static const char aliastemp[] = "aliases.tmp";
#else
static const char usersfile[] = "/usr/local/lib/users";
static const char userstemp[] = "/usr/local/lib/users.tmp";
static const char passfile[]  = "/etc/passwd";
static const char passtemp[]  = "/etc/ptmp";
static const char spassfile[] = "/.secure/etc/passwd";
static const char aliasfile[] = ALIASES_FILE;
static const char aliastemp[] = ALIASES_FILE ".tmp";
#endif

static const char *lockfile;
static const char null_string[] = "";
static long heapsize;
static struct user *users[NUM_USERS];
static struct user null_user;

/*---------------------------------------------------------------------------*/

static void terminate(const char *s)
{
  perror(s);
  if (lockfile) remove(lockfile);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void *allocate(size_t size)
{

  static char *freespace;
  static size_t allocsize = 64*1024-2;
  static size_t freesize;
  void * p;

  size = (size + 3) & ~3;
  if (size > freesize) {
    for (; ; ) {
      if (size > allocsize) {
	errno = ENOMEM;
	terminate("allocate()");
      }
      if ((freespace = (char *) malloc(allocsize))) {
	freesize = allocsize;
	heapsize += allocsize;
	break;
      }
      allocsize >>= 1;
    }
  }
  p = freespace;
  freespace += size;
  freesize -= size;
  return p;
}

/*---------------------------------------------------------------------------*/

static const char *strsave(const char *s)
{

#define NUM_STRINGS 4999

  struct strings {
    struct strings *next;
    char s[1];
  };

  int hash;
  static struct strings *strings[NUM_STRINGS];
  struct strings *p;

  if (!*s) return null_string;
  for (p = strings[hash = calc_crc_16(s) % NUM_STRINGS];
       p && strcmp(s, p->s);
       p = p->next)
    ;
  if (!p) {
    p = (struct strings *) allocate(sizeof(struct strings *) + strlen(s) + 1);
    strcpy(p->s, s);
    p->next = strings[hash];
    strings[hash] = p;
  }
  return p->s;
}

/*---------------------------------------------------------------------------*/

static char *strlwc(char *s)
{
  char *p;

  for (p = s; *p; p++)
    if (*p >= 'A' && *p <= 'Z') *p = tolower(*p);
  return s;
}

/*---------------------------------------------------------------------------*/

static char *rmspaces(char *s)
{
  char *f, *t;

  for (f = t = s; *f; f++)
    if (*f != ' ') *t++ = *f;
  *t = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *strtrim(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && *p == ' ') ;
  p[1] = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static int is_qth(const char *s)
{
  switch (strlen(s)) {
  case 5:
    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) &&
	((s[1] >= 'A' && s[1] <= 'Z') || (s[1] >= 'a' && s[1] <= 'z')) &&
	((s[2] >= '0' && s[2] <= '8')                                ) &&
	((s[3] >= '0' && s[3] <= '9')                                ) &&
	((s[4] >= 'A' && s[4] <= 'J') || (s[4] >= 'a' && s[4] <= 'j')) &&
	  s[4] != 'I' && s[4] != 'i')
      return 1;
    break;
  case 6:
    if (((s[0] >= 'A' && s[0] <= 'R') || (s[0] >= 'a' && s[0] <= 'r')) &&
	((s[1] >= 'A' && s[1] <= 'R') || (s[1] >= 'a' && s[1] <= 'r')) &&
	((s[2] >= '0' && s[2] <= '9')                                ) &&
	((s[3] >= '0' && s[3] <= '9')                                ) &&
	((s[4] >= 'A' && s[4] <= 'X') || (s[4] >= 'a' && s[4] <= 'x')) &&
	((s[5] >= 'A' && s[5] <= 'X') || (s[5] >= 'a' && s[5] <= 'x')))
      return 1;
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int is_phone(const char *s)
{
  int slash;

  for (slash = 0; *s; s++)
    if (*s == '/')
      slash++;
    else if (!isdigit(*s & 0xff))
      return 0;
  return (slash == 1);
}

/*---------------------------------------------------------------------------*/

static int is_mail(const char *s)
{
  return (int) (strchr(s, '@') != 0);
}

/*---------------------------------------------------------------------------*/

static int join(const char **s1, const char **s2)
{
  if (s1 == s2) return 0;
  if (*s1 == null_string || strstr(*s2, *s1)) {
    *s1 = *s2;
    return 0;
  }
  if (*s2 == null_string || strstr(*s1, *s2)) {
    *s2 = *s1;
    return 0;
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

static struct user *getup(const char *call, int create)
{

  int hash;
  struct user *up;

  for (up = users[hash = calc_crc_16(call) % NUM_USERS];
       up && strcmp(call, up->call);
       up = up->next)
    ;
  if (create && !up) {
    up = (struct user *) allocate(sizeof(struct user));
    *up = null_user;
    up->call = strsave(call);
    up->next = users[hash];
    users[hash] = up;
  }
  return up;
}

/*---------------------------------------------------------------------------*/

static FILE *fopenexcl(const char *path)
{

  FILE * fp;
  int fd, cnt;

  for (cnt = 1; ; cnt++) {
    if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) >= 0) break;
    if (cnt >= 10) terminate(path);
    sleep(cnt);
  }
  lockfile = path;
  fp = fdopen(fd, "w");
  if (!fp) terminate(path);
  return fp;
}

/*---------------------------------------------------------------------------*/

static int string_is_numeric(const char *s)
{
  for (; *s; s++)
    if (!isdigit(*s & 0xff))
      return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

static void parse_mybbs_messages(void)
{

  char from[1024];
  char line[1024];
  char mybbs[1024];
  char *cp;
  DIR *dirp;
  FILE *fp;
  int maxnum;
  int minnum;
  int num;
  int timestamp;
  struct dirent *dp;
  struct user *up;

  if (!*NEWS_DIR || !(dirp = opendir(NEWS_DIR "/ampr/bbs/m")))
    return;
  maxnum = 0;
  minnum = 0x7fffffff;
  while ((dp = readdir(dirp))) {
    if (string_is_numeric(dp->d_name)) {
      num = atoi(dp->d_name);
      if (minnum > num)
	minnum = num;
      if (maxnum < num)
	maxnum = num;
    }
  }
  closedir(dirp);

  for (num = minnum; num <= maxnum; num++) {
    sprintf(line, NEWS_DIR "/ampr/bbs/m/%d", num);
    if (!(fp = fopen(line, "r")))
      continue;
    *from = 0;
    *mybbs = 0;
    while (fgets(line, sizeof(line), fp)) {
      if (!strncmp(line, "From: ", 6)) {
	sscanf(line, "From: %s", from);
	if ((cp = strchr(from, '@')))
	  *cp = 0;
#if !DEBUG
	if (!callvalid(from))
	  *from = 0;
#endif
      }
      if (!strncmp(line, "Subject: ", 9)) {
	sscanf(line, "Subject: %s %d", mybbs, &timestamp);
	if (!callvalid(mybbs))
	  *mybbs = 0;
      }
      if (*from && *mybbs) {
	up = getup(strlwc(from), 1);
	*line = '@';
	strcpy(line + 1, strlwc(mybbs));
	up->mail = strsave(line);
	break;
      }
    }
    fclose(fp);
  }
}

/*---------------------------------------------------------------------------*/

static void output_line(const struct user *up, FILE *fp)
{

#define append(s)                    \
  if (*(s)) {                        \
    if (*line) {                     \
      *t++ = ',';                    \
      *t++ = ' ';                    \
    }                                \
    for (f = (s); *f; *t++ = *f++) ; \
  }

  char line[1024];
  const char *f;
  char *t;

  t = line;
  *t = 0;
  append(up->call);
  append(up->name);
  append(up->street);
  append(up->city);
  append(up->qth);
  append(up->phone);
  append(up->mail);
  *t = 0;
  fputs(line, fp);
  putc('\n', fp);
}

/*---------------------------------------------------------------------------*/

static int fixusers(void)
{

#define NF 20

  FILE *fpi, *fpo;
  char *cp;
  char *f, *t;
  char *field[NF];
  char hostname[1024];
  char line[1024], orig_line[1024];
  int errors = 0;
  int i, nf;
  struct user *up;
  struct user user;

  if (!(fpi = fopen(usersfile, "r"))) terminate(usersfile);
  fpo = fopenexcl(userstemp);
  while (fgets(line, sizeof(line), fpi)) {
    for (f = line; *f; f++)
      if (isspace(*f & 0xff)) *f = ' ';
    for (t = f = line; *f; f++)
      if (*f != ' ' || f[1] != ' ') *t++ = *f;
    if (t > line && t[-1] == ' ') t--;
    *t = 0;
    strcpy(orig_line, line);
    f = line;
    memset((char *) field, 0 , sizeof(field));
    nf = 0;
    user = null_user;
    for (; ; ) {
      while (*f && (*f == ' ' || *f == ',')) f++;
      if (!*f) break;
      field[nf++] = f;
      f = strchr(f, ',');
      if (f)
	*f++ = 0;
      else
	f = "";
      strtrim(field[nf-1]);
    }
    if (!nf) continue;
    for (i = 0; i < NF; i++)
      if (field[i]) {
	if (!*user.call && callvalid(field[i])) {
	  user.call = strsave(strlwc(field[i]));
	  field[i] = 0;
	  nf--;
	} else if (!*user.qth && is_qth(field[i])) {
	  user.qth = strsave(strlwc(field[i]));
	  field[i] = 0;
	  nf--;
	} else if (!*user.phone && is_phone(field[i])) {
	  user.phone = strsave(field[i]);
	  field[i] = 0;
	  nf--;
	} else if (!*user.mail && is_mail(field[i])) {
	  user.mail = strsave(strlwc(rmspaces(field[i])));
	  field[i] = 0;
	  nf--;
	}
      }
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.name = strsave(field[i]);
	  field[i] = 0;
	  nf--;
	  break;
	}
    if (nf >= 2)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.street = strsave(field[i]);
	  field[i] = 0;
	  nf--;
	  break;
	}
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.city = strsave(field[i]);
	  field[i] = 0;
	  nf--;
	  break;
	}
    if (nf) {
      errors++;
      fprintf(stderr, "***** Too many fields *****\n%s\n\n", orig_line);
      fputs(orig_line, fpo);
      putc('\n', fpo);
      continue;
    }
    if (!*user.call) {
#if 0
      fputs(orig_line, fpo);
      putc('\n', fpo);
#endif
      continue;
    }
    up = getup(user.call, 1);
    if (join(&up->name, &user.name)     |
	join(&up->street, &user.street) |
	join(&up->city, &user.city)     |
	join(&up->qth, &user.qth)       |
	join(&up->phone, &user.phone)   |
	join(&up->mail, &user.mail)) {
      errors++;
      fprintf(stderr, "***** Join failed *****\n%s\n\n", orig_line);
      output_line(&user, fpo);
    }
  }
  fclose(fpi);

  parse_mybbs_messages();

  gethostname(hostname, sizeof(hostname));
  if ((cp = strchr(hostname, '.'))) *cp = 0;
  if (callvalid(hostname)) {
    up = getup(hostname, 1);
    *line = '@';
    strcpy(line + 1, up->call);
    up->mail = strsave(line);
  }

  for (i = 0; i < NUM_USERS; i++)
    for (up = users[i]; up; up = up->next) output_line(up, fpo);
  fclose(fpo);

  if (rename(userstemp, usersfile)) terminate(usersfile);
  lockfile = 0;
  return errors;
}

/*---------------------------------------------------------------------------*/

static void fixpasswd(void)
{

#if !(defined __386BSD__ || defined __bsdi__ || defined ibm032 || defined __FreeBSD__)

  FILE * fp;
  int secured = 0;
  struct passwd *pp;
  struct stat statbuf;
  struct user *up = 0;

#ifdef __hpux
  if (!stat(spassfile, &statbuf)) secured = 1;
#endif
  fp = fopenexcl(passtemp);
  while ((pp = getpwent())) {
    if (callvalid(pp->pw_name) &&
	(up = getup(pp->pw_name, 0)) &&
	*up->name)
      pp->pw_gecos = (char *) up->name;
    if (secured) pp->pw_passwd = "*";
    putpwent(pp, fp);
  }
  endpwent();
  fclose(fp);
  if (rename(passtemp, passfile)) terminate(passfile);
  lockfile = 0;

#endif

}

/*---------------------------------------------------------------------------*/

static void fixaliases(void)
{

  FILE *fpi, *fpo;
  char *p;
  char line[1024];
  int i;
  struct user *up;

  if (!*aliasfile) return;
  if (!(fpi = fopen(aliasfile, "r"))) terminate(aliasfile);
  fpo = fopenexcl(aliastemp);
  while (fgets(line, sizeof(line), fpi)) {
    if (!strncmp(line, "# Generated", 11)) break;
    fputs(line, fpo);
    if (isspace(*line & 0xff)) continue;
    if ((p = strchr(line, '#'))) *p = 0;
    if (!(p = strchr(line, ':'))) continue;
    while (--p >= line && isspace(*p & 0xff)) ;
    p[1] = 0;
    if ((up = getup(line, 0))) up->alias = 1;
  }
  fclose(fpi);
  fputs("# Generated aliases\n", fpo);
  for (i = 0; i < NUM_USERS; i++)
    for (up = users[i]; up; up = up->next)
      if (!up->alias && *up->mail)
	if (*up->mail == '@')
	  fprintf(fpo, "%s\t\t: %s%s\n", up->call, up->call, up->mail);
	else
	  fprintf(fpo, "%s\t\t: %s\n", up->call, up->mail);
  fclose(fpo);
  if (rename(aliastemp, aliasfile)) terminate(aliasfile);
  lockfile = 0;
}

/*---------------------------------------------------------------------------*/

int main(void)
{

  null_user.next = 0;
  null_user.call = null_string;
  null_user.name = null_string;
  null_user.street = null_string;
  null_user.city = null_string;
  null_user.qth = null_string;
  null_user.phone = null_string;
  null_user.mail = null_string;
  null_user.alias = 0;

  umask(022);
  if (!fixusers()) {
    fixpasswd();
    fixaliases();
#if !DEBUG
    if (*NEWALIASES_PROG) system("exec " NEWALIASES_PROG " >/dev/null 2>&1");
#endif
  }

#if DEBUG
  fprintf(stderr, "Total heap size = %ld Bytes\n", heapsize);
#endif

  return 0;
}

