#ifndef __lint
static const char rcsid[] = "@(#) $Id: udbm.c,v 1.51 2000/02/05 23:54:48 deyke Exp $";
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

#if defined __cplusplus || defined __lint || defined __GNUC__
#if defined __cplusplus
extern "C"
#endif
int putpwent(const struct passwd *p, FILE *f);
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
static const char passtemp1[]  = "ptmp";
static const char passtemp2[]  = "passwd.tmp";
static const char spassfile[] = "spasswd";
static const char aliasfile[] = "aliases";
static const char aliastemp[] = "aliases.tmp";
#else
static const char usersfile[] = "/usr/local/lib/users";
static const char userstemp[] = "/usr/local/lib/users.tmp";
static const char passfile[]  = "/etc/passwd";
static const char passtemp1[]  = "/etc/ptmp";
static const char passtemp2[]  = "/etc/passwd.tmp";
static const char spassfile[] = "/.secure/etc/passwd";
static const char aliasfile[] = ALIASES_FILE;
static const char aliastemp[] = ALIASES_FILE ".tmp";
#endif

static const char Null_string[] = "";
static const char *Lockfile;
static long Heapsize;
static struct user Null_user;
static struct user *Users[NUM_USERS];

/*---------------------------------------------------------------------------*/

static void terminate(const char *s)
{
  perror(s);
  if (Lockfile) remove(Lockfile);
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
	Heapsize += allocsize;
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

static const char *strstore(const char *str)
{

#define NUM_STRINGS 4999

  struct strings {
    struct strings *next;
    /***************************** String value is stored here */
  };

#define strptr(p)       ((char *) ((p)+1))

  static struct strings *Strings[NUM_STRINGS];
  struct strings *p;
  struct strings **hp;

  if (!*str)
    return Null_string;
  hp = Strings + calc_crc_16(str) % NUM_STRINGS;
  for (p = *hp; p; p = p->next)
    if (!strcmp(strptr(p), str))
      return strptr(p);
  p = (struct strings *) allocate(sizeof(struct strings) + strlen(str) + 1);
  strcpy(strptr(p), str);
  p->next = *hp;
  *hp = p;
  return strptr(p);
}

#undef NUM_STRINGS
#undef strptr

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
  if (*s1 == Null_string || strstr(*s2, *s1)) {
    *s1 = *s2;
    return 0;
  }
  if (*s2 == Null_string || strstr(*s1, *s2)) {
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

  for (up = Users[hash = calc_crc_16(call) % NUM_USERS];
       up && strcmp(call, up->call);
       up = up->next)
    ;
  if (create && !up) {
    up = (struct user *) allocate(sizeof(struct user));
    *up = Null_user;
    up->call = strstore(call);
    up->next = Users[hash];
    Users[hash] = up;
  }
  return up;
}

/*---------------------------------------------------------------------------*/

static FILE *fopenexcl(const char *path)
{

  FILE *fp;
  int fd, cnt;

  for (cnt = 1; ; cnt++) {
    if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) >= 0) break;
    if (cnt >= 10) terminate(path);
    sleep(cnt);
  }
  Lockfile = path;
  fp = fdopen(fd, "w");
  if (!fp) terminate(path);
  return fp;
}

/*---------------------------------------------------------------------------*/

static void parse_mybbs_messages(void)
{

  char from[1024];
  char line[1024];
  char mybbs[1024];
  char tmp[1024];
  char *cp;
  const char *batchfile;
  const char *workfile;
  FILE *fpmsg;
  FILE *fpwrk;
  int i;
  int timestamp;
  struct stat statbuf;
  struct user *up;

  if (!*NEWS_DIR || !*CTLINND_PROG)
    return;
  batchfile = NEWS_DIR "/outgoing/udbm";
  workfile = NEWS_DIR "/outgoing/udbm.bbs";
  if (!(fpwrk = fopen(workfile, "r"))) {
    if (rename(batchfile, workfile))
      return;
    system(CTLINND_PROG " -s flush udbm");
    for (i = 0;; i++) {
      if (!stat(batchfile, &statbuf))
	break;
      if (i > 60)
	terminate(batchfile);
      sleep(1);
    }
    if (!(fpwrk = fopen(workfile, "r")))
      terminate(workfile);
  }
  while (fgets(line, sizeof(line), fpwrk)) {
    for (cp = line; *cp; cp++) {
      if (isspace(*cp & 0xff)) {
	*cp = 0;
	break;
      }
    }
    if (*line != '/') {
      sprintf(tmp, NEWS_DIR "/articles/%s", line);
      strcpy(line, tmp);
    }
    if (!(fpmsg = fopen(line, "r")))
      continue;
    *from = 0;
    *mybbs = 0;
    while (fgets(line, sizeof(line), fpmsg)) {
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
	if ((cp = strchr(mybbs, '.'))) {
	  *cp = 0;
	}
	if (!callvalid(mybbs))
	  *mybbs = 0;
      }
      if (*from && *mybbs) {
	up = getup(strlwc(from), 1);
	*line = '@';
	strcpy(line + 1, strlwc(mybbs));
	up->mail = strstore(line);
	break;
      }
    }
    fclose(fpmsg);
  }
  fclose(fpwrk);
