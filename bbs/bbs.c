/* Bulletin Board System */

static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 1.72 1989/07/01 22:18:09 dk5sg Exp $";

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

extern char  *calloc();
extern char  *getenv();
extern char  *malloc();
extern char  *optarg;
extern char  *sys_errlist[];
extern int  errno;
extern int  optind;
extern long  lseek();
extern long  time();
extern struct sockaddr *build_sockaddr();
extern unsigned long  alarm();
extern unsigned long  sleep();
extern unsigned short  getgid();
extern unsigned short  getuid();
extern void _exit();
extern void exit();
extern void free();

#define DEBUGDIR    "/tmp/bbs"
#define INFOFILE    "/usr/local/lib/station.data"
#define MYDESC      "[DK5SG BBS Gaertringen, jn48kp]"
#define NARGS       20
#define WRKDIR      "/users/bbs"

#define LEN_BID     12
#define LEN_SUBJECT 80
#define LEN_TO      8
#define LEN_AT      8
#define LEN_FROM    8

struct revision {
  char  number[16];
  char  date[16];
  char  time[16];
  char  author[16];
  char  state[16];
} revision;

struct index {
  long  size;
  long  date;
  int  mesg;
  char  bid[LEN_BID+1];
  char  type;
  char  subject[LEN_SUBJECT+1];
  char  status;
  char  to[LEN_TO+1];
  char  at[LEN_AT+1];
  char  from[LEN_FROM+1];
};

struct seq {
  int  forw;
  int  list;
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
  int  type;
  int  status;
  struct strlist *head;
  struct strlist *tail;
};

