#include <ctype.h>
#include <fcntl.h>
#include <memory.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>

struct user {
  char  call[8];
  char  name[80];
  char  street[80];
  char  city[80];
  char  qth[8];
  char  phone[80];
  char  mail[80];
  struct user *next;
};

#define NUM_USERS 1001

static struct user *users[NUM_USERS];

/*---------------------------------------------------------------------------*/

#define uchar(c)  ((unsigned char) (c))

/*---------------------------------------------------------------------------*/

char  *malloc(size)
register unsigned int  size;
{

#define ALLOCSIZE (64*1024)

  char  *sbrk();
  register char  *p;
  static char  *freespace;
  static int  freesize;

  if (size & 1) size++;
  if (size > freesize) {
    if (size > ALLOCSIZE) return (char *) 0;
    p = sbrk(ALLOCSIZE);
    if ((int) p == -1) return (char *) 0;
    freespace = p;
    freesize = ALLOCSIZE;
  }
  p = freespace;
  freespace += size;
  freesize -= size;
  return p;
}

/*---------------------------------------------------------------------------*/

void free(p)
char  *p;
{
}

/*---------------------------------------------------------------------------*/

char  *calloc(nelem, elsize)
unsigned int  nelem, elsize;
{
  register unsigned int  size;

  size = nelem * elsize;
  return memset(malloc(size), 0, (int) size);
}

/*---------------------------------------------------------------------------*/

/* Calculate crc16 for a null terminated string (used for hashing) */