#if !DEBUG
  if (remove(workfile))
    terminate(workfile);
#endif
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
    user = Null_user;
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
	  user.call = strstore(strlwc(field[i]));
	  field[i] = 0;
	  nf--;
	} else if (!*user.qth && is_qth(field[i])) {
	  user.qth = strstore(strlwc(field[i]));
	  field[i] = 0;
	  nf--;
	} else if (!*user.phone && is_phone(field[i])) {
	  user.phone = strstore(field[i]);
	  field[i] = 0;
	  nf--;
	} else if (!*user.mail && is_mail(field[i])) {
	  user.mail = strstore(strlwc(rmspaces(field[i])));
	  field[i] = 0;
	  nf--;
	}
      }
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.name = strstore(field[i]);
	  field[i] = 0;
	  nf--;
	  break;
	}
    if (nf >= 2)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.street = strstore(field[i]);
	  field[i] = 0;
	  nf--;
	  break;
	}
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.city = strstore(field[i]);
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
    up->mail = strstore(line);
  }

  for (i = 0; i < NUM_USERS; i++)
    for (up = Users[i]; up; up = up->next) output_line(up, fpo);
  fclose(fpo);

  if (rename(userstemp, usersfile)) terminate(usersfile);
  Lockfile = 0;
  return errors;
}

/*---------------------------------------------------------------------------*/

static void fixpasswd(void)
{

#if !(defined __386BSD__ || defined __NetBSD__|| defined __bsdi__ || defined ibm032 || defined __FreeBSD__)

  FILE *fp1;
  FILE *fp2;
  int secured = 0;
  struct passwd *pp;
  struct user *up = 0;

#ifdef __hpux
  {
    struct stat statbuf;
    if (!stat(spassfile, &statbuf)) {
      secured = 1;
    }
  }
#endif
  fp1 = fopenexcl(passtemp1);
  fp2 = fopenexcl(passtemp2);
  while ((pp = getpwent())) {
    if (callvalid(pp->pw_name) &&
	(up = getup(pp->pw_name, 0)) &&
	*up->name)
      pp->pw_gecos = (char *) up->name;
    if (secured)
      pp->pw_passwd = "*";
    putpwent(pp, fp1);
  }
  endpwent();
  fclose(fp2);
  fclose(fp1);
  if (rename(passtemp1, passfile)) {
    remove(passtemp2);
    remove(passtemp1);
    terminate(passfile);
  }
  if (remove(passtemp2))
    terminate(passtemp2);
  Lockfile = 0;

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
  for (i = 0; i < NUM_USERS; i++) {
    for (up = Users[i]; up; up = up->next) {
      if (!up->alias && *up->mail) {
	if (*up->mail == '@') {
	  fprintf(fpo, "%s\t\t: %s%s\n", up->call, up->call, up->mail);
	} else {
	  fprintf(fpo, "%s\t\t: %s\n", up->call, up->mail);
	}
      }
    }
  }
  fclose(fpo);
  if (rename(aliastemp, aliasfile)) terminate(aliasfile);
  Lockfile = 0;
}

/*---------------------------------------------------------------------------*/

int main(void)
{

  Null_user.next = 0;
  Null_user.call = Null_string;
  Null_user.name = Null_string;
  Null_user.street = Null_string;
  Null_user.city = Null_string;
  Null_user.qth = Null_string;
  Null_user.phone = Null_string;
  Null_user.mail = Null_string;
  Null_user.alias = 0;

  umask(022);
  if (!fixusers()) {
    fixpasswd();
    fixaliases();
#if !DEBUG
    if (*NEWALIASES_PROG) system("exec " NEWALIASES_PROG " >/dev/null 2>&1");
#endif
  }

#if DEBUG
  fprintf(stderr, "Total heap size = %ld Bytes\n", Heapsize);
#endif

  return 0;
}
