/* Bulletin Board System */

static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 2.12 1989/09/17 23:31:28 dk5sg Exp $";

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <memory.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

extern char  *calloc();
extern char  *getcwd();
extern char  *getenv();
extern char  *malloc();
extern char  *optarg;
extern char  *sys_errlist[];
extern int  errno;
extern int  optind;
extern long  lseek();
extern long  sigsetmask();
extern long  time();
extern struct sockaddr *build_sockaddr();
extern unsigned long  alarm();
extern unsigned long  sleep();
extern unsigned short  getuid();
extern void _exit();
extern void exit();
extern void free();

#define BIDFILE     "seqbid"
#define DEBUGDIR    "/tmp/bbs"
#define HELPFILE    "help"
#define INDEXFILE   "index"
#define INFOFILE    "/usr/local/lib/station.data"
#define MYDESC      "[Gaertringen JN48KP DK5SG-BBS %s OP:DK5SG]"
#define RCFILE      ".bbsrc"
#define SEQFILE     ".bbsseq"
#define WRKDIR      "/users/bbs"

#define SECONDES    (1L)
#define MINUTES     (60L*SECONDES)
#define HOURS       (60L*MINUTES)
#define DAYS        (24L*HOURS)

struct revision {
  char  number[16];
  char  date[16];
  char  time[16];
  char  author[16];
  char  state[16];
} revision;

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
  char  pad1;
  char  subject[LEN_SUBJECT+1];
  char  pad2;
  char  to[LEN_TO+1];
  char  at[LEN_AT+1];
  char  from[LEN_FROM+1];
  char  deleted;
};

struct user {
  char  *name;
  int  uid;
  int  gid;
  char  *dir;
  char  *shell;
  char  *cwd;
  int  seq;
};

struct strlist {
  struct strlist *next;
  char  str[1];
};

struct mail {
  char  from[1024];
  char  to[1024];
  char  subject[1024];
  char  bid[1024];
  char  mid[1024];
  long  date;
  struct strlist *head;
  struct strlist *tail;
};

extern struct cmdtable {
  char  *name;
  void (*fnc)();
  int  argc;
  int  level;
} cmdtable[];

static int  level;
#define USER 0
#define MBOX 1
#define ROOT 2

static char  *myhostname;
static char  mydesc[80];
static char  prompt[1024] = "bbs> ";
static int  debug;
static int  doforward;
static int  errors;
static int  fdlock = -1;
static int  findex;
static struct user user;
static struct utsname utsname;
volatile static int stopped;

/*---------------------------------------------------------------------------*/

static void errorstop(line)
int  line;
{
  printf("Fatal error in line %d: %s\n", line, sys_errlist[errno]);
  puts("Program stopped.");
  exit(1);
}

/*---------------------------------------------------------------------------*/

#define halt() errorstop(__LINE__)

/*---------------------------------------------------------------------------*/

#define uchar(x) ((unsigned char)(x))

/*---------------------------------------------------------------------------*/