static unsigned short  calc_crc(str)
register char  *str;
{

  static unsigned short  crc_table[] = {
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

  register unsigned short  crc = 0;

  while (*str)
    crc = (crc >> 8) ^ crc_table[uchar(crc ^ *str++)];
  return crc;
}

/*---------------------------------------------------------------------------*/

static char  *strlwc(s)
register char  *s;
{
  register char *p;

  for (p = s; *p = tolower(uchar(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *rmspaces(s)
register char  *s;
{
  register char  *f, *t;

  for (f = t = s; *f; f++)
    if (*f != ' ') *t++ = *f;
  *t = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strtrim(s)
register char  *s;
{
  register char  *p;

  for (p = s; *p; p++) ;
  while (--p >= s && *p == ' ') ;
  p[1] = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strpos(str, pat)
char  *str, *pat;
{
  register char  *s, *p;

  for (; ; str++)
    for (s = str, p = pat; ; ) {
      if (!*p) return str;
      if (!*s) return (char *) 0;
      if (*s++ != *p++) break;
    }
}

/*---------------------------------------------------------------------------*/

static int  is_call(s)
register char  *s;
{
  register int  d, l;

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
register char  *s;
{
  char  buf[8];

  switch (strlen(s)) {
  case 5:
    strlwc(strcpy(buf, s));
    if (buf[0] >= 'a' && buf[0] <= 'z' &&
	buf[1] >= 'a' && buf[1] <= 'z' &&
	buf[2] >= '0' && buf[2] <= '8' &&
	buf[3] >= '0' && buf[3] <= '9' &&
	buf[4] >= 'a' && buf[4] <= 'j' &&
	buf[4] != 'i') return 1;
    break;
  case 6:
    strlwc(strcpy(buf, s));
    if (buf[0] >= 'a' && buf[0] <= 'r' &&
	buf[1] >= 'a' && buf[1] <= 'r' &&
	buf[2] >= '0' && buf[2] <= '9' &&
	buf[3] >= '0' && buf[3] <= '9' &&
	buf[4] >= 'a' && buf[4] <= 'x' &&
	buf[5] >= 'a' && buf[5] <= 'x') return 1;
    break;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  is_phone(s)
register char  *s;
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
register char  *s;
{
  return (int) (strchr(s, '@') != (char *) 0);
}

/*---------------------------------------------------------------------------*/

static void clear_user(up)
register struct user *up;
{
  *up->call = '\0';
  *up->name = '\0';
  *up->street = '\0';
  *up->city = '\0';
  *up->qth = '\0';
  *up->phone = '\0';
  *up->mail = '\0';
  up->next = (struct user *) 0;
}

/*---------------------------------------------------------------------------*/

static int  join(s1, s2)
register char  *s1, *s2;
{
  if (strpos(s2, s1)) {
    strcpy(s1, s2);
    return 0;
  }
  if (strpos(s1, s2)) {
    strcpy(s2, s1);
    return 0;
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

static struct user *getup(call, create)
char  *call;
int  create;
{

  int  hash;
  register struct user *up;

  for (up = users[hash = calc_crc(call) % NUM_USERS]; up && strcmp(call, up->call); up = up->next) ;
  if (create && !up) {
    up = (struct user *) malloc(sizeof(struct user ));
    clear_user(up);
    strcpy(up->call, call);
    up->next = users[hash];
    users[hash] = up;
  }
  return up;
}

/*---------------------------------------------------------------------------*/

static FILE *fopenexcl(path)
char  *path;
{

  int  fd;
  unsigned long  try, sleep();

  for (try = 1; ; try++) {
    if ((fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0444)) >= 0) break;
    if (try == 10) return (FILE * ) 0;
    sleep(try);
  }
  return fdopen(fd, "w");
}

/*---------------------------------------------------------------------------*/

static void output_line(up, fp)
register struct user *up;
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

  char  line[1024];
  register char  *f, *t;

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

static void fixusers()
{

#define NF 20

  static char  tempfile[] = "/usr/local/lib/users.tmp";
  static char  usersfile[] = "/usr/local/lib/users";

  FILE * fpi, *fpo;
  char  *field[NF];
  char  line[1024], orig_line[1024];
  int  i, nf;
  register char  *f, *t;
  register struct user *up;
  struct user user;

  if (!(fpi = fopen(usersfile, "r"))) return;
  if (!(fpo = fopenexcl(tempfile))) return;
  while (fgets(line, sizeof(line), fpi)) {
    for (f = line; *f; f++)
      if (isspace(uchar(*f))) *f = ' ';
    for (t = f = line; *f; f++)
      if (*f != ' ' || f[1] != ' ') *t++ = *f;
    if (t > line && t[-1] == ' ') t--;
    *t = '\0';
    strcpy(orig_line, line);
    memset((char *) f, 0, sizeof(f));
    clear_user(&user);
    nf = 0;
    f = line;
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
	  strlwc(strcpy(user.call, field[i]));
	  field[i] = NULL;
	  nf--;
	} else if (!*user.qth && is_qth(field[i])) {
	  strlwc(strcpy(user.qth, field[i]));
	  field[i] = NULL;
	  nf--;
	} else if (!*user.phone && is_phone(field[i])) {
	  strcpy(user.phone, field[i]);
	  field[i] = NULL;
	  nf--;
	} else if (!*user.mail && is_mail(field[i])) {
	  strlwc(rmspaces(strcpy(user.mail, field[i])));
	  field[i] = NULL;
	  nf--;
	}
      }
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  strcpy(user.name, field[i]);
	  field[i] = NULL;
	  nf--;
	  break;
	}
    if (nf >= 2)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  strcpy(user.street, field[i]);
	  field[i] = NULL;
	  nf--;
	  break;
	}
    if (nf)
      for (i = 0; i < NF; i++)
	if (field[i]) {
	  strcpy(user.city, field[i]);
	  field[i] = NULL;
	  nf--;
	  break;
	}
    if (nf) {
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
    if (join(up->name, user.name)     |
	join(up->street, user.street) |
	join(up->city, user.city)     |
	join(up->qth, user.qth)       |
	join(up->phone, user.phone)   |
	join(up->mail, user.mail)) {
      fprintf(stderr, "***** Join failed *****\n%s\n\n", orig_line);
      output_line(&user, fpo);
    }
  }
  fclose(fpi);
  for (i = 0; i < NUM_USERS; i++)
    for (up = users[i]; up; up = up->next) output_line(up, fpo);
  fclose(fpo);
  rename(tempfile, usersfile);
}

/*---------------------------------------------------------------------------*/

static void fixpasswd()
{

  static char  passfile[] = "/etc/passwd";
  static char  tempfile[] = "/etc/ptmp";

  FILE * fp;
  register struct passwd *pp;
  register struct user *up;

  if (!(fp = fopenexcl(tempfile))) return;
  while (pp = getpwent()) {
    if (up = getup(pp->pw_name, 0)) pp->pw_gecos = up->name;
    putpwent(pp, fp);
  }
  endpwent();
  fclose(fp);
  rename(tempfile, passfile);
}

/*---------------------------------------------------------------------------*/

static void fixaliases()
{

  static char  aliasfile[] = "/usr/lib/aliases";
  static char  tempfile[] = "/usr/lib/aliases.tmp";

  FILE * fpi, *fpo;
  char  line[1024];
  int  i;
  register struct user *up;

  if (!(fpi = fopen(aliasfile, "r"))) return;
  if (!(fpo = fopenexcl(tempfile))) return;
  while (fgets(line, sizeof(line), fpi)) {
    if (!strncmp(line, "# Generated", 11)) break;
    fputs(line, fpo);
  }
  fclose(fpi);
  fputs("# Generated aliases\n", fpo);
  for (i = 0; i < NUM_USERS; i++)
    for (up = users[i]; up; up = up->next)
      if (*up->mail)
	if (*up->mail == '@')
	  fprintf(fpo, "%s\t\t: %s%s\n", up->call, up->call, up->mail);
	else
	  fprintf(fpo, "%s\t\t: %s\n", up->call, up->mail);
  fclose(fpo);
  rename(tempfile, aliasfile);
}

/*---------------------------------------------------------------------------*/

main()
{
  umask(022);
  fixusers();
  fixpasswd();
  fixaliases();
  return 0;
}

