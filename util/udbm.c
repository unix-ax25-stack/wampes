/* User Data Base Manager */

static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Attic/udbm.c,v 1.11 1991/10/25 14:21:28 deyke Exp $";

#define DEBUG           0

#define _HPUX_SOURCE    1

#include <sys/types.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef __TURBOC__

#include <alloc.h>
#include <dos.h>
#include <io.h>

struct utsname {
  char  nodename[16];
};

struct passwd {
  char  *pw_name;
  char  *pw_gecos;
};

#else

#include <pwd.h>
#include <sys/utsname.h>
#include <unistd.h>

#endif

#if (defined(__STDC__) || defined(__TURBOC__))
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#define const
#endif

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

#define NUM_USERS 2503

#if DEBUG
static char usersfile[] = "users";
static char userstemp[] = "users.tmp";
static char indexfile[] = "index";
static char passfile[]  = "passwd";
static char passtemp[]  = "ptmp";
static char aliasfile[] = "aliases";
static char aliastemp[] = "aliases.tmp";
#else
static char usersfile[] = "/usr/local/lib/users";
static char userstemp[] = "/usr/local/lib/users.tmp";
static char indexfile[] = "/users/bbs/index";
static char passfile[]  = "/etc/passwd";
static char passtemp[]  = "/etc/ptmp";
static char aliasfile[] = "/usr/lib/aliases";
static char aliastemp[] = "/usr/lib/aliases.tmp";
#endif

static const char *lockfile;
static const char *null_string = "";
static long  heapsize;
static struct user *users[NUM_USERS];
static struct user null_user;
static struct utsname uts_name;

int uname __ARGS((struct utsname *name));
struct passwd *getpwent __ARGS((void));
void endpwent __ARGS((void));
int putpwent __ARGS((struct passwd *p, FILE *f));
static void terminate __ARGS((const char *s));
static void *allocate __ARGS((size_t size));
static int calc_crc __ARGS((const char *str));
static const char *strsave __ARGS((const char *s));
static char *strlwc __ARGS((char *s));
static char *rmspaces __ARGS((char *s));
static char *strtrim __ARGS((char *s));
static int is_call __ARGS((const char *s));
static int is_qth __ARGS((const char *s));
static int is_phone __ARGS((const char *s));
static int is_mail __ARGS((const char *s));
static int join __ARGS((const char **s1, const char **s2));
static struct user *getup __ARGS((const char *call, int create));
static FILE *fopenexcl __ARGS((const char *path));
static void output_line __ARGS((const struct user *up, FILE *fp));
static int fixusers __ARGS((void));
static void fixpasswd __ARGS((void));
static void fixaliases __ARGS((void));
int main __ARGS((void));

/*---------------------------------------------------------------------------*/

#ifdef __TURBOC__

unsigned  _stklen = 10240;

int  uname(struct utsname *name)
{
  strcpy(name->nodename, "db0sao");
  return 0;
}

struct passwd *getpwent(void)
{
  return NULL;
}

void endpwent(void)
{
}

int  putpwent(struct passwd *p, FILE *f)
{
  return 0;
}

#endif

/*---------------------------------------------------------------------------*/

static void terminate(s)
const char *s;
{
  perror(s);
  if (lockfile) unlink(lockfile);
  exit(1);
}

/*---------------------------------------------------------------------------*/

#define uchar(c)  ((unsigned char) (c))

/*---------------------------------------------------------------------------*/