static char  *strupc(s)
char  *s;
{
  char *p;

  for (p = s; *p = toupper(uchar(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strlwc(s)
char  *s;
{
  char *p;

  for (p = s; *p = tolower(uchar(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static int  strcasecmp(s1, s2)
char  *s1, *s2;
{
  while (tolower(uchar(*s1)) == tolower(uchar(*s2++)))
    if (!*s1++) return 0;
  return tolower(uchar(*s1)) - tolower(uchar(s2[-1]));
}

/*---------------------------------------------------------------------------*/

static int  strncasecmp(s1, s2, n)
char  *s1, *s2;
int  n;
{
  while (--n >= 0 && tolower(uchar(*s1)) == tolower(uchar(*s2++)))
    if (!*s1++) return 0;
  return n < 0 ? 0 : tolower(uchar(*s1)) - tolower(uchar(s2[-1]));
}

/*---------------------------------------------------------------------------*/

static char  *strtrim(s)
char  *s;
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(uchar(*p))) ;
  p[1] = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static char  *strcasepos(str, pat)
char  *str, *pat;
{
  char  *s, *p;

  for (; ; str++)
    for (s = str, p = pat; ; ) {
      if (!*p) return str;
      if (!*s) return 0;
      if (tolower(uchar(*s++)) != tolower(uchar(*p++))) break;
    }
}

/*---------------------------------------------------------------------------*/

static char  *strsave(s)
char  *s;
{
  return strcpy(malloc((unsigned) (strlen(s) + 1)), s);
}

/*---------------------------------------------------------------------------*/

static char  *getstring(s)
char  *s;
{

  char  *p;
  static int  chr, lastchr;

  fflush(stdout);
  alarm(1 * HOURS);
  for (p = s; ; ) {
    *p = '\0';
    lastchr = chr;
    chr = getchar();
    if (stopped) {
      alarm(0);
      clearerr(stdin);
      *s = '\0';
      return s;
    }
    if (ferror(stdin) || feof(stdin)) {
      alarm(0);
      return 0;
    }
    switch (chr) {
    case EOF:
      alarm(0);
      return (p == s) ? 0 : strtrim(s);
    case '\0':
      break;
    case '\r':
      alarm(0);
      return strtrim(s);
    case '\n':
      if (lastchr != '\r') {
	alarm(0);
	return strtrim(s);
      }
      break;
    default:
      *p++ = chr;
    }
  }
}

/*---------------------------------------------------------------------------*/

static char  *timestr(gmt)
long  gmt;
{
  static char  buf[20];
  struct tm *tm;

  tm = gmtime(&gmt);
  sprintf(buf, "%02d%.3s%02d/%02d%02d",
	       tm->tm_mday,
	       "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	       tm->tm_year % 100,
	       tm->tm_hour,
	       tm->tm_min);
  return buf;
}

/*---------------------------------------------------------------------------*/

static int  callvalid(call)
char  *call;
{
  int  d, l;

  l = strlen(call);
  if (l < 3 || l > 6) return 0;
  if (isdigit(uchar(call[0])) && isdigit(uchar(call[1]))) return 0;
  if (!(isdigit(uchar(call[1])) || isdigit(uchar(call[2])))) return 0;
  if (!isalpha(uchar(call[l-1]))) return 0;
  d = 0;
  for (; *call; call++) {
    if (!isalnum(uchar(*call))) return 0;
    if (isdigit(uchar(*call))) d++;
  }
  if (d < 1 || d > 2) return 0;
  return 1;
}

/*---------------------------------------------------------------------------*/

#define calleq(c1, c2) (!strcasecmp((c1), (c2)))

/*---------------------------------------------------------------------------*/

static char  *dirname(mesg)
int  mesg;
{
  static char  buf[4];

  sprintf(buf, "%03d", mesg / 100);
  return buf;
}

/*---------------------------------------------------------------------------*/

static char  *filename(mesg)
int  mesg;
{
  static char  buf[8];

  sprintf(buf, "%03d/%02d", mesg / 100, mesg % 100);
  return buf;
}

/*---------------------------------------------------------------------------*/

static void put_seq()
{

  FILE * fp;
  char  fname[80];

  if (debug) return;
  sprintf(fname, "%s/%s", user.dir, SEQFILE);
  if (!(fp = fopen(fname, "w")) || fprintf(fp, "%d\n", user.seq) < 0) halt();
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void wait_for_prompt()
{

  char  buf[1024];
  int  l;

  do {
    if (!getstring(buf)) exit(1);
    l = strlen(buf);
  } while (!l || buf[l-1] != '>');
}

/*---------------------------------------------------------------------------*/

static void lock()
{
  if (fdlock < 0) {
    if ((fdlock = open("lock", O_RDWR | O_CREAT, 0644)) < 0) halt();
  }
  if (lockf(fdlock, F_LOCK, 0)) halt();
}

/*---------------------------------------------------------------------------*/

static void unlock()
{
  if (lockf(fdlock, F_ULOCK, 0)) halt();
}

/*---------------------------------------------------------------------------*/

static int  get_index(n, index)
int  n;
struct index *index;
{
  int  i1, i2, im, pos;

  i1 = 0;
  if (lseek(findex, 0l, 0)) halt();
  if (read(findex, (char *) index, sizeof(struct index )) != sizeof(struct index )) return 0;
  if (n == index->mesg) return 1;
  if (n < index->mesg) return 0;

  if ((pos = lseek(findex, (long) (-sizeof(struct index )), 2)) < 0) halt();
  i2 = pos / sizeof(struct index );
  if (read(findex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
  if (n == index->mesg) return 1;
  if (n > index->mesg) return 0;

  while (i1 + 1 < i2) {
    im = (i1 + i2) / 2;
    if (lseek(findex, (long) (im * sizeof(struct index )), 0) < 0) halt();
    if (read(findex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
    if (n == index->mesg) return 1;
    if (n > index->mesg)
      i1 = im;
    else
      i2 = im;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  read_allowed(index)
struct index *index;
{
  if (index->deleted) return 0;
  if (level == ROOT) return 1;
  if (index->to[1]) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static char  *get_user_from_path(path)
char  *path;
{
  char  *cp;

  return (cp = strrchr(path, '!')) ? cp + 1 : path;
}

/*---------------------------------------------------------------------------*/

static char  *get_host_from_path(path)
char  *path;
{

  char  *cp;
  static char  tmp[1024];

  strcpy(tmp, path);
  if (!(cp = strrchr(tmp, '!'))) return myhostname;
  *cp = '\0';
  return (cp = strrchr(tmp, '!')) ? cp + 1 : tmp;
}

/*---------------------------------------------------------------------------*/

static int  msg_uniq(bid, mid)
char  *bid, *mid;
{

  int  n;
  long  validdate;
  struct index *pi, index[1000];

  validdate = time((long *) 0) - 90 * DAYS;
  if (lseek(findex, 0l, 0)) halt();
  for (; ; ) {
    n = read(findex, (char *) (pi = index), 1000 * sizeof(struct index )) / sizeof(struct index );
    if (n < 1) return 1;
    for (; n; n--, pi++)
      if (pi->date >= validdate && !strcmp(pi->bid, bid)) return 0;
  }
}

/*---------------------------------------------------------------------------*/

static void send_to_bbs(mail)
struct mail *mail;
{

  FILE * fp;
  struct index index;
  struct strlist *p;

  lock();
  if (msg_uniq(mail->bid, mail->mid)) {
    if (lseek(findex, (long) (-sizeof(struct index )), 2) < 0)
      index.mesg = 1;
    else {
      if (read(findex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
      index.mesg++;
    }
    index.date = mail->date;
    strcpy(index.bid, mail->bid);
    index.pad1 = 0;
    strncpy(index.subject, mail->subject, LEN_SUBJECT);
    index.subject[LEN_SUBJECT] = '\0';
    index.pad2 = 0;
    strncpy(index.to, get_user_from_path(mail->to), LEN_TO);
    index.to[LEN_TO] = '\0';
    strupc(index.to);
    strncpy(index.at, get_host_from_path(mail->to), LEN_AT);
    index.at[LEN_AT] = '\0';
    strupc(index.at);
    strncpy(index.from, get_user_from_path(mail->from), LEN_FROM);
    index.from[LEN_FROM] = '\0';
    strupc(index.from);
    index.deleted = 0;
    index.size = 0;
    if (!(fp = fopen(filename(index.mesg), "w"))) {
      mkdir(dirname(index.mesg), 0755);
      if (!(fp = fopen(filename(index.mesg), "w"))) halt();
    }
    if (strcmp(index.to, "E") && strcmp(index.to, "M"))
      for (p = mail->head; p; p = p->next) {
	if (fputs(p->str, fp) == EOF) halt();
	if (putc('\n', fp) == EOF) halt();
	index.size += (strlen(p->str) + 1);
      }
    fclose(fp);
    if (lseek(findex, 0l, 2) < 0) halt();
    if (write(findex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
    if (index.mesg == user.seq + 1) {
      user.seq = index.mesg;
      put_seq();
    }
  }
  unlock();
}

/*---------------------------------------------------------------------------*/

static void send_to_mail(mail)
struct mail *mail;
{

  FILE * fp;
  char  command[1024];
  int  i;
  struct strlist *p;

  switch (fork()) {
  case -1:
    halt();
  case 0:
    setuid(0);
    setgid(1);
    for (i = 0; i < _NFILE; i++) close(i);
    setpgrp();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    switch (fork()) {
    case -1:
      _exit(1);
    case 0:
      sprintf(command, "/usr/lib/sendmail -oi -oem -f %s %s", mail->from, mail->to);
      if (!(fp = popen(command, "w"))) _exit(1);
      fprintf(fp, "From: %s\n", mail->from);
      fprintf(fp, "To: %s\n", mail->to);
      if (*mail->subject) fprintf(fp, "Subject: %s\n", mail->subject);
      fprintf(fp, "Bulletin-ID: <%s>\n", mail->bid);
      fprintf(fp, "Message-ID: <%s>\n", mail->mid);
      putc('\n', fp);
      for (p = mail->head; p; p = p->next) {
	fputs(p->str, fp);
	putc('\n', fp);
      }
      pclose(fp);
      _exit(0);
    default:
      _exit(0);
    }
  default:
    wait((int *) 0);
  }
}

/*---------------------------------------------------------------------------*/

static void send_to_news(mail)
struct mail *mail;
{

  FILE * fp;
  char  *fromhost;
  int  i;
  long  mst;
  struct strlist *p;
  struct tm *tm;

  switch (fork()) {
  case -1:
    halt();
  case 0:
    setuid(0);
    setgid(1);
    for (i = 0; i < _NFILE; i++) close(i);
    setpgrp();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    switch (fork()) {
    case -1:
      _exit(1);
    case 0:
      if (!(fp = popen("/usr/bin/rnews", "w"))) _exit(1);
      fprintf(fp, "Relay-Version: DK5SG-BBS version %s %s; site %s.ampr.org\n",
		  revision.number, revision.date, myhostname);
      fromhost = get_host_from_path(mail->from);
      fprintf(fp, "From: %s@%s%s\n", get_user_from_path(mail->from), fromhost, strchr(fromhost, '.') ? "" : ".ampr.org");
      mst = mail->date - 7 * HOURS;
      tm = gmtime(&mst);
      fprintf(fp, "Date: %.3s, %d %.3s %02d %02d:%02d:%02d MST\n",
	      "SunMonTueWedThuFriSat" + 3 * tm->tm_wday,
	      tm->tm_mday,
	      "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	      tm->tm_year % 100,
	      tm->tm_hour,
	      tm->tm_min,
	      tm->tm_sec);
      if (*mail->subject) fprintf(fp, "Subject: %s\n", mail->subject);
      fprintf(fp, "Bulletin-ID: <%s>\n", mail->bid);
      fprintf(fp, "Message-ID: <%s>\n", mail->mid);
      fprintf(fp, "Path: %s\n", mail->from);
      if (debug)
	fprintf(fp, "Newsgroups: %s.test\n", myhostname);
      else
	fprintf(fp, "Newsgroups: dnet.ham\n");
      fprintf(fp, "Posting-Version: DK5SG-BBS version %s %s; site %s.ampr.org\n",
		  revision.number, revision.date, myhostname);
      putc('\n', fp);
      for (p = mail->head; p; p = p->next) {
	fputs(p->str, fp);
	putc('\n', fp);
      }
      pclose(fp);
      _exit(0);
    default:
      _exit(0);
    }
  default:
    wait((int *) 0);
  }
}

/*---------------------------------------------------------------------------*/

static void fix_address(addr)
char  *addr;
{

  char  tmp[1024];
  char  *p1, *p2;

  for (p1 = addr; *p1; p1++)
    switch (*p1) {
    case '%':
    case '.':
      *p1 = '@';
      break;
    case ',':
      *p1 = ':';
      break;
    case '^':
      *p1 = '!';
      break;
    }

  while ((p1 = strchr(addr, '@')) && (p2 = strchr(p1, ':'))) {
    *p1 = *p2 = '\0';
    sprintf(tmp, "%s%s!%s", addr, p1 + 1, p2 + 1);
    strcpy(addr, tmp);
  }

  while (p2 = strrchr(addr, '@')) {
    *p2 = '\0';
    p1 = p2;
    while (p1 > addr && *p1 != '!') p1--;
    if (p1 == addr)
      sprintf(tmp, "%s!%s", p2 + 1, addr);
    else {
      *p1 = '\0';
      sprintf(tmp, "%s!%s!%s", addr, p2 + 1, p1 + 1);
    }
    strcpy(addr, tmp);
  }

  if (!strchr(addr, '!')) {
    sprintf(tmp, "%s!%s", myhostname, addr);
    strcpy(addr, tmp);
  }

  strlwc(addr);
}

/*---------------------------------------------------------------------------*/

static void free_mail(mail)
struct mail *mail;
{
  struct strlist *p;

  while (p = mail->head) {
    mail->head = p->next;
    free((char *) p);
  }
  free((char *) mail);
}

/*---------------------------------------------------------------------------*/

static void route_mail(mail)
struct mail *mail;
{

#define MidSuffix  "@bbs.net"

  FILE * fp;
  int  n;
  char  *cp;
  char  *s;
  struct strlist *p;

  /* Set date */

  mail->date = time((long *) 0);

  /* Fix addresses */

  fix_address(mail->from);
  fix_address(mail->to);

  /* Set bid */

  if (!*mail->bid && (cp = strchr(mail->mid, '@')) && !strcmp(cp, MidSuffix)) {
    strcpy(mail->bid, mail->mid);
    mail->bid[strlen(mail->bid)-strlen(MidSuffix)] = '\0';
  }
  if (!*mail->bid) {
    n = 0;
    lock();
    if (fp = fopen(BIDFILE, "r")) {
      fscanf(fp, "%d", &n);
      fclose(fp);
    }
    n++;
    if (!(fp = fopen(BIDFILE, "w")) || fprintf(fp, "%d\n", n) < 0) halt();
    fclose(fp);
    unlock();
    sprintf(mail->bid, "%012d", n);
    strncpy(mail->bid, myhostname, strlen(myhostname));
  }
  mail->bid[LEN_BID] = '\0';
  strupc(mail->bid);

  /* Set mid */

  if (!*mail->mid) {
    strcpy(mail->mid, mail->bid);
    strcat(mail->mid, MidSuffix);
  }

  /* Remove all message delimiters */

  for (p = mail->head; p; p = p->next) {
    s = p->str;
    if (!strcmp(s, ".")) *s = '\0';
    while (cp = strchr(s, '\004'))       while (cp[0] = cp[1]) cp++;
    while (cp = strchr(s, '\032'))       while (cp[0] = cp[1]) cp++;
    while (cp = strcasepos(s, "/ex"))    while (cp[0] = cp[3]) cp++;
    while (cp = strcasepos(s, "***end")) while (cp[0] = cp[6]) cp++;
  }

  /* Call delivery agents */

  if (callvalid(get_user_from_path(mail->to)))
    send_to_mail(mail);
  else if (calleq(get_host_from_path(mail->to), myhostname))
    send_to_bbs(mail);
  else if (callvalid(get_host_from_path(mail->to)))
    send_to_mail(mail);
  else {
    send_to_bbs(mail);
    if (strlen(get_user_from_path(mail->to)) > 1) send_to_news(mail);
  }

  /* Free mail */

  free_mail(mail);

}

/*---------------------------------------------------------------------------*/

static void append_line(mail, line)
struct mail *mail;
char  *line;
{
  struct strlist *p;

  p = (struct strlist *) malloc(sizeof(struct strlist ) + strlen(line));
  p->next = 0;
  strcpy(p->str, line);
  if (!mail->head)
    mail->head = p;
  else
    mail->tail->next = p;
  mail->tail = p;
}

/*---------------------------------------------------------------------------*/

static void  get_header_value(name, line, value)
char  *name, *line, *value;
{
  char  *p1, *p2;

  while (*name)
    if (tolower(uchar(*name++)) != tolower(uchar(*line++))) return;
  while (isspace(uchar(*line))) line++;
  while ((p1 = strchr(line, '<')) && (p2 = strrchr(p1, '>'))) {
    *p2 = '\0';
    line = p1 + 1;
  }
  strcpy(value, line);
}

/*---------------------------------------------------------------------------*/

static char  *get_host_from_header(line)
char  *line;
{

  char  *p, *q;
  static char  buf[1024];

  if (*line == 'R' && line[1] == ':' && (p = strchr(strcpy(buf, line), '@'))) {
    p++;
    while (*p == ':' || isspace(uchar(*p))) p++;
    for (q = p; isalnum(uchar(*q)); q++) ;
    *q = '\0';
    return p;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  host_in_header(fname, host)
char  *fname;
char  *host;
{

  FILE  * fp;
  char  buf[1024];
  char  *p;

  if (!(fp = fopen(fname, "r"))) halt();
  while (fgets(buf, sizeof(buf), fp))
    if ((p = get_host_from_header(buf)) && calleq(p, host)) {
      fclose(fp);
      return 1;
    }
  fclose(fp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void delete_command(argc, argv)
int  argc;
char  **argv;
{

  int  i;
  int  mesg;
  struct index index;

  for (i = 1; i < argc; i++)
    if ((mesg = atoi(argv[i])) > 0 && get_index(mesg, &index) && !index.deleted)
      if (level == ROOT                 ||
	  calleq(index.from, user.name) ||
	  calleq(index.to, user.name)) {
	if (unlink(filename(mesg))) halt();
	index.deleted = 1;
	if (lseek(findex, (long) (-sizeof(struct index )), 1) < 0) halt();
	if (write(findex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
	printf("Message %d deleted.\n", mesg);
      } else
	printf("Message %d not deleted:  Permission denied.\n", mesg);
    else
      printf("No such message: '%s'.\n", argv[i]);
}

/*---------------------------------------------------------------------------*/

struct dir_entry {
  struct dir_entry *left, *right;
  int  count;
  char  to[LEN_TO+1];
};

static int  dir_column;

static void dir_print(p)
struct dir_entry *p;
{
  if (p) {
    dir_print(p->left);
    if (!stopped)
      printf((dir_column++ % 5) < 4 ? "%5d %-8s" : "%5d %s\n", p->count, p->to);
    dir_print(p->right);
    free((char *) p);
  }
}

/*---------------------------------------------------------------------------*/

static void dir_command(argc, argv)
int  argc;
char  **argv;
{

  int  n, cmp;
  struct dir_entry *head, *curr, *prev;
  struct index *pi, index[1000];

  head = 0;
  cmp = 0;
  if (lseek(findex, 0l, 0)) halt();
  for (; ; ) {
    n = read(findex, (char *) (pi = index), 1000 * sizeof(struct index )) / sizeof(struct index );
    if (n < 1) break;
    for (; n; n--, pi++)
      if (read_allowed(pi))
	for (prev = 0, curr = head; ; ) {
	  if (!curr) {
	    curr = (struct dir_entry *) malloc(sizeof(struct dir_entry ));
	    curr->left = curr->right = 0;
	    curr->count = 1;
	    strcpy(curr->to, pi->to);
	    if (!prev)
	      head = curr;
	    else if (cmp < 0)
	      prev->left = curr;
	    else
	      prev->right = curr;
	    break;
	  }
	  if (!(cmp = strcmp(pi->to, curr->to))) {
	    curr->count++;
	    break;
	  }
	  prev = curr;
	  curr = (cmp < 0) ? curr->left : curr->right;
	}
  }
  dir_column = 0;
  dir_print(head);
  if (!stopped && (dir_column % 5)) putchar('\n');
}

/*---------------------------------------------------------------------------*/

static void disconnect_command(argc, argv)
int  argc;
char  **argv;
{
  puts("Disconnecting...");
  kill(0, 1);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void f_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp;
  char  buf[1024];
  int  c;
  int  do_not_exit;
  struct index index;
  struct tm *tm;

  do_not_exit = doforward;
  if (!get_index(user.seq, &index))
    if (lseek(findex, 0l, 0)) halt();
  while (read(findex, (char *) &index, sizeof(struct index )) == sizeof(struct index )) {
    if (!index.deleted        &&
	index.mesg > user.seq &&
	!host_in_header(filename(index.mesg), user.name)) {
      do_not_exit = 1;
      printf("S %s%s%s < %s%s%s\n",
	     index.to,
	     *index.at ? " @ " : "",
	     index.at,
	     index.from,
	     *index.bid ? " $" : "",
	     index.bid);
      if (!getstring(buf)) exit(1);
      switch (tolower(uchar(*buf))) {
      case 'o':
	puts(index.subject);
	if (!strcmp(index.to, "E") || !strcmp(index.to, "M"))
	  putchar('\n');
	else {
	  tm = gmtime(&index.date);
	  printf("R:%02d%02d%02d/%02d%02dz @%-6s %s\n",
		 tm->tm_year % 100,
		 tm->tm_mon + 1,
		 tm->tm_mday,
		 tm->tm_hour,
		 tm->tm_min,
		 myhostname,
		 mydesc);
	  if (!(fp = fopen(filename(index.mesg), "r"))) halt();
	  while ((c = getc(fp)) != EOF) putchar(c);
	  fclose(fp);
	}
	puts("\032");
	wait_for_prompt();
	break;
      case 'n':
	wait_for_prompt();
	break;
      default:
	exit(1);
      }
    }
    if (user.seq < index.mesg) {
      user.seq = index.mesg;
      put_seq();
    }
  }
  if (do_not_exit)
    putchar('F');
  else
    exit(0);
}

/*---------------------------------------------------------------------------*/

static void help_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp;
  char  line[1024];
  int  i;
  int  state;
  struct cmdtable *cmdp;

  if (argc < 2) {
    puts("Commands may be abbreviated.  Commands are:");
    for (i = 0, cmdp = cmdtable; cmdp->name; cmdp++)
      if (level >= cmdp->level)
	printf((i++ % 6) < 5 ? "%-13s" : "%s\n", cmdp->name);
    if (i % 6) putchar('\n');
    return;
  }
  if (!(fp = fopen(HELPFILE, "r"))) {
    puts("Sorry, cannot open help file.");
    return;
  }
  if (!strcasecmp(argv[1], "all")) {
    while (!stopped && fgets(line, sizeof(line), fp))
      if (*line != '^') fputs(line, stdout);
  } else {
    state = 0;
    while (!stopped && fgets(line, sizeof(line), fp)) {
      strtrim(line);
      if (state == 0 && *line == '^' && !strcasecmp(line + 1, argv[1]))
	state = 1;
      if (state == 1 && *line != '^')
	state = 2;
      if (state == 2) {
	if (*line == '^') break;
	puts(line);
      }
    }
    if (!stopped && state < 2)
      printf("Sorry, there is no help available for '%s'.\n", argv[1]);
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void info_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp;
  int  c;

  if (!(fp = fopen(INFOFILE, "r"))) {
    puts("Sorry, cannot open info file.");
    return;
  }
  while (!stopped && (c = getc(fp)) != EOF) putchar(c);
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

#define nextarg(name)     \
  if (++i >= argc) {      \
    errors++;             \
    printf(errstr, name); \
    return;               \
  }

static void list_command(argc, argv)
int  argc;
char  **argv;
{

  char  *at = 0;
  char  *bid = 0;
  char  *errstr = "The %s option requires an argument.  Type ? LIST for help.\n";
  char  *from = 0;
  char  *subject = 0;
  char  *to = 0;
  int  count = 99999;
  int  found = 0;
  int  i;
  int  len;
  int  max = 99999;
  int  min = 1;
  int  update_seq = 0;
  struct index index;

  for (i = 1; i < argc; i++) {
    len = strlen(strlwc(argv[i]));
    if (!strcmp("$", argv[i]) || !strncmp("bid", argv[i], len)) {
      nextarg("BID");
      bid = strupc(argv[i]);
    } else if (!strcmp("<", argv[i]) || !strncmp("from", argv[i], len)) {
      nextarg("FROM");
      from = strupc(argv[i]);
    } else if (!strcmp(">", argv[i]) || !strncmp("to", argv[i], len)) {
      nextarg("TO");
      to = strupc(argv[i]);
    } else if (!strcmp("@", argv[i]) || !strncmp("at", argv[i], len)) {
      nextarg("AT");
      at = strupc(argv[i]);
    } else if (!strncmp("count", argv[i], len)) {
      nextarg("COUNT");
      count = atoi(argv[i]);
    } else if (!strncmp("new", argv[i], len)) {
      min = user.seq + 1;
      update_seq = (argc == 2 && level == USER);
    } else if (!strncmp("max", argv[i], len)) {
      nextarg("MAX");
      max = atoi(argv[i]);
    } else if (!strncmp("min", argv[i], len)) {
      nextarg("MIN");
      min = atoi(argv[i]);
    } else if (!strncmp("subject", argv[i], len)) {
      nextarg("SUBJECT");
      subject = argv[i];
    } else {
      to = strupc(argv[i]);
    }
  }
  if (lseek(findex, (long) (-sizeof(struct index )), 2) >= 0) {
    for (; ; ) {
      if (stopped) return;
      if (read(findex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
      if (index.mesg < min) break;
      if (index.mesg <= max                            &&
	  read_allowed(&index)                         &&
	  (!bid      || !strcmp(index.bid, bid))       &&
	  (!from     || !strcmp(index.from, from))     &&
	  (!to       || !strcmp(index.to, to))         &&
	  (!at       || !strcmp(index.at, at))         &&
	  (!subject  || strcasepos(index.subject, subject))) {
	if (!found) {
	  puts(" Msg#  Size To      @ BBS     From     Date    Subject");
	  found = 1;
	}
	printf("%5d %5ld %-8s%c%-8s %-8s %-7.7s %.32s\n",
	       index.mesg,
	       index.size,
	       index.to,
	       *index.at ? '@' : ' ',
	       index.at,
	       index.from,
	       timestr(index.date),
	       index.subject);
	if (update_seq && user.seq < index.mesg) {
	  user.seq = index.mesg;
	  put_seq();
	}
	if (--count <= 0) break;
      }
      if (lseek(findex, -2l * sizeof(struct index ), 1) < 0) break;
    }
  }
  if (!found)
    puts(update_seq ?
	 "No new messages since last LIST NEW command." :
	 "No matching message found.");
}

#undef nextarg

/*---------------------------------------------------------------------------*/

static void mybbs_command(argc, argv)
int  argc;
char  **argv;
{
  struct mail *mail;

  if (!callvalid(strupc(argv[1]))) {
    printf("Invalid call '%s'.\n", argv[1]);
    return;
  }
  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  strcpy(mail->from, user.name);
  strcpy(mail->to, "m@thebox");
  sprintf(mail->subject, "%s %ld", argv[1], time((long *) 0));
  append_line(mail, "");
  printf("Setting MYBBS to %s.\n", argv[1]);
  route_mail(mail);
}

/*---------------------------------------------------------------------------*/

static void prompt_command(argc, argv)
int  argc;
char  **argv;
{
  strcpy(prompt, argv[1]);
}

/*---------------------------------------------------------------------------*/

static void quit_command(argc, argv)
int  argc;
char  **argv;
{
  puts("BBS terminated.");
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void read_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp;
  char  *p;
  char  buf[1024];
  char  path[1024];
  int  i;
  int  inheader;
  int  mesg;
  struct index index;

  for (i = 1; i < argc; i++)
    if ((mesg = atoi(argv[i])) > 0 && get_index(mesg, &index) && read_allowed(&index)) {
      if (!(fp = fopen(filename(mesg), "r"))) halt();
      printf("Msg# %d   To: %s%s%s   From: %s   Date: %s\n",
	     index.mesg,
	     index.to,
	     *index.at ? " @" : "",
	     index.at,
	     index.from,
	     timestr(index.date));
      if (*index.subject) printf("Subject: %s\n", index.subject);
      if (*index.bid) printf("Bulletin ID: %s\n", index.bid);
      *path = '\0';
      inheader = 1;
      while (fgets(buf, sizeof(buf), fp)) {
	if (stopped) {
	  fclose(fp);
	  return;
	}
	if (inheader) {
	  if (p = get_host_from_header(buf)) {
	    strcat(path, *path ? "!" : "Path: ");
	    strcat(path, p);
	    continue;
	  }
	  if (*path) puts(path);
	  inheader = 0;
	}
	fputs(buf, stdout);
      }
      putchar('\n');
      fclose(fp);
    } else
      printf("No such message: '%s'.\n", argv[i]);
}

/*---------------------------------------------------------------------------*/

static void reply_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp;
  char  *host;
  char  *mesgstr;
  char  *p;
  char  line[1024];
  int  all;
  int  i;
  int  mesg;
  struct index index;
  struct mail *mail;

  mesg = all = 0;
  mesgstr = 0;
  for (i = 1; i < argc; i++)
    if (!strcasecmp(argv[i], "all"))
      all = 1;
    else
      mesg = atoi(mesgstr = argv[i]);
  if (!mesgstr) {
    puts("No message number specified.");
    return;
  }
  if (mesg < 1 || !get_index(mesg, &index) || !read_allowed(&index)) {
    printf("No such message: '%s'.\n", mesgstr);
    return;
  }
  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  strcpy(mail->from, user.name);
  if (all) {
    strcpy(mail->to, index.to);
    if (*index.at) {
      strcat(mail->to, "@");
      strcat(mail->to, index.at);
    }
  } else {
    strcpy(mail->to, index.from);
    if (!(fp = fopen(filename(mesg), "r"))) halt();
    for (host = 0; fgets(line, sizeof(line), fp); host = p)
      if (!(p = get_host_from_header(line))) break;
    fclose(fp);
    if (host) {
      strcat(mail->to, "@");
      strcat(mail->to, host);
    }
  }
  strlwc(mail->to);
  for (p = index.subject; ; ) {
    while (isspace(uchar(*p))) p++;
    if (strncasecmp(p, "Re:", 3)) break;
    p += 3;
  }
  sprintf(mail->subject, "Re:  %s", p);
  printf("To: %s\n", mail->to);
  printf("Subject: %s\n", mail->subject);
  puts("Enter message: (terminate with ^Z or /EX or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (stopped) {
      free_mail(mail);
      return;
    }
    if (*line == '\032') break;
    if (!strncasecmp(line, "***end", 6)) break;
    if (!strncasecmp(line, "/ex", 3)) break;
    append_line(mail, line);
    if (strchr(line, '\032')) break;
  }
  printf("Sending message to %s.\n", mail->to);
  route_mail(mail);
}

/*---------------------------------------------------------------------------*/

#define nextarg(name)     \
  if (++i >= argc) {      \
    errors++;             \
    printf(errstr, name); \
    free_mail(mail);      \
    return;               \
  }

static void send_command(argc, argv)
int  argc;
char  **argv;
{

  char  *errstr = "The %s option requires an argument.  Type ? SEND for help.\n";
  char  *p;
  char  at[1024];
  char  line[1024];
  char  path[1024];
  int  check_header = 1;
  int  i;
  int  lifetime = 0;
  struct mail *mail;

  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  *at = *path = '\0';
  for (i = 1; i < argc; i++)
    if (!strcmp("#", argv[i])) {
      nextarg("#");
      lifetime = atoi(argv[i]);
    } else if (!strcmp("$", argv[i])) {
      nextarg("$");
      strcpy(mail->bid, argv[i]);
    } else if (!strcmp("<", argv[i])) {
      nextarg("<");
      strcpy(mail->from, argv[i]);
    } else if (!strcmp(">", argv[i])) {
      nextarg(">");
      strcpy(mail->to, argv[i]);
    } else if (!strcmp("@", argv[i])) {
      nextarg("@");
      strcpy(at, argv[i]);
    } else {
      strcpy(mail->to, argv[i]);
    }
  if (!*mail->to) {
    errors++;
    puts("No recipient specified.");
    free_mail(mail);
    return;
  }
  if (level == USER && !mail->to[1]) {
    errors++;
    puts("Invalid recipient specified.");
    free_mail(mail);
    return;
  }
  if (*at) {
    strcat(mail->to, "@");
    strcat(mail->to, at);
  }
  strlwc(mail->to);
  if (!*mail->from || level < MBOX) strcpy(mail->from, user.name);
  if (*mail->bid) {
    mail->bid[LEN_BID] = '\0';
    strupc(mail->bid);
    if (!msg_uniq(mail->bid, mail->mid)) {
      puts("No");
      free_mail(mail);
      return;
    }
  }
  puts((level == MBOX) ? "OK" : "Enter subject:");
  if (!getstring(mail->subject)) exit(1);
  if (stopped) {
    free_mail(mail);
    return;
  }
  if (level != MBOX) puts("Enter message: (terminate with ^Z or /EX or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (stopped) {
      free_mail(mail);
      return;
    }
    if (*line == '\032') break;
    if (!strncasecmp(line, "***end", 6)) break;
    if (!strncasecmp(line, "/ex", 3)) break;
    append_line(mail, line);
    if (check_header) {
      if (p = get_host_from_header(line)) {
	if (*path) strcat(path, "!");
	strcat(path, p);
      } else if (*path)
	check_header = 0;
    }
    if (strchr(line, '\032')) break;
  }
  if (*path) {
    strcpy(line, mail->from);
    sprintf(mail->from, "%s!%s", path, line);
  }
  if (level != MBOX) printf("Sending message to %s.\n", mail->to);
  route_mail(mail);
}

#undef nextarg

/*---------------------------------------------------------------------------*/

static void shell_command(argc, argv)
int  argc;
char  **argv;
{

  char  command[2048];
  int  i;
  int  oldstopped;
  int  pid;
  int  status;
  long  oldmask;

  switch (pid = fork()) {
  case -1:
    puts("Sorry, cannot fork.");
    break;
  case 0:
    setgid(user.gid);
    setuid(user.uid);
    for (i = 3; i < _NFILE; i++) close(i);
    chdir(user.cwd);
    *command = '\0';
    for (i = 1; i < argc; i++) {
      if (i > 1) strcat(command, " ");
      strcat(command, argv[i]);
    }
    if (*command)
      execl(user.shell, user.shell, "-c", command, (char *) 0);
    else
      execl(user.shell, user.shell, (char *) 0);
    _exit(127);
    break;
  default:
    oldmask = sigsetmask(-1);
    oldstopped = stopped;
    while (waitpid(pid, &status, 0) != pid) ;
    sigsetmask(oldmask);
    stopped = oldstopped;
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void sid_command(argc, argv)
int  argc;
char  **argv;
{
  /*** ignore System IDentifiers (SIDs) for now ***/
}

/*---------------------------------------------------------------------------*/

static void status_command(argc, argv)
int  argc;
char  **argv;
{

  int  active = 0;
  int  crunchok = 0;
  int  deleted = 0;
  int  highest = 0;
  int  n;
  int  new = 0;
  int  readable = 0;
  long  validdate;
  struct index *pi, index[1000];

  validdate = time((long *) 0) - 90 * DAYS;
  printf("DK5SG-BBS  Revision: %s %s\n", revision.number, revision.date);
  if (lseek(findex, 0l, 0)) halt();
  for (; ; ) {
    n = read(findex, (char *) (pi = index), 1000 * sizeof(struct index )) / sizeof(struct index );
    if (n < 1) break;
    for (; n; n--, pi++) {
      highest = pi->mesg;
      if (pi->deleted) {
	deleted++;
	if (!*pi->bid || pi->date < validdate) crunchok++;
      } else {
	active++;
	if (read_allowed(pi)) {
	  readable++;
	  if (pi->mesg > user.seq) new++;
	}
      }
    }
  }
  printf("%5d  Highest message number\n", highest);
  printf("%5d  Active messages\n", active);
  printf("%5d  Readable messages\n", readable);
  if (level == ROOT) {
    printf("%5d  Deleted messages\n", deleted);
    printf("%5d  Messages may be crunched\n", crunchok);
  }
  printf("%5d  Last message listed\n", user.seq);
  printf("%5d  New messages\n", new);
}

/*---------------------------------------------------------------------------*/

static void unknown_command(argc, argv)
int  argc;
char  **argv;
{
  errors++;
  printf("Unknown command '%s'.  Type ? for help.\n", *argv);
}

/*---------------------------------------------------------------------------*/

static void xcrunch_command(argc, argv)
int  argc;
char  **argv;
{

  char  *tempfile = "index.tmp";
  int  f;
  int  wflag;
  long  validdate;
  struct index index;

  validdate = time((long *) 0) - 90 * DAYS;
  if ((f = open(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) halt();
  if (lseek(findex, 0l, 0)) halt();
  wflag = 0;
  while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index )) {
    wflag = 1;
    if (!index.deleted || *index.bid && index.date >= validdate) {
      if (write(f, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
      wflag = 0;
    }
  }
  if (wflag)
    if (write(f, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
  if (close(f)) halt();
  if (close(findex)) halt();
  if (rename(tempfile, INDEXFILE)) halt();
  exit(0);
}

/*---------------------------------------------------------------------------*/

#define Invalid (                                   \
   pi->deleted                                   || \
   *from    &&  strcmp(pi->from, from)           || \
   *to      &&  strcmp(pi->to,   to)             || \
   *at      &&  strcmp(pi->at,   at)             || \
   *subject && !strcasepos(pi->subject, subject)    \
   )

static void xscreen_command(argc, argv)
int  argc;
char  **argv;
{

  FILE * fp = 0;
  char  at[1024];
  char  buf[1024];
  char  from[1024];
  char  subject[1024];
  char  to[1024];
  int  cmd;
  int  indexarrayentries;
  int  lines = 0;
  int  maxlines;
  int  mesg;
  struct index *indexarray;
  struct index *pi;
  struct stat statbuf;
  struct termio curr_termio, prev_termio;
  unsigned int  indexarraysize;

  if (fstat(findex, &statbuf)) halt();
  indexarraysize = statbuf.st_size;
  indexarrayentries = indexarraysize / sizeof(struct index );
  if (!indexarrayentries) return;
  if (!(indexarray = (struct index *) malloc(indexarraysize))) halt();
  if (lseek(findex, 0l, 0)) halt();
  if (read(findex, (char *) indexarray, indexarraysize) != indexarraysize) halt();

  ioctl(0, TCGETA, &prev_termio);
  memcpy(&curr_termio, &prev_termio, sizeof(struct termio ));
  curr_termio.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
  curr_termio.c_lflag = 0;
  curr_termio.c_cc[VMIN] = 1;
  curr_termio.c_cc[VTIME] = 0;
  ioctl(0, TCSETA, &curr_termio);
  if (!(maxlines = atoi(getenv("LINES")))) maxlines = 24;

  *from = *to = *at = *subject = '\0';
  pi = indexarray;
  cmd = '\b';
  for (; ; ) {
    lines = 0;
    fflush(stdout);
    while (!cmd) cmd = getchar();
    switch (cmd) {

    case '\b':
      if (pi > indexarray) pi--;
      while (Invalid && pi > indexarray) pi--;
      while (Invalid && pi < indexarray + (indexarrayentries - 1)) pi++;
      cmd = Invalid ? 'q' : 'r';
      break;

    case 'k':
      if (unlink(filename(pi->mesg))) halt();
      pi->deleted = 1;
      if (lseek(findex, (long) ((pi - indexarray) * sizeof(struct index )), 0) < 0) halt();
      if (write(findex, (char *) pi, sizeof(struct index )) != sizeof(struct index )) halt();

    case '\n':
      if (pi < indexarray + (indexarrayentries - 1)) pi++;
      while (Invalid && pi < indexarray + (indexarrayentries - 1)) pi++;
      while (Invalid && pi > indexarray) pi--;
      cmd = Invalid ? 'q' : 'r';
      break;

    case 'r':
      if (fp) {
	fclose(fp);
	fp = 0;
      }
      if (!(fp = fopen(filename(pi->mesg), "r"))) halt();
      printf("\033&a0y0C\033JMsg# %d   To: %s%s%s   From: %s   Date: %s\n", pi->mesg, pi->to, *pi->at ? " @" : "", pi->at, pi->from, timestr(pi->date));
      lines++;
      if (*pi->subject) {
	printf("Subject: %s\n", pi->subject);
	lines++;
      }
      if (*pi->bid) {
	printf("Bulletin ID: %s\n", pi->bid);
	lines++;
      }
      putchar('\n');
      lines++;

    case ' ':
      if (fp) {
	while (lines < maxlines - 1 && fgets(buf, sizeof(buf), fp)) {
	  fputs(buf, stdout);
	  lines++;
	}
	if (feof(fp)) {
	  fclose(fp);
	  fp = 0;
	}
	cmd = 0;
      } else
	cmd = '\n';
      break;

    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
      ioctl(0, TCSETA, &prev_termio);
      printf("\nMessage number: %c", cmd);
      *buf = cmd;
      gets(buf + 1);
      ioctl(0, TCSETA, &curr_termio);
      mesg = atoi(buf);
      pi = indexarray;
      while ((mesg > pi->mesg || Invalid) && pi < indexarray + (indexarrayentries - 1)) pi++;
      while (Invalid && pi > indexarray) pi--;
      cmd = Invalid ? 'q' : 'r';
      break;

    case '<':
      *from = *to = *at = *subject = '\0';
      ioctl(0, TCSETA, &prev_termio);
      printf("\nFROM field: ");
      gets(from);
      ioctl(0, TCSETA, &curr_termio);
      strupc(from);
      cmd = Invalid ? '\n' : 'r';
      break;

    case '>':
      *from = *to = *at = *subject = '\0';
      ioctl(0, TCSETA, &prev_termio);
      printf("\nTO field: ");
      gets(to);
      ioctl(0, TCSETA, &curr_termio);
      strupc(to);
      cmd = Invalid ? '\n' : 'r';
      break;

    case '@':
      *from = *to = *at = *subject = '\0';
      ioctl(0, TCSETA, &prev_termio);
      printf("\nAT field: ");
      gets(at);
      ioctl(0, TCSETA, &curr_termio);
      strupc(at);
      cmd = Invalid ? '\n' : 'r';
      break;

    case 's':
      *from = *to = *at = *subject = '\0';
      ioctl(0, TCSETA, &prev_termio);
      printf("\nSUBJECT substring: ");
      gets(subject);
      ioctl(0, TCSETA, &curr_termio);
      cmd = Invalid ? '\n' : 'r';
      break;

    case 'v':
      sprintf(buf, "vi %s", filename(pi->mesg));
      ioctl(0, TCSETA, &prev_termio);
      system(buf);
      ioctl(0, TCSETA, &curr_termio);
      cmd = 'r';
      break;

    case 'q':
      ioctl(0, TCSETA, &prev_termio);
      free((char *) indexarray);
      return;

    case '?':
      puts("----------------------------------- Commands ----------------------------------");

      puts("<          specify FROM field");
      puts("<number>   show numbered entry");
      puts(">          specify TO field");
      puts("?          print command summary");
      puts("@          specify AT field");
      puts("BACKSPACE  show previous entry");
      puts("RETURN     show next entry");
      puts("SPACE      show next screenful");
      puts("k          delete current entry");
      puts("q          quit BBS");
      puts("r          redisplay current entry");
      puts("s          specify SUBJECT substring");
      puts("v          edit current entry");

      printf("\nPress any key to continue ");
      getchar();
      cmd = 'r';
      break;
    default:
      putchar('\007');
      cmd = 0;
      break;
    }
  }
}

#undef Invalid

/*---------------------------------------------------------------------------*/

static char  *connect_addr(host)
char  *host;
{

  FILE * fp;
  char  *addr;
  char  *h;
  char  *p;
  static char  line[1024];

  addr = 0;
  if (fp = fopen("config", "r")) {
    while (fgets(line, sizeof(line), fp)) {
      for (p = line; isspace(uchar(*p)); p++) ;
      for (h = p; *p && !isspace(uchar(*p)); p++) ;
      if (*p) *p++ = '\0';
      if (!strcmp(h, host)) {
	addr = strtrim(p);
	break;
      }
    }
    fclose(fp);
  }
  return addr;
}

/*---------------------------------------------------------------------------*/

static void connect_bbs()
{

  char  *address;
  int  addrlen;
  int  fd;
  struct sockaddr *addr;

  if (!(address = connect_addr(user.name))) exit(1);
  if (!(addr = build_sockaddr("unix:/tcp/sockets/netcmd", &addrlen))) exit(1);
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) exit(1);
  if (connect(fd, addr, addrlen)) exit(1);
  if (fd != 0) dup2(fd, 0);
  if (fd != 1) dup2(fd, 1);
  if (fd != 2) dup2(fd, 2);
  if (fd > 2) close(fd);
  fdopen(0, "r+");
  fdopen(1, "r+");
  fdopen(2, "r+");
  printf("connect %s\n", address);
}

/*---------------------------------------------------------------------------*/

static struct cmdtable cmdtable[] = {

  "!",          shell_command,          0,      USER,
  "?",          help_command,           0,      USER,
  "BYE",        quit_command,           0,      USER,
  "DELETE",     delete_command,         2,      USER,
  "DIR",        dir_command,            0,      USER,
  "DISCONNECT", disconnect_command,     0,      USER,
  "ERASE",      delete_command,         2,      USER,
  "EXIT",       quit_command,           0,      USER,
  "F",          f_command,              2,      MBOX,
  "HELP",       help_command,           0,      USER,
  "INFO",       info_command,           0,      USER,
  "KILL",       delete_command,         2,      USER,
  "LIST",       list_command,           2,      USER,
  "MYBBS",      mybbs_command,          2,      USER,
  "PRINT",      read_command,           2,      USER,
  "PROMPT",     prompt_command,         2,      USER,
  "QUIT",       quit_command,           0,      USER,
  "READ",       read_command,           2,      USER,
  "REPLY",      reply_command,          2,      USER,
  "RESPOND",    reply_command,          2,      USER,
  "SB",         send_command,           2,      MBOX,
  "SEND",       send_command,           2,      USER,
  "SHELL",      shell_command,          0,      USER,
  "SP",         send_command,           2,      MBOX,
  "STATUS",     status_command,         0,      USER,
  "TYPE",       read_command,           2,      USER,
  "VERSION",    status_command,         0,      USER,
  "XCRUNCH",    xcrunch_command,        0,      ROOT,
  "XSCREEN",    xscreen_command,        0,      ROOT,
  "[",          sid_command,            0,      MBOX,

  (char *) 0,   unknown_command,        0,      USER
};

/*---------------------------------------------------------------------------*/

static void parse_command_line(line)
char  *line;
{

  char  *argv[256];
  char  *delim = "<>@$#[";
  char  *f;
  char  *t;
  char  buf[2048];
  int  argc;
  int  len;
  int  quote;
  struct cmdtable *cmdp;

  argc = 0;
  memset((char *) argv, 0, sizeof(argv));
  for (f = line, t = buf; ; ) {
    while (isspace(uchar(*f))) f++;
    if (!*f) break;
    argv[argc++] = t;
    if (*f == '"' || *f == '\'') {
      quote = *f++;
      while (*f && *f != quote) *t++ = *f++;
      if (*f) f++;
    } else if (strchr(delim, *f)) {
      *t++ = *f++;
    } else {
      while (*f && !isspace(uchar(*f)) && !strchr(delim, *f)) *t++ = *f++;
    }
    *t++ = '\0';
  }
  if (!argc) return;
  if (!(len = strlen(*argv))) return;
  for (cmdp = cmdtable; ; cmdp++)
    if (!cmdp->name ||
	level >= cmdp->level && !strncasecmp(cmdp->name, *argv, len)) {
      if (argc >= cmdp->argc)
	(*cmdp->fnc)(argc, argv);
      else {
	errors++;
	printf("The %s command requires more arguments.  Type ? %s for help.\n",
	       cmdp->name, cmdp->name);
      }
      break;
    }
  if (level == MBOX && errors >= 3) exit(1);
}

/*---------------------------------------------------------------------------*/

static void bbs()
{

  FILE * fp;
  char  line[1024];

  if (level != MBOX)
    printf("DK5SG-BBS  Revision: %s   Type ? for help.\n", revision.number);
  sprintf(line, "%s/%s", user.dir, RCFILE);
  if (fp = fopen(line, "r")) {
    while (fgets(line, sizeof(line), fp)) parse_command_line(line);
    fclose(fp);
  }
  if (doforward) {
    connect_bbs();
    wait_for_prompt();
  }
  if (level == MBOX) printf("[THEBOX-1.5l-$]\n");
  if (doforward) wait_for_prompt();
  for (; ; ) {
    if (doforward)
      strcpy(line, "f>");
    else {
      printf("%s", (level == MBOX) ? ">\n" : prompt);
      if (!getstring(line)) exit(1);
    }
    parse_command_line(line);
    doforward = 0;
    if (stopped) {
      puts("\n*** Interrupt ***");
      stopped = 0;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void interrupt_handler(sig, code, scp)
int  sig, code;
struct sigcontext *scp;
{
  stopped = 1;
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

static void alarm_handler(sig, code, scp)
int  sig, code;
struct sigcontext *scp;
{
  puts("\n*** Timeout ***");
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void trap_signal(sig, handler)
int  sig;
void (*handler)();
{
  struct sigvec vec;

  sigvector(sig, (struct sigvec *) 0, &vec);
  if (vec.sv_handler != SIG_IGN) {
    vec.sv_mask = vec.sv_flags = 0;
    vec.sv_handler = handler;
    sigvector(sig, &vec, (struct sigvec *) 0);
  }
}

/*---------------------------------------------------------------------------*/

static void rmail()
{

  char  line[1024];
  int  state = 0;
  struct mail *mail;

  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  strcpy(mail->to, "alle@alle");
  if (scanf("%*s%s", mail->from) != 1) halt();
  if (!getstring(line)) halt();
  while (getstring(line))
    switch (state) {
    case 0:
      get_header_value("Subject:", line, mail->subject);
      get_header_value("Bulletin-ID:", line, mail->bid);
      get_header_value("Message-ID:", line, mail->mid);
      if (*line) continue;
      state = 1;
    case 1:
      if (!*line) continue;
      state = 2;
    case 2:
      append_line(mail, line);
    }
  route_mail(mail);
}

/*---------------------------------------------------------------------------*/

static void rnews()
{

  char  line[1024];
  int  n;
  int  state = 0;
  struct mail *mail;

  while (fgets(line, sizeof(line), stdin)) {
    mail = (struct mail *) calloc(1, sizeof(struct mail ));
    strcpy(mail->to, "alle@alle");
    if (strncmp(line, "#! rnews ", 9)) halt();
    n = atoi(line + 9);
    while (n > 0) {
      if (!fgets(line, n < sizeof(line) ? n + 1 : (int) sizeof(line), stdin)) halt();
      n -= strlen(line);
      strtrim(line);
      switch (state) {
      case 0:
	get_header_value("Path:", line, mail->from);
	get_header_value("Subject:", line, mail->subject);
	get_header_value("Bulletin-ID:", line, mail->bid);
	get_header_value("Message-ID:", line, mail->mid);
	if (*line) continue;
	state = 1;
      case 1:
	if (!*line) continue;
	state = 2;
      case 2:
	append_line(mail, line);
      }
    }
    route_mail(mail);
  }
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{

#define BBS   0
#define RMAIL 1
#define RNEWS 2

  FILE * fp;
  char  *dir = WRKDIR;
  char  *sysname;
  char  fname[80];
  int  c;
  int  err_flag = 0;
  int  mode = BBS;
  struct passwd *pw;

  trap_signal(SIGINT,  interrupt_handler);
  trap_signal(SIGQUIT, interrupt_handler);
  trap_signal(SIGTERM, interrupt_handler);
  trap_signal(SIGALRM, alarm_handler);
  umask(022);

  sscanf(rcsid, "%*s %*s %*s %s %s %s %s %s",
		revision.number,
		revision.date,
		revision.time,
		revision.author,
		revision.state);
  sprintf(mydesc, MYDESC, revision.number);

  while ((c = getopt(argc, argv, "df:mnw:")) != EOF)
    switch (c) {
    case 'd':
      debug = 1;
      dir = DEBUGDIR;
      break;
    case 'f':
      if (getuid()) {
	puts("The 'f' option is for Store&Forward use only.");
	exit(1);
      }
      sysname = optarg;
      mode = BBS;
      doforward = 1;
      break;
    case 'm':
      mode = RMAIL;
      break;
    case 'n':
      mode = RNEWS;
      break;
    case 'w':
      sleep((unsigned long) atoi(optarg));
      break;
    case '?':
      err_flag = 1;
      break;
    }
  if (optind < argc) err_flag = 1;
  if (err_flag) {
    puts("usage: bbs [-d] [-w seconds] [-f system|-m|-n]");
    exit(1);
  }
  user.cwd = getcwd((char *) 0, 256);
  if (chdir(dir)) {
    mkdir(dir, 0755);
    if (chdir(dir)) halt();
  }

  if (uname(&utsname)) halt();
  myhostname = utsname.nodename;

  pw = doforward ? getpwnam(sysname) : getpwuid(getuid());
  if (!pw) halt();
  user.name = strsave(pw->pw_name);
  user.uid = pw->pw_uid;
  user.gid = pw->pw_gid;
  user.dir = strsave(pw->pw_dir);
  user.shell = strsave(pw->pw_shell);
  endpwent();
  sprintf(fname, "%s/%s", user.dir, SEQFILE);
  if (fp = fopen(fname, "r")) {
    fscanf(fp, "%d", &user.seq);
    fclose(fp);
  }
  if (!user.uid) level = ROOT;
  if (connect_addr(user.name)) level = MBOX;

  if ((findex = open(INDEXFILE, O_RDWR | O_CREAT, 0644)) < 0) halt();

  switch (mode) {
  case BBS:
    bbs();
    break;
  case RMAIL:
    rmail();
    break;
  case RNEWS:
    rnews();
    break;
  }
  return 0;
}