static char  *arg[NARGS];
static char  *mydesc;
static char  *myhostname;
static char  loginname[80];
static int  debug;
static int  errors;
static int  fdlock = -1;
static int  findex;
static int  hostmode;
static int  locked;
static int  superuser;
static struct seq seq;
static struct utsname utsname;

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
register char  *s;
{
  register char *p;

  for (p = s; *p = toupper(uchar(*p)); p++) ;
  return s;
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

static int  strcasecmp(s, t)
register char  *s, *t;
{
  for (; tolower(uchar(*s)) == tolower(uchar(*t)); s++, t++)
    if (!*s) return 0;
  return tolower(uchar(*s)) - tolower(uchar(*t));
}

/*---------------------------------------------------------------------------*/

static char  *strtrim(s)
register char  *s;
{
  register char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(uchar(*p))) ;
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

static char  *getstring(s)
char  *s;
{

  char  *p;
  static int  chr, lastchr;

  fflush(stdout);
  alarm(60 * 60);
  for (p = s; ; ) {
    *p = '\0';
    lastchr = chr;
    chr = getchar();
    if (ferror(stdin) || feof(stdin)) {
      alarm(0);
      return NULL;
    }
    switch (chr) {
    case EOF:
      alarm(0);
      return (p == s) ? NULL : strtrim(s);
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
register char  *call;
{
  register int  d, l;

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

static void get_seq()
{

  FILE * fp;
  char  fname[80];

  sprintf(fname, "seq/seq.%s", loginname);
  if (fp = fopen(fname, "r")) {
    fscanf(fp, "%d %d", &seq.forw, &seq.list);
    fclose(fp);
  }
}

/*---------------------------------------------------------------------------*/

static void put_seq()
{

  FILE * fp;
  char  fname[80];

  sprintf(fname, "seq/seq.%s", loginname);
  if (!(fp = fopen(fname, "w")) || fprintf(fp, "%d %d\n", seq.forw, seq.list) < 0) halt();
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static int  can_forward(host)
char  *host;
{

  FILE * fp;
  char  *cp;
  char  buf[1024];
  char  s1[1024];
  char  s2[1024];
  register struct strlist *p;
  static int  initialized;
  static struct strlist *hostlist;

  if (calleq(host, loginname)) return 1;
  if (!initialized) {
    initialized = 1;
    if (!(fp = fopen("/usr/lib/mail/paths", "r"))) return 0;
    while (fgets(buf, sizeof(buf), fp))
      if (sscanf(buf, "%s%s", s1, s2) == 2) {
	if (cp = strchr(s2, '!')) *cp = '\0';
	if (!strcmp(s2, loginname)) {
	  p = (struct strlist *) malloc(sizeof(struct strlist ) + strlen(s1));
	  strcpy(p->str, s1);
	  p->next = hostlist;
	  hostlist = p;
	}
      }
    fclose(fp);
  }
  for (p = hostlist; p; p = p->next)
    if (calleq(p->str, host)) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static int  check_abort()
{

  char  buf[1024];
  int  rmask;
  static struct timeval timeout;

  rmask = 1;
  if (select(1, &rmask, (int *) 0, (int *) 0, &timeout) == 1) {
    if (!getstring(buf)) exit(1);
    if (*buf == 'A' || *buf == 'a') {
      puts("*** Aborted by User ***");
      return 1;
    }
  }
  return 0;
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
  if (locked <= 0) {
    if (fdlock < 0) {
      if ((fdlock = open("lock", O_RDWR | O_CREAT, 0644)) < 0) halt();
    }
    if (lockf(fdlock, F_LOCK, 0)) halt();
  }
  locked++;
}

/*---------------------------------------------------------------------------*/

static void unlock()
{
  if (--locked <= 0)
    if (lockf(fdlock, F_ULOCK, 0)) halt();
}

/*---------------------------------------------------------------------------*/

static int  lastmesg()
{

  FILE * fp;
  int  n;

  n = 0;
  if (fp = fopen("seq/seq.msg", "r")) {
    fscanf(fp, "%d", &n);
    fclose(fp);
  }
  return n;
}

/*---------------------------------------------------------------------------*/

static int  nextmesg()
{

  FILE * fp;
  int  n;

  n = lastmesg() + 1;
  if (!(fp = fopen("seq/seq.msg", "w")) || fprintf(fp, "%d\n", n) < 0) halt();
  fclose(fp);
  return n;
}

/*---------------------------------------------------------------------------*/

static int  numbmesg()
{

  int  n;
  struct index index;

  n = 0;
  if (lseek(findex, 0l, 0)) halt();
  while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index ))
    if (index.mesg) n++;
  return n;
}

/*---------------------------------------------------------------------------*/

static int  read_allowed(index)
struct index *index;
{
  return superuser || index->type != 'P' || calleq(index->from, loginname) || calleq(index->to, loginname);
}

/*---------------------------------------------------------------------------*/

static void unknown_command()
{
  errors++;
  if (hostmode && errors >= 3) kill(0, 1);
  printf("Unknown command '%s'.  Type ? for help.\n", arg[0]);
}

/*---------------------------------------------------------------------------*/

static char  *get_user_from_path(path)
char  *path;
{
  register char  *cp;

  return (cp = strrchr(path, '!')) ? cp + 1 : path;
}

/*---------------------------------------------------------------------------*/

static char  *get_host_from_path(path)
char  *path;
{

  register char  *cp;
  static char  tmp[1024];

  strcpy(tmp, path);
  if (!(cp = strrchr(tmp, '!'))) return "";
  *cp = '\0';
  return (cp = strrchr(tmp, '!')) ? cp + 1 : tmp;
}

/*---------------------------------------------------------------------------*/

static int  msg_uniq(bid, mid)
char  *bid, *mid;
{
  struct index index;

  if (lseek(findex, 0l, 0)) halt();
  while (read(findex, (char *) &index, sizeof(struct index )) == sizeof(struct index ))
    if (!strcmp(bid, index.bid)) return 0;
  return 1;
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
    index.mesg = nextmesg();
    index.size = 0;
    if (!(fp = fopen(filename(index.mesg), "w"))) {
      mkdir(dirname(index.mesg), 0755);
      if (!(fp = fopen(filename(index.mesg), "w"))) halt();
    }
    for (p = mail->head; p; p = p->next) {
      if (fputs(p->str, fp) == EOF) halt();
      if (putc('\n', fp) == EOF) halt();
      index.size += (strlen(p->str) + 1);
    }
    fclose(fp);
    index.date = mail->date;
    strcpy(index.bid, mail->bid);
    index.type = mail->type;
    strncpy(index.subject, mail->subject, LEN_SUBJECT);
    index.subject[LEN_SUBJECT] = '\0';
    index.status = mail->status;
    strncpy(index.to, get_user_from_path(mail->to), LEN_TO);
    index.to[LEN_TO] = '\0';
    strupc(index.to);
    strncpy(index.at, get_host_from_path(mail->to), LEN_AT);
    index.at[LEN_AT] = '\0';
    strupc(index.at);
    strncpy(index.from, get_user_from_path(mail->from), LEN_FROM);
    index.from[LEN_FROM] = '\0';
    strupc(index.from);
    if (lseek(findex, 0l, 2) < 0) halt();
    if (write(findex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
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
      fprintf(fp, "Relay-Version: DK5SG BBS version %s %s; site %s.ampr.org\n",
		  revision.number, revision.date, myhostname);
      fromhost = get_host_from_path(mail->from);
      fprintf(fp, "From: %s@%s%s\n", get_user_from_path(mail->from), fromhost, strchr(fromhost, '.') ? "" : ".ampr.org");
      mst = mail->date - 7 * 60 * 60;
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
      fprintf(fp, "Posting-Version: DK5SG BBS version %s %s; site %s.ampr.org\n",
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
  register char  *p1, *p2;

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

static void route_mail(mail)
struct mail *mail;
{

#define BidSeqFile "seq/seq.bid"
#define MidSuffix  "@bbs.net"

  FILE * fp;
  int  n;
  register char  *cp;
  register char  *s;
  struct strlist *p;

  /* Set date */

  mail->date = time((long *) 0);

  /* Fix addresses */

  fix_address(mail->from);
  fix_address(mail->to);

  /* Set type & status */

  mail->type = callvalid(get_user_from_path(mail->to)) ? 'P' : 'B';
  mail->status =
    (mail->type == 'P' || callvalid(get_host_from_path(mail->to))) ? 'N' : '$';

  /* Set bid */

  if (!*mail->bid && (cp = strchr(mail->mid, '@')) && !strcmp(cp, MidSuffix)) {
    strcpy(mail->bid, mail->mid);
    mail->bid[strlen(mail->bid)-strlen(MidSuffix)] = '\0';
  }
  if (!*mail->bid) {
    n = 0;
    lock();
    if (fp = fopen(BidSeqFile, "r")) {
      fscanf(fp, "%d", &n);
      fclose(fp);
    }
    n++;
    if (!(fp = fopen(BidSeqFile, "w")) || fprintf(fp, "%d\n", n) < 0) halt();
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
    while (cp = strchr(s, '\004'))   while (cp[0] = cp[1]) cp++;
    while (cp = strchr(s, '\032'))   while (cp[0] = cp[1]) cp++;
    while (cp = strpos(s, "/EX"))    while (cp[0] = cp[3]) cp++;
    while (cp = strpos(s, "/ex"))    while (cp[0] = cp[3]) cp++;
    while (cp = strpos(s, "***END")) while (cp[0] = cp[6]) cp++;
    while (cp = strpos(s, "***end")) while (cp[0] = cp[6]) cp++;
  }

  /* Call delivery agents */

  if (mail->type == 'P') send_to_mail(mail);
  if (mail->type == 'B') send_to_bbs(mail);
  if (mail->type == 'B'   &&
      mail->status == '$' &&
      strlen(get_user_from_path(mail->to)) > 1) send_to_news(mail);

  /* Free mail */

  while (p = mail->head) {
    mail->head = p->next;
    free((char *) p);
  }
  free((char *) mail);

}

/*---------------------------------------------------------------------------*/

static void append_line(mail, line)
struct mail *mail;
char  *line;
{
  register struct strlist *p;

  p = (struct strlist *) malloc(sizeof(struct strlist ) + strlen(line));
  p->next = NULL;
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

  register char  *p, *q;
  static char  buf[1024];

  if (*line == 'R' && line[1] == ':' && (p = strchr(strcpy(buf, line), '@'))) {
    p++;
    while (*p == ':' || isspace(uchar(*p))) p++;
    for (q = p; isalnum(uchar(*q)); q++) ;
    *q = '\0';
    return p;
  }
  return NULL;
}

/*---------------------------------------------------------------------------*/

static int  host_in_header(fname, host)
char  *fname;
char  *host;
{

  FILE  * fp;
  char  buf[1024];
  register char  *p;

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

static void s_cmd()
{

  char  *p;
  char  at[1024];
  char  line[1024];
  char  path[1024];
  int  check_header = 1;
  int  i;
  struct mail *mail;

  if (strlen(arg[0]) > 2) {
    unknown_command();
    return;
  }
  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  *at = *path = '\0';
  if (*arg[1]) strcpy(mail->to, arg[1]);
  for (i = 2; i < NARGS; i++)
    switch (*arg[i]) {
    case '@':
      strcpy(at, arg[i][1] ? arg[i] + 1 : arg[++i]);
      break;
    case '<':
      strcpy(mail->from, arg[i][1] ? arg[i] + 1 : arg[++i]);
      break;
    case '$':
      strcpy(mail->bid, arg[i][1] ? arg[i] + 1 : arg[++i]);
      break;
    }
  if (p = strchr(mail->to, '@')) {
    *p++ = '\0';
    strcpy(at, p);
  }
  if (p = strchr(at, '<')) {
    *p++ = '\0';
    strcpy(mail->from, p);
  }
  if (p = strchr(mail->from, '$')) {
    *p++ = '\0';
    strcpy(mail->bid, p);
  }
  if (*mail->bid) {
    mail->bid[LEN_BID] = '\0';
    strupc(mail->bid);
    if (!msg_uniq(mail->bid, mail->mid)) {
      puts("No");
      free((char *) mail);
      return;
    }
  }
  puts(hostmode ? "OK" : "Enter subject:");
  if (!getstring(mail->subject)) exit(1);
  if (!hostmode) puts("Enter message: (terminate with ^Z or /EX or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (*line == '\032') break;
    if (!strncmp(line, "/EX", 3)) break;
    if (!strncmp(line, "/ex", 3)) break;
    if (!strncmp(line, "***END", 6)) break;
    if (!strncmp(line, "***end", 6)) break;
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
  if (!*mail->to) strcpy(mail->to, "alle");
  if (*at) {
    strcat(mail->to, "@");
    strcat(mail->to, at);
  }
  if (!*mail->from || !hostmode && !superuser) strcpy(mail->from, loginname);
  if (*path) {
    strcpy(line, mail->from);
    sprintf(mail->from, "%s!%s", path, line);
  }
  if (!hostmode) /*!!!!!!!!!! Print Ack !!!!!!!!!!!!!!!*/ ;
  route_mail(mail);
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

static void b_cmd()
{
  if (arg[0][1]) {
    unknown_command();
    return;
  }
  puts("BBS mode terminated.");
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void c_cmd()
{

  char  *tempfile = "index.tmp";
  int  delete;
  int  f;
  long  keepdate;
  struct index index;
  struct stat statbuf;

  if (arg[0][1] || !superuser) {
    unknown_command();
    return;
  }
  keepdate = time((long *) 0) - 90l * 24l * 60l * 60l;
  if ((f = open(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) halt();
  if (lseek(findex, 0l, 0)) halt();
  while (read(findex, (char *) &index, sizeof(struct index )) == sizeof(struct index )) {
    delete = (index.mesg == 0 && *index.bid == '\0')      ||
	     (index.mesg == 0 && index.date < keepdate)   ||
	     (index.type == 'P' && index.date < keepdate) ||
	     (index.status == 'F');
    if (index.mesg) {
      index.size = (delete || stat(filename(index.mesg), &statbuf)) ? 0 : statbuf.st_size;
      if (!index.size) {
	delete = 1;
	unlink(filename(index.mesg));
      }
    }
    if (!delete)
      if (write(f, (char *) &index, sizeof(struct index )) != sizeof(struct index )) halt();
  }
  if (close(f)) halt();
  if (close(findex)) halt();
  if (rename(tempfile, "index")) halt();
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void f_cmd(do_not_exit)
int  do_not_exit;
{

  FILE * fp;
  char  buf[1024];
  int  c;
  struct index index;
  struct tm *tm;

  if (strcmp(arg[0], "f>") || !hostmode) {
    unknown_command();
    return;
  }
  if (lseek(findex, 0l, 0)) halt();
  while (read(findex, (char *) &index, sizeof(struct index )) == sizeof(struct index )) {
    if ((index.status == '$' && index.mesg > seq.forw ||
	 index.status != '$' && index.status != 'F' && index.mesg && can_forward(index.at)) &&
	!host_in_header(filename(index.mesg), loginname)) {
      do_not_exit = 1;
      printf("S%c %s%s%s < %s%s%s\n", index.type, index.to, *index.at ? " @ " : "", index.at, index.from, *index.bid ? " $" : "", index.bid);
      if (!getstring(buf)) exit(1);
      switch (*strlwc(buf)) {
      case 'o':
	puts(index.subject);
	tm = gmtime(&index.date);
	printf("R:%02d%02d%02d/%02d%02dz %d@%s %s\n",
	       tm->tm_year % 100, tm->tm_mon + 1, tm->tm_mday,
	       tm->tm_hour, tm->tm_min, index.mesg, myhostname, mydesc);
	if (!(fp = fopen(filename(index.mesg), "r"))) halt();
	while ((c = getc(fp)) != EOF) putchar(c);
	fclose(fp);
	puts("\032");
	wait_for_prompt();
	if (index.status == 'N' || index.status == 'Y') {
	  index.status = 'F';
	  if (lseek(findex, (long) (-sizeof(struct index )), 1) < 0) halt();
	  if (write(findex, (char *) &index, sizeof(struct index )) != sizeof(struct index )) halt();
	}
	break;
      case 'n':
	wait_for_prompt();
	break;
      default:
	exit(1);
      }
    }
    if (seq.forw < index.mesg) {
      seq.forw = index.mesg;
      put_seq();
    }
  }
  if (do_not_exit)
    putchar('F');
  else
    exit(0);
}

/*---------------------------------------------------------------------------*/

static void h_cmd()
{
  putchar('\n');
  switch (arg[0][1] ? arg[0][1] : *arg[1]) {
  case '?':
    puts("? - Display help.");
    putchar('\n');
    puts("? [cmd]");
    break;
  case 'a':
    puts("A - Abort current activity.");
    break;
  case 'b':
    puts("B - Bye, terminate BBS mode.");
    break;
  case 'h':
    puts("H - Display help.");
    putchar('\n');
    puts("H [cmd]");
    break;
  case 'i':
    puts("I - Display information about the system.");
    break;
  case 'k':
    puts("K - Kill messages.");
    putchar('\n');
    puts("K msg ...");
    puts("KM");
    break;
  case 'l':
    puts("L - List message headers.");
    putchar('\n');
    puts("L         [first [last]]");
    puts("L< from   [first [last]]");
    puts("L> to     [first [last]]");
    puts("L@ at     [first [last]]");
    puts("LB        [first [last]]");
    puts("LL count  [first [last]]");
    puts("LM        [first [last]]");
    puts("LN        [first [last]]");
    puts("LS substr [first [last]]");
    break;
  case 'm':
    puts("M - Set MYBBS.");
    putchar('\n');
    puts("M call");
    break;
  case 'q':
    puts("Q - Quit, terminate BBS mode.");
    break;
  case 'r':
    puts("R - Read messages.");
    putchar('\n');
    puts("R msg ...");
    puts("RM");
    puts("RN");
    break;
  case 's':
    puts("S - Send a message.");
    putchar('\n');
    puts("S  to [@ at] [< from] [$bid]");
    puts("SB to [@ at] [< from] [$bid]");
    puts("SP to [@ at] [< from] [$bid]");
    break;
  case 'v':
    puts("V - Display BBS version and status.");
    break;
  default:
    puts("Command summary:");
    putchar('\n');
    puts("? - Display help.");
    puts("A - Abort current activity.");
    puts("B - Bye, terminate BBS mode.");
    puts("H - Display help.");
    puts("I - Display information about the system.");
    puts("K - Kill messages.");
    puts("L - List message headers.");
    puts("M - Set MYBBS.");
    puts("Q - Quit, terminate BBS mode.");
    puts("R - Read messages.");
    puts("S - Send a message.");
    puts("V - Display BBS version and status.");
    putchar('\n');
    puts("Try ? cmd for more help about cmd.");
    break;
  }
  putchar('\n');
}

/*---------------------------------------------------------------------------*/

static void host_cmd()
{
  if (!hostmode) unknown_command();
}

/*---------------------------------------------------------------------------*/

static void i_cmd()
{

  FILE * fp;
  int  c;

  if (arg[0][1]) {
    unknown_command();
    return;
  }
  if (!(fp = fopen(INFOFILE, "r"))) halt();
  while ((c = getc(fp)) != EOF) {
    putchar(c);
    if (c == '\n' && check_abort()) break;
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void do_kill(index)
struct index *index;
{
  int  mesg;

  if (superuser || calleq(index->from, loginname) || calleq(index->to, loginname)) {
    if (unlink(filename(mesg = index->mesg))) halt();
    index->mesg = 0;
    if (lseek(findex, (long) (-sizeof(struct index )), 1) < 0) halt();
    if (write(findex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
    printf("*** Msg # %d - Killed\n", mesg);
  } else
    printf("You are not authorized to kill Msg # %d\n", index->mesg);
}

/*---------------------------------------------------------------------------*/

static void k_cmd()
{

  int  found;
  int  i;
  int  mesg;
  struct index index;

  if (!strcmp(arg[0], "km")) {
    found = 0;
    if (lseek(findex, 0l, 0)) halt();
    while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index ))
      if (index.mesg && calleq(index.to, loginname) && (index.status == 'Y' || index.status == 'F')) {
	do_kill(&index);
	found = 1;
      }
    if (!found) puts("No matching message found.");
    return;
  }

  if (arg[0][1]) {
    unknown_command();
    return;
  }
  for (i = 1; i < NARGS && *arg[i]; i++) {
    found = 0;
    if ((mesg = atoi(arg[i])) > 0) {
      if (lseek(findex, 0l, 0)) halt();
      while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index ))
	if (index.mesg == mesg) {
	  do_kill(&index);
	  found = 1;
	  break;
	}
    }
    if (!found) printf("No such message: '%s'.\n", arg[i]);
  }
}

/*---------------------------------------------------------------------------*/

static void l_cmd()
{

  char  *to, *at, *from, *sub;
  char  buf[1024];
  int  found, update_seq, status, type;
  int  i;
  int  min, max, num;
  int  tmp;
  struct index index;

  to = at = from = sub = NULL;
  found = update_seq = status = type = 0;
  min = 1;
  max = num = 32767;
  i = 1;
  switch (arg[0][1]) {
  case '\0':
    min = seq.list + 1;
    update_seq = !*arg[1];
    break;
  case '<':
    from = arg[i++];
    break;
  case '>':
    to = arg[i++];
    break;
  case '@':
    at = arg[i++];
    break;
  case 'b':
    type = 'B';
    break;
  case 'l':
    num = atoi(arg[i++]);
    break;
  case 'n':
    status = 'N';
  case 'm':
    to = loginname;
    break;
  case 's':
    sub = arg[i++];
    break;
  default:
    unknown_command();
    return;
  }
  if (tmp = atoi(arg[i++])) min = tmp;
  if (tmp = atoi(arg[i++])) max = tmp;
  if (min > max) {
    tmp = max;
    max = min;
    min = tmp;
  }
  if (lseek(findex, (long) (-sizeof(struct index )), 2) >= 0) {
    for (; ; ) {
      if (check_abort()) return;
      if (read(findex, (char *) &index, sizeof(struct index )) != sizeof(struct index )) halt();
      if (index.mesg >= min && index.mesg <= max  &&
	  read_allowed(&index)                    &&
	  (superuser || index.to[1])              &&
	  (!type     || type == index.type)       &&
	  (!status   || status == index.status)   &&
	  (!from     || calleq(from, index.from)) &&
	  (!to       || calleq(to, index.to))     &&
	  (!at       || calleq(at, index.at))     &&
	  (!sub      || strpos(strlwc(strcpy(buf, index.subject)), sub))) {
	if (!found) {
	  puts(" Msg#  Size To      @ BBS     From     Date    Subject");
	  found = 1;
	}
	printf("%5d %5ld %-8s%c%-8s %-8s %-7.7s %.32s\n", index.mesg, index.size, index.to, *index.at ? '@' : ' ', index.at, index.from, timestr(index.date), index.subject);
	if (update_seq && seq.list < index.mesg) {
	  seq.list = index.mesg;
	  put_seq();
	}
	if (--num <= 0) break;
      }
      if (lseek(findex, -2l * sizeof(struct index ), 1) < 0) break;
    }
  }
  if (!found) puts(update_seq ? "No new messages since last L command." : "No matching message found.");
}

/*---------------------------------------------------------------------------*/

static void m_cmd()
{

  char  line[1024];
  struct mail *mail;

  if (arg[0][1]) {
    unknown_command();
    return;
  }
  if (!callvalid(strupc(arg[1]))) {
    printf("Invalid call '%s'.\n", arg[1]);
    return;
  }
  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  strcpy(mail->from, loginname);
  strcpy(mail->to, "m@thebox");
  sprintf(line, "%s %ld", arg[1], time((long *) 0));
  strcpy(mail->subject, line);
  sprintf(line, "de %s @ %s", loginname, myhostname);
  append_line(mail, line);
  append_line(mail, "");
  append_line(mail, "");
  append_line(mail, "");
  append_line(mail, "");
  append_line(mail, "");
  route_mail(mail);
}

/*---------------------------------------------------------------------------*/

static int  do_read(index)
struct index *index;
{

  FILE * fp;
  char  *p;
  char  buf[1024];
  char  path[1024];
  int  inheader;

  if (!(fp = fopen(filename(index->mesg), "r"))) halt();
  printf("Msg# %d Type:%c Stat:%c To: %s%s%s From: %s Date: %s\n",
	 index->mesg,
	 index->type,
	 index->status,
	 index->to,
	 *index->at ? " @" : "",
	 index->at,
	 index->from,
	 timestr(index->date));
  if (*index->subject) printf("Subject: %s\n", index->subject);
  if (*index->bid) printf("Bulletin ID: %s\n", index->bid);
  *path = '\0';
  inheader = 1;
  while (fgets(buf, sizeof(buf), fp)) {
    if (check_abort()) {
      fclose(fp);
      return 0;
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
  if (index->status == 'N' && (index->type != 'P' || calleq(index->to, loginname))) {
    index->status = 'Y';
    if (lseek(findex, (long) (-sizeof(struct index )), 1) < 0) halt();
    if (write(findex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

static void r_cmd()
{

  int  found;
  int  i;
  int  mesg;
  struct index index;

  if (!strcmp(arg[0], "rm") || !strcmp(arg[0], "rn")) {
    found = 0;
    if (lseek(findex, 0l, 0)) halt();
    while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index ))
      if (index.mesg && calleq(index.to, loginname) && (arg[0][1] == 'm' || index.status == 'N')) {
	if (!do_read(&index)) return;
	found = 1;
      }
    if (!found) puts("No matching message found.");
    return;
  }

  if (arg[0][1]) {
    unknown_command();
    return;
  }
  for (i = 1; i < NARGS && *arg[i]; i++) {
    found = 0;
    if ((mesg = atoi(arg[i])) > 0) {
      if (lseek(findex, 0l, 0)) halt();
      while (read(findex, (char *) & index, sizeof(struct index )) == sizeof(struct index ))
	if (index.mesg == mesg && read_allowed(&index)) {
	  if (!do_read(&index)) return;
	  found = 1;
	  break;
	}
    }
    if (!found) printf("No such message: '%s'.\n", arg[i]);
  }
}

/*---------------------------------------------------------------------------*/

static void v_cmd()
{
  if (arg[0][1]) {
    unknown_command();
    return;
  }
  printf("DK5SG BBS %s %s\n", revision.number, revision.date);
  printf("Active: %d   Next: %d\n", numbmesg(), lastmesg() + 1);
}

/*---------------------------------------------------------------------------*/

#include <memory.h>
#include <termio.h>

static void z_cmd()
{

  FILE * fp = 0;
  char  buf[1024];
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

  if (arg[0][1] || !superuser) {
    unknown_command();
    return;
  }

  if (fstat(findex, &statbuf)) halt();
  indexarraysize = statbuf.st_size;
  indexarrayentries = indexarraysize / sizeof(struct index );
  if (!indexarrayentries) halt();
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

  pi = indexarray;
  cmd = '\b';
  for (; ; ) {
    lines = 0;
    while (!cmd) cmd = getchar();
    switch (cmd) {
    case '\b':
      if (pi > indexarray) pi--;
      while (!pi->mesg && pi > indexarray) pi--;
      while (!pi->mesg && pi < indexarray + (indexarrayentries - 1)) pi++;
      cmd = 'r';
      break;
    case 'k':
      if (unlink(filename(pi->mesg))) halt();
      pi->mesg = 0;
      if (lseek(findex, (long) ((pi - indexarray) * sizeof(struct index )), 0) < 0) halt();
      if (write(findex, (char *) pi, sizeof(struct index )) != sizeof(struct index )) halt();
    case '\n':
      if (pi < indexarray + (indexarrayentries - 1)) pi++;
      while (!pi->mesg && pi < indexarray + (indexarrayentries - 1)) pi++;
      while (!pi->mesg && pi > indexarray) pi--;
    case 'r':
      if (fp) {
	fclose(fp);
	fp = 0;
      }
      if (!(fp = fopen(filename(pi->mesg), "r"))) halt();
      printf("\033&a0y0C\033JMsg# %d Type:%c Stat:%c To: %s%s%s From: %s Date: %s\n", pi->mesg, pi->type, pi->status, pi->to, *pi->at ? " @" : "", pi->at, pi->from, timestr(pi->date));
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
      for (mesg = 0; isdigit(cmd); cmd = getchar())
	mesg = 10 * mesg + cmd - '0';
      pi = indexarray;
      while (mesg > pi->mesg && pi < indexarray + (indexarrayentries - 1)) pi++;
      while (!pi->mesg && pi > indexarray) pi--;
      cmd = 'r';
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
      exit(0);
      break;
    case '?':
      puts("----------------------------------- Commands ----------------------------------");
      puts("<number>   show numbered entry");
      puts("?          print command summary");
      puts("BACKSPACE  show previous entry");
      puts("RETURN     show next entry");
      puts("SPACE      show next screenful");
      puts("k          delete current entry");
      puts("q          quit BBS");
      puts("r          redisplay current entry");
      puts("v          edit current entry");
      printf("\nPress any key to continue ");
      getchar();
      cmd = 'r';
      break;
    default:
      putchar('\007');
      fflush(stdout);
      cmd = 0;
      break;
    }
  }
}

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

  if (!(address = connect_addr(loginname))) exit(1);
  if (!(addr = build_sockaddr("unix:/tcp/sockets/netcmd", &addrlen))) exit(1);
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0) exit(1);
  if (connect(fd, addr, addrlen)) exit(1);
  dup2(fd, 0);
  dup2(fd, 1);
  close(fd);
  fdopen(0, "r+");
  fdopen(1, "r+");
  printf("connect %s\n", address);
}

/*---------------------------------------------------------------------------*/

static void bbs(doforward)
int  doforward;
{

  char  line[1024];
  char  quote;
  int  i;
  register char  *p;

  if (doforward) {
    connect_bbs();
    wait_for_prompt();
  }
  printf("[DK5SG-%s-H$]\n", revision.number);
  if (doforward) wait_for_prompt();
  for (; ; ) {
    if (doforward)
      strcpy(line, "f>");
    else {
      if (hostmode)
	puts(">");
      else
	printf("%s de %s-BBS \007 >\n", loginname, myhostname);
      if (!getstring(line)) exit(1);
    }
    for (p = strlwc(line), i = 0; i < NARGS; i++) {
      quote = '\0';
      while (isspace(uchar(*p))) p++;
      if (*p == '"' || *p == '\'') quote = *p++;
      arg[i] = p;
      if (quote) {
	if (!(p = strchr(p, quote))) p = "";
      } else
	while (*p && !isspace(uchar(*p))) p++;
      if (*p) *p++ = '\0';
    }
    switch (*arg[0]) {
    case '\0':                    break;
    case '?':  h_cmd();           break;
    case '[':  host_cmd();        break;
    case 'a':                     break;
    case 'b':  b_cmd();           break;
    case 'c':  c_cmd();           break;
    case 'f':  f_cmd(doforward);  break;
    case 'h':  h_cmd();           break;
    case 'i':  i_cmd();           break;
    case 'k':  k_cmd();           break;
    case 'l':  l_cmd();           break;
    case 'm':  m_cmd();           break;
    case 'q':  b_cmd();           break;
    case 'r':  r_cmd();           break;
    case 's':  s_cmd();           break;
    case 'v':  v_cmd();           break;
    case 'z':  z_cmd();           break;
    default:   unknown_command(); break;
    }
    doforward = 0;
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

  char  *cp;
  char  *dir = WRKDIR;
  int  c;
  int  doforward = 0;
  int  err_flag = 0;
  int  mode = BBS;

  sscanf(rcsid, "%*s %*s %*s %s %s %s %s %s",
		revision.number, revision.date, revision.time,
		revision.author, revision.state);
  superuser = (getuid() == 0 && getgid() == 1);
  umask(022);
  if (uname(&utsname)) halt();
  myhostname = utsname.nodename;
  mydesc = MYDESC;
  cp = getenv("LOGNAME");
  if (!cp || !*cp) halt();
  strlwc(strcpy(loginname, cp));
  while ((c = getopt(argc, argv, "df:mn")) != EOF)
    switch (c) {
    case 'd':
      debug = 1;
      dir = DEBUGDIR;
      break;
    case 'f':
      if (!superuser) {
	puts("Permission denied");
	exit(1);
      }
      strlwc(strcpy(loginname, optarg));
      hostmode = 1;
      mode = BBS;
      doforward = 1;
      break;
    case 'm':
      mode = RMAIL;
      break;
    case 'n':
      mode = RNEWS;
      break;
    case '?':
      err_flag = 1;
      break;
    }
  if (optind < argc) err_flag = 1;
  if (err_flag) {
    puts("usage: bbs [-d] [-f system|-m|-n]");
    exit(1);
  }
  mkdir(dir, 0755);
  if (chdir(dir)) halt();
  if (connect_addr(loginname)) hostmode = 1;
  if ((findex = open("index", O_RDWR | O_CREAT, 0644)) < 0) halt();
  mkdir("seq", 0755);
  get_seq();
  switch (mode) {
  case BBS:
    bbs(doforward);
    break;
  case RMAIL:
    rmail();
    break;
  case RNEWS:
    rnews();
    break;
  }
  exit(0);
  return 0;
}