static void *allocate(size)
size_t size;
{

  static char  *freespace;
  static size_t allocsize = 64*1024-2;
  static size_t freesize;
  void * p;

  if (size & 1) size++;
  if (size > freesize) {
    for (; ; ) {
#ifdef __TURBOC__
      fprintf(stderr, "size = %u coreleft = %lu allocsize = %u\n",
	      size, coreleft(), allocsize);
#endif
      if (size > allocsize) {
	errno = ENOMEM;
	terminate("allocate()");
      }
      if ((freespace = malloc(allocsize)) != NULL) {
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

/* Calculate crc16 for a null terminated string (used for hashing) */

static int  calc_crc(str)
const char *str;
{

  static const int crc_table[] = {
    0x0000, 0xc0c1, 0xc181, 0x0140, 0xc301, 0x03c0, 0x0280, 0xc241,
    0xc601, 0x06c0, 0x0780, 0xc741, 0x0500, 0xc5c1, 0xc481, 0x0440,
    0xcc01, 0x0cc0, 0x0d80, 0xcd41, 0x0f00, 0xcfc1, 0xce81, 0x0e40,
    0x0a00, 0xcac1, 0xcb81, 0x0b40, 0xc901, 0x09c0, 0x0880, 0xc841,
    0xd801, 0x18c0, 0x1980, 0xd941, 0x1b00, 0xdbc1, 0xda81, 0x1a40,
    0x1e00, 0xdec1, 0xdf81, 0x1f40, 0xdd01, 0x1dc0, 0x1c80, 0xdc41,
    0x1400, 0xd4c1, 0xd581, 0x1540, 0xd701, 0x17c0, 0x1680, 0xd641,
    0xd201, 0x12c0, 0x1380, 0xd341, 0x1100, 0xd1c1, 0xd081, 0x1040,
    0xf001, 0x30c0, 0x3180, 0xf141, 0x3300, 0xf3c1, 0xf281, 0x3240,
    0x3600, 0xf6c1, 0xf781, 0x3740, 0xf501, 0x35c0, 0x3480, 0xf441,
    0x3c00, 0xfcc1, 0xfd81, 0x3d40, 0xff01, 0x3fc0, 0x3e80, 0xfe41,
    0xfa01, 0x3ac0, 0x3b80, 0xfb41, 0x3900, 0xf9c1, 0xf881, 0x3840,
    0x2800, 0xe8c1, 0xe981, 0x2940, 0xeb01, 0x2bc0, 0x2a80, 0xea41,
    0xee01, 0x2ec0, 0x2f80, 0xef41, 0x2d00, 0xedc1, 0xec81, 0x2c40,
    0xe401, 0x24c0, 0x2580, 0xe541, 0x2700, 0xe7c1, 0xe681, 0x2640,
    0x2200, 0xe2c1, 0xe381, 0x2340, 0xe101, 0x21c0, 0x2080, 0xe041,
    0xa001, 0x60c0, 0x6180, 0xa141, 0x6300, 0xa3c1, 0xa281, 0x6240,
    0x6600, 0xa6c1, 0xa781, 0x6740, 0xa501, 0x65c0, 0x6480, 0xa441,
    0x6c00, 0xacc1, 0xad81, 0x6d40, 0xaf01, 0x6fc0, 0x6e80, 0xae41,
    0xaa01, 0x6ac0, 0x6b80, 0xab41, 0x6900, 0xa9c1, 0xa881, 0x6840,
    0x7800, 0xb8c1, 0xb981, 0x7940, 0xbb01, 0x7bc0, 0x7a80, 0xba41,
    0xbe01, 0x7ec0, 0x7f80, 0xbf41, 0x7d00, 0xbdc1, 0xbc81, 0x7c40,
    0xb401, 0x74c0, 0x7580, 0xb541, 0x7700, 0xb7c1, 0xb681, 0x7640,
    0x7200, 0xb2c1, 0xb381, 0x7340, 0xb101, 0x71c0, 0x7080, 0xb041,
    0x5000, 0x90c1, 0x9181, 0x5140, 0x9301, 0x53c0, 0x5280, 0x9241,
    0x9601, 0x56c0, 0x5780, 0x9741, 0x5500, 0x95c1, 0x9481, 0x5440,
    0x9c01, 0x5cc0, 0x5d80, 0x9d41, 0x5f00, 0x9fc1, 0x9e81, 0x5e40,
    0x5a00, 0x9ac1, 0x9b81, 0x5b40, 0x9901, 0x59c0, 0x5880, 0x9841,
    0x8801, 0x48c0, 0x4980, 0x8941, 0x4b00, 0x8bc1, 0x8a81, 0x4a40,
    0x4e00, 0x8ec1, 0x8f81, 0x4f40, 0x8d01, 0x4dc0, 0x4c80, 0x8c41,
    0x4400, 0x84c1, 0x8581, 0x4540, 0x8701, 0x47c0, 0x4680, 0x8641,
    0x8201, 0x42c0, 0x4380, 0x8341, 0x4100, 0x81c1, 0x8081, 0x4040
  };

  int  crc;

  crc = 0;
  while (*str)
    crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *str++) & 0xff];
  return crc;
}

/*---------------------------------------------------------------------------*/

static const char *strsave(s)
const char *s;
{

#define NUM_STRINGS 4999

  struct strings {
    struct strings *next;
    char  s[1];
  };

  int  hash;
  static struct strings *strings[NUM_STRINGS];
  struct strings *p;

  if (!*s) return null_string;
  for (p = strings[hash = ((calc_crc(s) & 0x7fff) % NUM_STRINGS)];
       p && strcmp(s, p->s);
       p = p->next)
    ;
  if (!p) {
    p = allocate(sizeof(struct strings *) + strlen(s) + 1);
    strcpy(p->s, s);
    p->next = strings[hash];
    strings[hash] = p;
  }
  return p->s;
}

/*---------------------------------------------------------------------------*/

static char  *strlwc(s)
char  *s;
{
  char  *p;

  for (p = s; (*p = tolower(uchar(*p))) != 0; p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *rmspaces(s)
char  *s;
{
  char  *f, *t;

  for (f = t = s; *f; f++)
    if (*f != ' ') *t++ = *f;
  *t = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strtrim(s)
char  *s;
{
  char  *p;

  for (p = s; *p; p++) ;
  while (--p >= s && *p == ' ') ;
  p[1] = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static int  is_call(s)
const char *s;
{
  int  d, l;

  l = strlen(s);
  if (l < 4 || l > 6) return 0;
  if (!isalpha(uchar(s[l-1]))) return 0;
  for (d = 0; *s; s++) {
    if (!isalnum(uchar(*s))) return 0;
    if (isdigit(uchar(*s))) d++;
  }
  return (d >= 1 && d <= 2);
}

/*---------------------------------------------------------------------------*/

static int  is_qth(s)
const char *s;
{
  switch (strlen(s)) {
  case 5:
    if ((s[0] >= 'A' && s[0] <= 'Z' || s[0] >= 'a' && s[0] <= 'z') &&
	(s[1] >= 'A' && s[1] <= 'Z' || s[1] >= 'a' && s[1] <= 'z') &&
	(s[2] >= '0' && s[2] <= '8'                              ) &&
	(s[3] >= '0' && s[3] <= '9'                              ) &&
	(s[4] >= 'A' && s[4] <= 'J' || s[4] >= 'a' && s[4] <= 'j') &&
	 s[4] != 'I' && s[4] != 'i')
      return 1;
    break;
  case 6:
    if ((s[0] >= 'A' && s[0] <= 'R' || s[0] >= 'a' && s[0] <= 'r') &&
	(s[1] >= 'A' && s[1] <= 'R' || s[1] >= 'a' && s[1] <= 'r') &&
	(s[2] >= '0' && s[2] <= '9'                              ) &&
	(s[3] >= '0' && s[3] <= '9'                              ) &&
	(s[4] >= 'A' && s[4] <= 'X' || s[4] >= 'a' && s[4] <= 'x') &&
	(s[5] >= 'A' && s[5] <= 'X' || s[5] >= 'a' && s[5] <= 'x'))
      return 1;
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  is_phone(s)
const char *s;
{
  int  slash;

  for (slash = 0; *s; s++)
    if (*s == '/')
      slash++;
    else if (!isdigit(uchar(*s)))
      return 0;
  return (slash == 1);
}

/*---------------------------------------------------------------------------*/

static int  is_mail(s)
const char *s;
{
  return (int) (strchr(s, '@') != NULL);
}

/*---------------------------------------------------------------------------*/

static int  join(s1, s2)
const char **s1, **s2;
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

static struct user *getup(call, create)
const char *call;
int  create;
{

  int  hash;
  struct user *up;

  for (up = users[hash = ((calc_crc(call) & 0x7fff) % NUM_USERS)];
       up && strcmp(call, up->call);
       up = up->next)
    ;
  if (create && !up) {
    up = allocate(sizeof(*up));
    *up = null_user;
    up->call = strsave(call);
    up->next = users[hash];
    users[hash] = up;
  }
  return up;
}

/*---------------------------------------------------------------------------*/

static FILE *fopenexcl(path)
const char *path;
{

  FILE * fp;
  int  fd, try;

  for (try = 1; ; try++) {
    if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644)) >= 0) break;
    if (try >= 10) terminate(path);
    sleep(try);
  }
  lockfile = path;
  fp = fdopen(fd, "w");
  if (!fp) terminate(path);
  return fp;
}

/*---------------------------------------------------------------------------*/

static void output_line(up, fp)
const struct user *up;
FILE *fp;
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
  *t = '\0';
  append(up->call);
  append(up->name);
  append(up->street);
  append(up->city);
  append(up->qth);
  append(up->phone);
  append(up->mail);
  *t = '\0';
  fputs(line, fp);
  putc('\n', fp);
}

/*---------------------------------------------------------------------------*/

static int  fixusers()
{

#define NF 20

#define LEN_BID     12
#define LEN_SUBJECT 80
#define LEN_TO      8
#define LEN_AT      8
#define LEN_FROM    8

  struct index {
    long  size;
    long  date;
    int  mesg;
    char  bid[LEN_BID+1];
    char  lifetime_h;
    char  subject[LEN_SUBJECT+1];
    char  lifetime_l;
    char  to[LEN_TO+1];
    char  at[LEN_AT+1];
    char  from[LEN_FROM+1];
    char  deleted;
  };

  FILE *fpi, *fpo;
  char  *f, *t;
  char  *field[NF];
  char  line[1024], orig_line[1024], mybbs[1024];
  int  errors = 0;
  int  i, nf, timestamp;
  struct index index;
  struct user *up;
  struct user user;

  if (!(fpi = fopen(usersfile, "r"))) terminate(usersfile);
  fpo = fopenexcl(userstemp);
  while (fgets(line, sizeof(line), fpi)) {
    for (f = line; *f; f++)
      if (isspace(uchar(*f))) *f = ' ';
    for (t = f = line; *f; f++)
      if (*f != ' ' || f[1] != ' ') *t++ = *f;
    if (t > line && t[-1] == ' ') t--;
    *t = '\0';
    strcpy(orig_line, line);
    f = line;
    memset(field, 0 , sizeof(field));
    nf = 0;
    user = null_user;
    for (; ; ) {
      while (*f && (*f == ' ' || *f == ',')) f++;
      if (!*f) break;
      field[nf++] = f;
      f = strchr(f, ',');
      if (f)
	*f++ = '\0';
      else
	f = "";
      strtrim(field[nf-1]);
    }
    if (!nf) continue;
    for (i = 0; i < NF; i++)
      if (field[i]) {
	if (!*user.call && is_call(field[i])) {
	  user.call = strsave(strlwc(field[i]));
	  field[i] = NULL;
	  nf--;
	} else if (!*user.qth && is_qth(field[i])) {
	  user.qth = strsave(strlwc(field[i]));
	  field[i] = NULL;
	  nf--;
	} else if (!*user.phone && is_phone(field[i])) {
	  user.phone = strsave(field[i]);
	  field[i] = NULL;
	  nf--;
	} else if (!*user.mail && is_mail(field[i])) {
	  user.mail = strsave(strlwc(rmspaces(field[i])));
	  field[i] = NULL;
	  nf--;
	}
      }
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.name = strsave(field[i]);
	  field[i] = NULL;
	  nf--;
	  break;
	}
    if (nf >= 2)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.street = strsave(field[i]);
	  field[i] = NULL;
	  nf--;
	  break;
	}
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  user.city = strsave(field[i]);
	  field[i] = NULL;
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
      fputs(orig_line, fpo);
      putc('\n', fpo);
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

  if ((fpi = fopen(indexfile, "r")) != NULL) {
    while (fread((char *) &index, sizeof(index), 1, fpi))
      if (index.to[0] == 'M' && index.to[1] == '\0'              &&
	  !strcmp(index.at, "THEBOX")                            &&
	  is_call(index.from)                                    &&
	  sscanf(index.subject, "%s %d", mybbs, &timestamp) == 2 &&
	  is_call(mybbs)) {
	up = getup(strlwc(index.from), 1);
	*line = '@';
	strcpy(line+1, strlwc(mybbs));
	up->mail = strsave(line);
      }
    fclose(fpi);
  }

  if (is_call(uts_name.nodename)) {
    up = getup(uts_name.nodename, 1);
    *line = '@';
    strcpy(line+1, up->call);
    up->mail = strsave(line);
  }

  for (i = 0; i < NUM_USERS; i++)
    for (up = users[i]; up; up = up->next) output_line(up, fpo);
  fclose(fpo);

#ifdef __TURBOC__
  unlink(usersfile);
#endif
  if (rename(userstemp, usersfile)) terminate(usersfile);
  lockfile = NULL;
  return errors;
}

/*---------------------------------------------------------------------------*/

static void fixpasswd()
{

  FILE * fp;
  struct passwd *pp;
  struct user *up;

  fp = fopenexcl(passtemp);
  while ((pp = getpwent()) != NULL) {
    if (is_call(pp->pw_name) && (up = getup(pp->pw_name, 0)) != NULL)
      pp->pw_gecos = (char *) up->name;
    putpwent(pp, fp);
  }
  endpwent();
  fclose(fp);
#ifdef __TURBOC__
  unlink(passfile);
#endif
  if (rename(passtemp, passfile)) terminate(passfile);
  lockfile = NULL;
}

/*---------------------------------------------------------------------------*/

static void fixaliases()
{

  FILE * fpi, *fpo;
  char  *p;
  char  line[1024];
  int  i;
  struct user *up;

  if (!(fpi = fopen(aliasfile, "r"))) terminate(aliasfile);
  fpo = fopenexcl(aliastemp);
  while (fgets(line, sizeof(line), fpi)) {
    if (!strncmp(line, "# Generated", 11)) break;
    fputs(line, fpo);
    if (isspace(uchar(*line))) continue;
    if ((p = strchr(line, '#')) != NULL) *p = '\0';
    if (!(p = strchr(line, ':'))) continue;
    while (--p >= line && isspace(uchar(*p))) ;
    p[1] = '\0';
    if ((up = getup(line, 0)) != NULL) up->alias = 1;
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
#ifdef __TURBOC__
  unlink(aliasfile);
#endif
  if (rename(aliastemp, aliasfile)) terminate(aliasfile);
  lockfile = NULL;
}

/*---------------------------------------------------------------------------*/

int  main()
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
  uname(&uts_name);
  if (!fixusers()) {
    fixpasswd();
    fixaliases();
#if (!DEBUG && !defined(__TURBOC__))
    system("exec /usr/bin/newaliases >/dev/null 2>&1");
#endif
  }

#if DEBUG
  fprintf(stderr, "Total heap size = %ld Bytes\n", heapsize);
#endif

  return 0;
}

