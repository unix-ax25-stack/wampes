static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 2.90 1995/05/13 18:46:55 deyke Exp $";

/* Bulletin Board System */

#ifdef __cplusplus
#define volatile
#endif

#include <sys/types.h>

#include <stdio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifdef ibm032

#include <sys/dir.h>
#include <sys/fcntl.h>
#include <sys/file.h>

typedef int pid_t;

#define dirent direct

#else

#include <dirent.h>
#include <sys/time.h>

#endif

extern char *optarg;
extern int optind;

#include "bbs.h"
#include "buildsaddr.h"
#include "callvalid.h"
#include "configure.h"
#include "lockfile.h"
#include "seek.h"
#include "seteugid.h"
#include "strdup.h"

#define DEBUG_NNTP      0

#define SECONDS         (1L)
#define MINUTES         (60L*SECONDS)
#define HOURS           (60L*MINUTES)
#define DAYS            (24L*HOURS)

#define SIZEINDEX       (sizeof(struct index))

enum e_level {
  USER,
  MBOX,
  ROOT
};

enum e_type {
  NEWS,
  MAIL
};

enum e_where {
  HEAD,
  TAIL
};

struct strlist {
  struct strlist *next;
  char str[1];
};

struct mail {
  char fromuser[1024];
  char fromhost[1024];
  char touser[1024];            /* Group, Board */
  char tohost[1024];            /* Distribution, At */
  char subject[1024];
  char bid[1024];
  char mid[1024];
  long date;
  int lifetime;
  struct strlist *head;
  struct strlist *tail;
};

struct dir_entry {
  struct dir_entry *left, *right;
  int count;
  char to[LEN_TO + 1];
};

struct cmdtable {
  const char *name;
  void (*fnc)(int argc, char **argv);
  int argc;
  enum e_level level;
};

struct revision {
  char number[16];
  char date[16];
  char time[16];
  char author[16];
  char state[16];
};

static struct revision revision;

struct user {
  char *name;
  int uid;
  int gid;
  char *dir;
  char *shell;
  int seq;
};

static struct user user;

struct nntp_channel {
  int fd;
  int incnt;
  char *inptr;
  char inbuf[1024];
};

static struct nntp_channel nntp_channel = {
  -1
};

static char prompt[1024] = "bbs> ";
static char *myhostname;
static const char daynames[] = "SunMonTueWedThuFriSat";
static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static int did_forward;
static int do_forward;
static int errors;
static int fdindex;
static int fdseq;
static int level;
static int packetcluster;
static volatile int stopped;

/*---------------------------------------------------------------------------*/

static void errorstop(int line)
{
  char buf[80];

  sprintf(buf, "Fatal error in line %d", line);
  perror(buf);
  fprintf(stderr, "Program stopped.\n");
  exit(1);
}

/*---------------------------------------------------------------------------*/

#define halt() errorstop(__LINE__)

/*---------------------------------------------------------------------------*/

#if defined _SC_OPEN_MAX && !(defined __386BSD__ || \
			      defined __bsdi__   || \
			      defined __FreeBSD__)
#define open_max()      ((int) sysconf(_SC_OPEN_MAX))
#else
#define open_max()      (1024)
#endif

/*---------------------------------------------------------------------------*/

/* Use private function because some platforms are broken, eg 386BSD */

static int Xtolower(int c)
{
  return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}

/*---------------------------------------------------------------------------*/

/* Use private function because some platforms are broken, eg 386BSD */

static int Xtoupper(int c)
{
  return (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
}

/*---------------------------------------------------------------------------*/

static char *strupc(char *s)
{
  char *p;

  for (p = s; (*p = Xtoupper(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *strlwc(char *s)
{
  char *p;

  for (p = s; (*p = Xtolower(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static int Strcasecmp(const char *s1, const char *s2)
{
  while (Xtolower(*s1) == Xtolower(*s2)) {
    if (!*s1) return 0;
    s1++;
    s2++;
  }
  return Xtolower(*s1) - Xtolower(*s2);
}

/*---------------------------------------------------------------------------*/

static int Strncasecmp(const char *s1, const char *s2, int n)
{
  while (--n >= 0 && Xtolower(*s1) == Xtolower(*s2)) {
    if (!*s1) return 0;
    s1++;
    s2++;
  }
  return n < 0 ? 0 : Xtolower(*s1) - Xtolower(*s2);
}

/*---------------------------------------------------------------------------*/

#define calleq(c1, c2) (!Strcasecmp((c1), (c2)))

/*---------------------------------------------------------------------------*/

static const char *Strcasestr(const char *str, const char *pat)
{
  const char *s, *p;

  for (; ; str++)
    for (s = str, p = pat; ; s++, p++) {
      if (!*p) return str;
      if (!*s) return 0;
      if (Xtolower(*s) != Xtolower(*p)) break;
    }
}

/*---------------------------------------------------------------------------*/

static char *strtrim(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(*p & 0xff)) ;
  p[1] = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *rip(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && (*p == '\r' || *p == '\n')) ;
  p[1] = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static struct strlist *alloc_string(const char *s)
{
  struct strlist *p;

  p = (struct strlist *) malloc(sizeof(struct strlist *) + strlen(s) + 1);
  if (!p)
    halt();
  p->next = 0;
  strcpy(p->str, s);
  return p;
}

/*---------------------------------------------------------------------------*/

static void interrupt_handler(int sig)
{
  signal(sig, interrupt_handler);
  stopped = 1;
}

/*---------------------------------------------------------------------------*/

static void alarm_handler(int sig)
{
  puts("\n*** Timeout ***");
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void getstring(char *s)
{

  char *p;
  static int chr;
  static int lastchr;

  fflush(stdout);
  alarm((unsigned) (1 * HOURS));
  for (p = s;;) {
    *p = 0;
    lastchr = chr;
    chr = getchar();
    if (stopped) {
      alarm(0);
      clearerr(stdin);
      *s = 0;
      return;
    }
    if (ferror(stdin) || feof(stdin))
      exit(1);
    switch (chr) {
    case 0:
      break;
    case EOF:
      if (p == s)
	exit(1);
      /* fall thru */
    case '\r':
      alarm(0);
      return;
    case '\n':
      if (lastchr != '\r') {
	alarm(0);
	return;
      }
      break;
    default:
      *p++ = chr;
      break;
    }
  }
}

/*---------------------------------------------------------------------------*/

static char *rfc822_date_string(long gmt)
{

  static char buf[32];
  struct tm *tm;

  tm = gmtime(&gmt);
  sprintf(buf, "%.3s, %d %.3s %02d %02d:%02d:%02d GMT",
	  daynames + 3 * tm->tm_wday,
	  tm->tm_mday,
	  monthnames + 3 * tm->tm_mon,
	  tm->tm_year % 100,
	  tm->tm_hour,
	  tm->tm_min,
	  tm->tm_sec);
  return buf;
}

/*---------------------------------------------------------------------------*/

static long calculate_date(int yy, int mm, int dd,
			   int h, int m, int s,
			   int tzcorrection)
{

  static int mdays[] = {
    31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  int i;
  long jdate;

  if (yy <= 37)
    yy += 2000;
  if (yy <= 99)
    yy += 1900;
  mdays[1] = 28 + (yy % 4 == 0 && (yy % 100 || yy % 400 == 0));
  mm--;
  if (yy < 1970 || yy > 2037 ||
      mm < 0 || mm > 11 ||
      dd < 1 || dd > mdays[mm] ||
      h < 0 || h > 23 ||
      m < 0 || m > 59 ||
      s < 0 || s > 59)
    return -1;
  jdate = dd - 1;
  for (i = 0; i < mm; i++)
    jdate += mdays[i];
  for (i = 1970; i < yy; i++)
    jdate += 365 + (i % 4 == 0);
  jdate *= (24L * 60L * 60L);
  return jdate + 3600 * h + 60 * m + s - tzcorrection;
}

/*---------------------------------------------------------------------------*/

static long parse_rfc822_date(const char *str)
{

  char monthstr[4];
  char tzstr[1024];
  char *p;
  int dd;
  int h;
  int i;
  int m;
  int s;
  int tzcorrection;
  int yy;

  i = sscanf((char *) str,
	     "%*s %d %3s %d %d:%d:%d %s",
	     &dd, monthstr, &yy, &h, &m, &s, tzstr);
  if (i < 6)
    return -1;
  if (i == 7 &&
      (tzstr[0] == '-' || tzstr[0] == '+') &&
      isdigit(tzstr[1] & 0xff) &&
      isdigit(tzstr[2] & 0xff) &&
      isdigit(tzstr[3] & 0xff) &&
      isdigit(tzstr[4] & 0xff) &&
      !tzstr[5]) {
    tzcorrection =
	(tzstr[1] - '0') * 36000 +
	(tzstr[2] - '0') * 3600 +
	(tzstr[3] - '0') * 600 +
	(tzstr[4] - '0') * 60;
    if (tzstr[0] == '-')
      tzcorrection = -tzcorrection;
  } else {
    tzcorrection = 0;
  }
  if (strlen(monthstr) != 3)
    return -1;
  p = strstr(monthnames, monthstr);
  if (!p)
    return -1;
  return calculate_date(yy, (p - monthnames) / 3 + 1, dd,
			h, m, s, tzcorrection);
}

/*---------------------------------------------------------------------------*/

static void write_nntp(const char *line)
{

#if DEBUG_NNTP
  printf("NNTP> %s", line);
#endif

  if (write(nntp_channel.fd, line, strlen(line)) < 0)
    halt();
}

/*---------------------------------------------------------------------------*/

static void read_nntp(char *line)
{

  int chr;

#if DEBUG_NNTP
  char *l = line;
#endif

  for (;;) {
    if (nntp_channel.incnt <= 0) {
      nntp_channel.incnt = read(nntp_channel.fd, nntp_channel.inptr = nntp_channel.inbuf, sizeof(nntp_channel.inbuf));
      if (nntp_channel.incnt <= 0)
	halt();
    }
    nntp_channel.incnt--;
    switch (chr = *nntp_channel.inptr++) {
    case '\r':
      break;
    case '\n':
      *line = 0;
#if DEBUG_NNTP
      printf("NNTP< %s\n", l);
#endif
      return;
    default:
      *line++ = chr;
      break;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void open_nntp(void)
{

  char line[1024];
  int addrlen;
  struct sockaddr *addr;

  if (!(addr = build_sockaddr("127.0.0.1:119", &addrlen)))
    halt();
  if ((nntp_channel.fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0)
    halt();
  if (connect(nntp_channel.fd, addr, addrlen))
    halt();

  read_nntp(line);
  if (*line != '2')
    halt();

  write_nntp("mode reader\r\n");
  read_nntp(line);
}

/*---------------------------------------------------------------------------*/

static void make_parent_directories(const char *filename)
{

  char dirname[1024];
  char *p;

  strcpy(dirname, filename);
  p = strrchr(dirname, '/');
  if (!p)
    halt();
  *p = 0;
  if (!*dirname)
    halt();
  if (!mkdir(dirname, 0755))
    return;
  if (errno != ENOENT)
    halt();
  make_parent_directories(dirname);
  if (mkdir(dirname, 0755))
    halt();
}

/*---------------------------------------------------------------------------*/

static char *getfilename(int mesg)
{
  static char buf[80];

  sprintf(buf,
	  WRKDIR "/%02x/%02x/%02x/%02x",
	  (mesg >> 24) & 0xff,
	  (mesg >> 16) & 0xff,
	  (mesg >>  8) & 0xff,
	  (mesg      ) & 0xff);
  return buf;
}

/*---------------------------------------------------------------------------*/

static void get_seq(void)
{

  char buf[16];
  char fname[1024];

  sprintf(fname, "%s/%s", user.dir, SEQFILE);
  seteugid(user.uid, user.gid);
  fdseq = open(fname, O_RDWR | O_CREAT, 0644);
  seteugid(0, 0);
  if (fdseq < 0) halt();
  if (lock_fd(fdseq, 1)) {
    puts("Sorry, you are already running another BBS.\n");
    exit(1);
  }
  if (read(fdseq, buf, sizeof(buf)) >= 2) user.seq = atoi(buf);
}

/*---------------------------------------------------------------------------*/

static void put_seq(void)
{

  char buf[16];
  int n;

  sprintf(buf, "%d\n", user.seq);
  n = strlen(buf);
  if (lseek(fdseq, 0L, SEEK_SET)) halt();
  if (write(fdseq, buf, n) != n) halt();
}

/*---------------------------------------------------------------------------*/

static void wait_for_prompt(void)
{

  char buf[1024];
  int l;

  do {
    getstring(buf);
    l = strlen(buf);
  } while (!l || buf[l - 1] != '>');
}

/*---------------------------------------------------------------------------*/

static int get_index(int n, struct index *index)
{

  int i1, i2, im;
  long pos;

  i1 = 0;
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (read(fdindex, index, SIZEINDEX) != SIZEINDEX) return 0;
  if (n == index->mesg) return 1;
  if (n < index->mesg) return 0;

  if ((pos = lseek(fdindex, -SIZEINDEX, SEEK_END)) < 0) halt();
  i2 = (int) (pos / SIZEINDEX);
  if (read(fdindex, index, SIZEINDEX) != SIZEINDEX) halt();
  if (n == index->mesg) return 1;
  if (n > index->mesg) return 0;

  while (i1 + 1 < i2) {
    im = (i1 + i2) / 2;
    if (lseek(fdindex, im * SIZEINDEX, SEEK_SET) < 0) halt();
    if (read(fdindex, index, SIZEINDEX) != SIZEINDEX) halt();
    if (n == index->mesg) return 1;
    if (n > index->mesg)
      i1 = im;
    else
      i2 = im;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int read_allowed(const struct index *index)
{
  if (index->deleted) return 0;
  if (level == ROOT) return 1;
  if (index->to[1]) return 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

static void translate_bangs(char *s)
{

  char *p;
  static char tmp[1024];

  if ((p = strchr(s, '!'))) {
    *p = 0;
    translate_bangs(p + 1);
    sprintf(tmp, "%s@%s", p + 1, s);
    strcpy(s, tmp);
  }
}

/*---------------------------------------------------------------------------*/

static void split_address(const char *addr, char *userpart, char *hostpart)
{

  char buf[1024];
  char *cp;
  const char *from;

  for (cp = buf, from = addr; *from; from++)
    if (*from == '%')
      *cp++ = '@';
    else
      *cp++ = Xtolower(*from & 0xff);
  *cp = 0;

  translate_bangs(buf);

  strcpy(userpart, buf);
  cp = strchr(userpart, '@');
  if (!cp) {
    strcpy(hostpart, myhostname);
  } else {
    *cp = 0;
    strcpy(hostpart, cp + 1);
    cp = strchr(hostpart, '@');
    if (cp)
      *cp = 0;
  }

  if (!*userpart || !strcmp(userpart, "mailer-daemon"))
    strcpy(userpart, myhostname);
  if ((cp = strchr(hostpart, '.')))
    *cp = 0;
  if (!*hostpart)
    strcpy(hostpart, myhostname);
}

/*---------------------------------------------------------------------------*/

static char *get_host_from_header(const char *line)
{

  char *p;
  char *q;
  static char buf[1024];

  if (*line == 'R' && line[1] == ':' && (p = strchr(strcpy(buf, line), '@'))) {
    p++;
    while (*p == ':' || isspace(*p & 0xff))
      p++;
    for (q = p; isalnum(*q & 0xff); q++) ;
    *q = 0;
    if (*p)
      return strlwc(p);
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static long get_date_from_header(const char *line)
{

  int dd;
  int h;
  int mm;
  int m;
  int yy;

  if (sscanf((char *) line,
	     "R:%02d%02d%02d/%02d%02d",
	     &yy, &mm, &dd, &h, &m) != 5)
    return -1;
  return calculate_date(yy, mm, dd, h, m, 0, 0);
}

/*---------------------------------------------------------------------------*/

static void send_to_bbs(struct mail *mail)
{

  FILE *fp;
  int fdlock;
  int mesg;
  struct index index;
  struct strlist *p;

  if ((fdlock = lock_file(SENDLOCKFILE, 0)) < 0)
    halt();
  if (lseek(fdindex, -SIZEINDEX, SEEK_END) < 0)
    mesg = 1;
  else {
    if (read(fdindex, &index, SIZEINDEX) != SIZEINDEX)
      halt();
    mesg = index.mesg + 1;
  }
  memset((char *) &index, 0, SIZEINDEX);
  index.mesg = mesg;
  index.date = mail->date;
  strncpy(index.bid, mail->bid, LEN_BID);
  strupc(index.bid);
  strncpy(index.subject, mail->subject, LEN_SUBJECT);
  strncpy(index.to, mail->touser, LEN_TO);
  strupc(index.to);
  strncpy(index.at, mail->tohost, LEN_AT);
  strupc(index.at);
  strncpy(index.from, mail->fromuser, LEN_FROM);
  strupc(index.from);
  if (!(fp = fopen(getfilename(index.mesg), "w"))) {
    make_parent_directories(getfilename(index.mesg));
    if (!(fp = fopen(getfilename(index.mesg), "w")))
      halt();
  }
  if (strcmp(index.to, "E") && strcmp(index.to, "M"))
    for (p = mail->head; p; p = p->next) {
      if (fputs(p->str, fp) == EOF)
	halt();
      if (putc('\n', fp) == EOF)
	halt();
      index.size += (strlen(p->str) + 1);
    }
  fclose(fp);
  if (write(fdindex, &index, SIZEINDEX) != SIZEINDEX)
    halt();
  close(fdlock);
}

/*---------------------------------------------------------------------------*/

static void send_to_mail_or_news(struct mail *mail, enum e_type dest)
{

  char cmid[1024];
  char command[1024];
  char path[1024];
  char *h;
  FILE *fp;
  struct strlist *p;

  if (dest == NEWS)
    strcpy(command, RNEWS_PROG);
  else
    sprintf(command, SENDMAIL_PROG " -oi -oem -f %s@%s %s@%s",
	    mail->fromuser, mail->fromhost,
	    mail->touser, mail->tohost);
  if (!(fp = popen(command, "w")))
    halt();
  fprintf(fp, "From: %s@%s\n", mail->fromuser, mail->fromhost);
  if (dest == NEWS) {
    fprintf(fp, "Newsgroups: %s%s\n", NEWSGROUPSPREFIX, mail->touser);
    fprintf(fp, "Distribution: %s\n", mail->tohost);
  } else {
    fprintf(fp, "To: %s@%s\n", mail->touser, mail->tohost);
  }
  if (!strcmp(mail->touser, "e")) {
    strcpy(cmid, mail->subject);
    cmid[LEN_BID] = 0;
    strupc(cmid);
    strcat(cmid, MIDSUFFIX);
    fprintf(fp, "Control: cancel <%s>\n", cmid);
    fprintf(fp, "Subject: cmsg cancel <%s>\n", cmid);
  } else {
    fprintf(fp, "Subject: %s\n", mail->subject);
  }
  fprintf(fp, "Date: %s\n", rfc822_date_string(mail->date));
  fprintf(fp, "Message-ID: <%s>\n", mail->mid);
  if (dest == NEWS) {
    *path = 0;
    if (mail->head) {
      for (p = mail->head->next;
	   p && (h = get_host_from_header(p->str));
	   p = p->next) {
	if (*path)
	  strcat(path, "!");
	strcat(path, h);
      }
    }
    if (!*path)
      strcpy(path, "not-for-mail");
    if (level == MBOX &&
	(!strcmp(mail->touser, "e") || !strcmp(mail->touser, "m")))
      strcpy(path, user.name);
    fprintf(fp, "Path: %s\n", path);
  }
  if (mail->lifetime) {
    fprintf(fp, "Lifetime: %d\n", mail->lifetime);
    fprintf(fp, "Expires: %s\n", rfc822_date_string(time(0) + DAYS * mail->lifetime));
  }
  fprintf(fp, "Bulletin-ID: <%s>\n", mail->bid);
  p = mail->head;
  while (p && p->str[0] == 'R' && p->str[1] == ':') {
    fprintf(fp, "X-R: %s\n", p->str + 2);
    p = p->next;
  }
  putc('\n', fp);
  while (p) {
    fputs(p->str, fp);
    putc('\n', fp);
    p = p->next;
  }
  pclose(fp);
}

/*---------------------------------------------------------------------------*/

static struct mail *alloc_mail(void)
{
  struct mail *mail;

  mail = (struct mail *) calloc(1, sizeof(struct mail));
  if (!mail)
    halt();
  return mail;
}

/*---------------------------------------------------------------------------*/

static void free_mail(struct mail *mail)
{
  struct strlist *p;

  while ((p = mail->head)) {
    mail->head = p->next;
    free(p);
  }
  free(mail);
}

/*---------------------------------------------------------------------------*/

static void generate_bid_and_mid(struct mail *mail)
{

  char *cp = 0;
  int fdlock;
  long t1;
  long t;
  static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

  if (!*mail->bid &&
      (cp = strchr(mail->mid, '@')) &&
      !strcmp(cp, MIDSUFFIX)) {
    strcpy(mail->bid, mail->mid);
    mail->bid[strlen(mail->bid) - strlen(MIDSUFFIX)] = 0;
  }

  if (!*mail->bid) {
    if ((fdlock = lock_file(BIDLOCKFILE, 0)) < 0)
      halt();
    t = t1 = time(0);
    cp = mail->bid + 13;
    *--cp = 0;
    *--cp = chars[t1 % 36];
    t1 /= 36;
    *--cp = chars[t1 % 36];
    t1 /= 36;
    *--cp = chars[t1 % 36];
    t1 /= 36;
    *--cp = chars[t1 % 36];
    t1 /= 36;
    *--cp = chars[t1 % 36];
    t1 /= 36;
    *--cp = chars[t1 % 36];
    *--cp = '_';
    *--cp = '_';
    *--cp = '_';
    *--cp = '_';
    *--cp = '_';
    *--cp = '_';
    strncpy(mail->bid, myhostname, strlen(myhostname));
    while (t == time(0))
      sleep(1);
    close(fdlock);
  }

  mail->bid[LEN_BID] = 0;
  strupc(mail->bid);

  if (!*mail->mid) {
    strcpy(mail->mid, mail->bid);
    strcat(mail->mid, MIDSUFFIX);
  }
}

/*---------------------------------------------------------------------------*/

static void append_line(struct mail *mail, const char *line, enum e_where where)
{

  char buf[1024];
  char *t;
  const char *f;
  struct strlist *p;

  /* Remove ^D and ^Z */

  for (t = buf, f = line; *f; f++)
    if (*f != '\004' && *f != '\032')
      *t++ = *f;
  *t = 0;

  /* Clear lines just containing ".", "/EX", or "***END" */

  if (
       !strcmp(buf, "***END") ||
       !strcmp(buf, "***end") ||
       !strcmp(buf, ".") ||
       !strcmp(buf, "/EX") ||
       !strcmp(buf, "/ex")
      )
    *buf = 0;

  p = alloc_string(buf);
  if (!mail->head) {
    mail->head = mail->tail = p;
  } else if (where == HEAD) {
    p->next = mail->head;
    mail->head = p;
  } else {
    mail->tail->next = p;
    mail->tail = p;
  }
}

/*---------------------------------------------------------------------------*/

static void route_mail(struct mail *mail)
{

  char buf[1024];
  char *cp;
  long gmt;
  struct strlist *p;
  struct tm *tm;

  /* Check for bogus mails */

  if ((cp = get_host_from_header(mail->subject)) && callvalid(cp)) {
    free_mail(mail);
    return;
  }

  /* Fix addresses */

  if ((cp = strchr(mail->fromhost, '.')))
    *cp = 0;
  if ((cp = strchr(mail->tohost, '.')))
    *cp = 0;
  if (!*mail->fromhost)
    strcpy(mail->fromhost, myhostname);
  if (!*mail->tohost)
    strcpy(mail->tohost, myhostname);
  strlwc(mail->fromuser);
  strlwc(mail->fromhost);
  strlwc(mail->touser);
  strlwc(mail->tohost);

  /* Set date */

  for (p = mail->head; p; p = p->next) {
    if ((gmt = get_date_from_header(p->str)) == -1)
      break;
    mail->date = gmt;
  }
  if (!mail->date)
    mail->date = time(0);

  /* Set bid and mid */

  generate_bid_and_mid(mail);

  /* Set subject */

  strtrim(mail->subject);
  if (!*mail->subject)
    strcpy(mail->subject, "(no subject)");

  /* Prepend R: line */

  gmt = time(0);
  tm = gmtime(&gmt);
  sprintf(buf, "R:%02d%02d%02d/%02d%02dz @:%s.%s",
	  tm->tm_year % 100,
	  tm->tm_mon + 1,
	  tm->tm_mday,
	  tm->tm_hour,
	  tm->tm_min,
	  myhostname,
	  MYDOMAIN);
  append_line(mail, strupc(buf), HEAD);

  /* Call delivery agents */

  if (callvalid(mail->touser) ||
      (callvalid(mail->tohost) && !calleq(mail->tohost, myhostname))) {
    send_to_mail_or_news(mail, MAIL);
  } else {
    send_to_mail_or_news(mail, NEWS);
    send_to_bbs(mail);
  }

  /* Free mail */

  free_mail(mail);
}

/*---------------------------------------------------------------------------*/

static int get_header_value(const char *name, int do822, const char *line, char *value)
{

  char buf[1024];
  char *cp;
  char *p1;
  char *p2;
  int chr;
  int comment;

  for (; *name; name++, line++)
    if (Xtolower(*name) != Xtolower(*line))
      return 0;

  strcpy(cp = buf, line);

  if (do822) {
    for (comment = 0, p1 = cp; (chr = *p1); p1++) {
      if (chr == '(')
	comment++;
      if (comment)
	*p1 = ' ';
      if (comment && chr == ')')
	comment--;
    }
    while ((p1 = strchr(cp, '<')) && (p2 = strrchr(p1, '>'))) {
      *p2 = 0;
      cp = p1 + 1;
    }
  }

  while (isspace(*cp & 0xff))
    cp++;
  strcpy(value, strtrim(cp));
  return 1;
}

/*---------------------------------------------------------------------------*/

static void delete_command(int argc, char **argv)
{

  int i;
  int mesg;
  struct index index;

  for (i = 1; i < argc; i++)
    if ((mesg = atoi(argv[i])) > 0 && get_index(mesg, &index) && !index.deleted)
      if (level == ROOT                 ||
	  calleq(index.from, user.name) ||
	  calleq(index.to, user.name)) {
	if (remove(getfilename(mesg))) halt();
	index.deleted = 1;
	if (lseek(fdindex, -SIZEINDEX, SEEK_CUR) < 0) halt();
	if (write(fdindex, &index, SIZEINDEX) != SIZEINDEX) halt();
	printf("Message %d deleted.\n", mesg);
      } else
	printf("Message %d not deleted:  Permission denied.\n", mesg);
    else
      printf("No such message: '%s'.\n", argv[i]);
}

/*---------------------------------------------------------------------------*/

static int dir_column;

static void dir_print(struct dir_entry *p)
{
  if (p) {
    dir_print(p->left);
    if (!stopped)
      printf((dir_column++ % 5) < 4 ? "%5d %-8s" : "%5d %s\n", p->count, p->to);
    dir_print(p->right);
    free(p);
  }
}

/*---------------------------------------------------------------------------*/

static void dir_command(int argc, char **argv)
{

  int n, cmp;
  struct dir_entry *head, *curr, *prev;
  struct index *pi, index[1000];

  head = 0;
  cmp = 0;
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  for (; ; ) {
    n = read(fdindex, pi = index, 1000 * SIZEINDEX) / SIZEINDEX;
    if (n < 1) break;
    for (; n; n--, pi++)
      if (read_allowed(pi))
	for (prev = 0, curr = head; ; ) {
	  if (!curr) {
	    curr = (struct dir_entry *) malloc(sizeof(struct dir_entry));
	    if (!curr)
	      halt();
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

static void disconnect_command(int argc, char **argv)
{
  puts("Disconnecting...");
  kill(0, 1);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static struct mail *read_mail_or_news_file(const char *filename, enum e_type src)
{

  char distribution[1024];
  char expires[1024];
  char from[1024];
  char lifetime[1024];
  char line[1024];
  char newsgroups[1024];
  char Rline[1024];
  char *cp;
  FILE *fp;
  int in_header;
  int prefixlen;
  long expiretime;
  struct mail *mail;

  if (!(fp = fopen(filename, "r")))
    halt();

  in_header = 1;
  mail = alloc_mail();
  *distribution = 0;
  *expires = 0;
  *from = 0;
  *lifetime = 0;
  *newsgroups = 0;

  while (fgets(line, sizeof(line), fp)) {
    rip(line);
    if (in_header) {
      if (*line) {
	if (!*from && !strncmp(line, "From ", 5))
	  sscanf(line, "From %s", from);
	if (!*from)
	  get_header_value("From:", 1, line, from);
	if (src == NEWS) {
	  get_header_value("Newsgroups:", 0, line, newsgroups);
	  get_header_value("Distribution:", 0, line, distribution);
	}
	get_header_value("Subject:", 0, line, mail->subject);
	get_header_value("Message-ID:", 1, line, mail->mid);
	get_header_value("Lifetime:", 0, line, lifetime);
	get_header_value("Expires:", 0, line, expires);
	get_header_value("Bulletin-ID:", 1, line, mail->bid);
	if (!*mail->bid)
	  get_header_value("BBS-Message-ID:", 1, line, mail->bid);
	if (get_header_value("X-R:", 0, line, Rline + 2)) {
	  Rline[0] = 'R';
	  Rline[1] = ':';
	  append_line(mail, Rline, TAIL);
	}
      } else {
	in_header = 0;
      }
    } else {
      append_line(mail, line, TAIL);
    }
  }
  fclose(fp);

  split_address(from, mail->fromuser, mail->fromhost);

  if (src == NEWS) {
    prefixlen = strlen(NEWSGROUPSPREFIX);
    while (*newsgroups && strncmp(newsgroups, NEWSGROUPSPREFIX, prefixlen)) {
      if ((cp = strchr(newsgroups, ',')))
	strcpy(newsgroups, cp + 1);
      else
	*newsgroups = 0;
    }
    if (*newsgroups)
      strcpy(newsgroups, newsgroups + prefixlen);
    if ((cp = strchr(newsgroups, ',')))
      *cp = 0;
    if ((cp = strrchr(newsgroups, '.')))
      strcpy(newsgroups, cp + 1);
    strcpy(mail->touser, newsgroups);

    if ((cp = strchr(distribution, ',')))
      *cp = 0;
    if ((cp = strrchr(distribution, '.')))
      strcpy(distribution, cp + 1);
    strcpy(mail->tohost, distribution);
  }

  generate_bid_and_mid(mail);

  if ((expiretime = parse_rfc822_date(expires)) != -1) {
    mail->lifetime = (int) ((expiretime - time(0) + DAYS - 1) / DAYS);
    if (mail->lifetime < 1)
      mail->lifetime = 1;
  }

  if (*lifetime) {
    mail->lifetime = atoi(lifetime);
    if (mail->lifetime < 1)
      mail->lifetime = 1;
  }

  if (!strncmp(mail->subject, "cmsg cancel <", 13)) {
    strcpy(line, mail->subject + 13);
    if ((cp = strstr(line, MIDSUFFIX))) {
      *cp = 0;
      line[LEN_BID] = 0;
      strcpy(mail->subject, strupc(line));
    }
  }

  if (src == NEWS && !*mail->touser) {
    free_mail(mail);
    mail = 0;
  }

  return mail;
}

/*---------------------------------------------------------------------------*/

static void forward_message(struct mail *mail)
{

  char buf[1024];
  int fast_forward;
  struct strlist *p;

  did_forward = 1;
  fast_forward = (!strcmp(mail->touser, "e") ||
		  !strcmp(mail->touser, "m"));
  sprintf(buf, "S %s", mail->touser);
  if (*mail->tohost)
    sprintf(buf + strlen(buf), " @ %s", mail->tohost);
  if (*mail->fromuser)
    sprintf(buf + strlen(buf), " < %s", mail->fromuser);
  if (*mail->bid)
    sprintf(buf + strlen(buf), " $%s", mail->bid);
  if (mail->lifetime)
    sprintf(buf + strlen(buf), " # %d", mail->lifetime);
  if (fast_forward) {
    if (*mail->subject)
      sprintf(buf + strlen(buf), " %s", mail->subject);
    strcat(buf, " \032");
  }
  puts(strupc(buf));
  if (fast_forward)
    *buf = 'n';
  else
    getstring(buf);
  switch (*buf) {
  case 'O':
  case 'o':
    puts(*mail->subject ? mail->subject : "(no subject)");
    for (p = mail->head; p; p = p->next)
      puts(p->str);
    puts("\032");
    /* fall thru */
  case 'N':
  case 'n':
    wait_for_prompt();
    break;
  default:
    exit(1);
  }
}

/*---------------------------------------------------------------------------*/

static void forward_mail(void)
{

  char cfile[1024];
  char dfile[1024];
  char dirname[1024];
  char line[1024];
  char tmp1[1024];
  char tmp2[1024];
  char tmp3[1024];
  char to[1024];
  char xfile[1024];
  DIR *dirp;
  FILE *fp;
  struct dirent *dp;
  struct mail *mail;
  struct strlist *filelist;
  struct strlist *p;
  struct strlist *q;

  sprintf(dirname, UUCP_DIR "/%s", user.name);
  if (!(dirp = opendir(dirname)))
    return;
  filelist = 0;
  for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
    if (*dp->d_name != 'C')
      continue;
    p = alloc_string(dp->d_name);
    if (!filelist || strcmp(p->str, filelist->str) < 0) {
      p->next = filelist;
      filelist = p;
    } else {
      for (q = filelist;
	   q->next && strcmp(p->str, q->next->str) > 0;
	   q = q->next) ;
      p->next = q->next;
      q->next = p;
    }
  }
  closedir(dirp);
  for (; (p = filelist); filelist = p->next, free(p)) {
    sprintf(cfile, "%s/%s/%s", UUCP_DIR, user.name, p->str);
    if (!(fp = fopen(cfile, "r")))
      continue;
    *dfile = *xfile = *to = 0;
    while (fgets(line, sizeof(line), fp)) {
      if (*line == 'E' &&
	  sscanf(line, "%*s %*s %*s %*s %*s %s %*s %*s %*s %s %s",
		 tmp1, tmp2, tmp3) == 3 &&
	  *tmp1 == 'D' &&
	  !strcmp(tmp2, "rmail")) {
	sprintf(dfile, "%s/%s/%s", UUCP_DIR, user.name, tmp1);
	sprintf(to, "%s!%s", user.name, tmp3);
      }
      if (*line == 'S' &&
	  sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2) {
	if (*tmp1 == 'D')
	  sprintf(dfile, "%s/%s/%s", UUCP_DIR, user.name, tmp2);
	if (*tmp1 == 'X')
	  sprintf(xfile, "%s/%s/%s", UUCP_DIR, user.name, tmp2);
      }
    }
    fclose(fp);
    if (!*dfile)
      continue;
    if (*xfile) {
      if (!(fp = fopen(xfile, "r")))
	continue;
      while (fgets(line, sizeof(line), fp)) {
	rip(line);
	if (!strncmp(line, "C rmail ", 8)) {
	  sprintf(to, "%s!%s", user.name, line + 8);
	  break;
	}
      }
      fclose(fp);
    }
    if (!*to)
      continue;
    if ((mail = read_mail_or_news_file(dfile, MAIL))) {
      split_address(to, mail->touser, mail->tohost);
      forward_message(mail);
      free_mail(mail);
    }
    if (remove(cfile))
      halt();
    if (remove(dfile))
      halt();
    if (*xfile && remove(xfile))
      halt();
  }
}

/*---------------------------------------------------------------------------*/

static void forward_news(void)
{

  char batchfile[1024];
  char line[1024];
  char workfile[1024];
  char *cp;
  FILE *fp;
  int i;
  long starttime;
  struct mail *mail;
  struct stat statbuf;
  struct strlist *filelist;
  struct strlist *filetail;
  struct strlist *p;

  sprintf(batchfile, NEWSSPOOL "/out.going/%s", user.name);
  sprintf(workfile, "%s.bbs", batchfile);
  if (!(fp = fopen(workfile, "r"))) {
    if (rename(batchfile, workfile))
      return;
    sprintf(line, CTLINND " -s flush %s", user.name);
    system(line);
    for (i = 0;; i++) {
      if (!stat(batchfile, &statbuf))
	break;
      if (i > 60)
	halt();
      sleep(1);
    }
    if (!(fp = fopen(workfile, "r")))
      halt();
  }
  filelist = filetail = 0;
  while (fgets(line, sizeof(line), fp)) {
    for (cp = line; *cp; cp++) {
      if (isspace(*cp & 0xff)) {
	*cp = 0;
	break;
      }
    }
    if (*line != '/') {
      char tmp[1024];
      sprintf(tmp, NEWSSPOOL "/%s", line);
      strcpy(line, tmp);
    }
    if (!stat(line, &statbuf)) {
      p = alloc_string(line);
      if (!filelist) {
	filelist = filetail = p;
      } else {
	filetail->next = p;
	filetail = p;
      }
    }
  }
  fclose(fp);
  if (!filelist && remove(workfile))
    halt();
  starttime = time(0);
  while (filelist) {
    if (!stat(filelist->str, &statbuf)) {
      if ((mail = read_mail_or_news_file(filelist->str, NEWS))) {
	forward_message(mail);
	free_mail(mail);
      }
    }
    p = filelist;
    filelist = p->next;
    free(p);
    if (filelist) {
      if (!(fp = fopen(workfile, "w")))
	halt();
      for (p = filelist; p; p = p->next) {
	fputs(p->str, fp);
	putc('\n', fp);
      }
      fclose(fp);
    } else {
      if (remove(workfile))
	halt();
    }
    if (starttime + 600 <= time(0)) {
      while ((p = filelist)) {
	filelist = p->next;
	free(p);
      }
      return;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void f_command(int argc, char **argv)
{
  int did_any_forward;

  did_any_forward = 0;
  for (;;) {
    did_forward = 0;
    forward_mail();
    forward_news();
    if (!did_forward)
      break;
    did_any_forward = 1;
  }
  if (!do_forward && !did_any_forward)
    exit(0);
  putchar('F');
}

/*---------------------------------------------------------------------------*/

static void info_command(int argc, char **argv)
{

  FILE *fp;
  int c;

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

static void list_command(int argc, char **argv)
{

  char *at = 0;
  char *bid = 0;
  char *errstr = "The %s option requires an argument.  Type ? LIST for help.\n";
  char *from = 0;
  char *subject = 0;
  char *to = 0;
  int count = 999999;
  int found = 0;
  int i;
  int max = 999999;
  int min = 1;
  int update_seq = 0;
  size_t len;
  struct index index;
  struct tm *tm;

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
  if (lseek(fdindex, -SIZEINDEX, SEEK_END) >= 0) {
    for (; ; ) {
      if (stopped) return;
      if (read(fdindex, &index, SIZEINDEX) != SIZEINDEX) halt();
      if (index.mesg < min) break;
      if (index.mesg <= max                            &&
	  read_allowed(&index)                         &&
	  (!bid      || !strcmp(index.bid, bid))       &&
	  (!from     || !strcmp(index.from, from))     &&
	  (!to       || !strcmp(index.to, to))         &&
	  (!at       || !strcmp(index.at, at))         &&
	  (!subject  || Strcasestr(index.subject, subject))) {
	if (!found) {
	  puts("  Msg#  Size To      @ BBS     From     Date    Subject");
	  found = 1;
	}
	tm = gmtime(&index.date);
	printf("%6d %5ld %-8s%c%-8s %-8s %02d%02d%02d  %.31s\n",
	       index.mesg,
	       index.size,
	       index.to,
	       *index.at ? '@' : ' ',
	       index.at,
	       index.from,
	       tm->tm_year % 100,
	       tm->tm_mon + 1,
	       tm->tm_mday,
	       index.subject);
	if (update_seq && user.seq < index.mesg) {
	  user.seq = index.mesg;
	  put_seq();
	}
	if (--count <= 0) break;
      }
      if (lseek(fdindex, -2L * SIZEINDEX, SEEK_CUR) < 0) break;
    }
  }
  if (!found)
    puts(update_seq ?
	 "No new messages since last LIST NEW command." :
	 "No matching message found.");
}

#undef nextarg

/*---------------------------------------------------------------------------*/

static void mybbs_command(int argc, char **argv)
{
  struct mail *mail;

  if (!callvalid(argv[1])) {
    printf("Invalid call '%s'.\n", argv[1]);
    return;
  }
  mail = alloc_mail();
  strcpy(mail->fromuser, user.name);
  strcpy(mail->touser, "m");
  strcpy(mail->tohost, "thebox");
  sprintf(mail->subject, "%s %ld", argv[1], time(0));
  strupc(mail->subject);
  printf("Setting MYBBS of %s to %s.\n", mail->fromuser, argv[1]);
  route_mail(mail);
}

/*---------------------------------------------------------------------------*/

static void prompt_command(int argc, char **argv)
{
  strcpy(prompt, argv[1]);
}

/*---------------------------------------------------------------------------*/

static void quit_command(int argc, char **argv)
{
  puts("BBS terminated.");
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void read_command(int argc, char **argv)
{

  FILE *fp;
  char *p;
  char buf[1024];
  char path[1024];
  int i;
  int inheader;
  int mesg;
  struct index index;

  for (i = 1; i < argc; i++)
    if ((mesg = atoi(argv[i])) > 0 && get_index(mesg, &index) && read_allowed(&index)) {
      if (!(fp = fopen(getfilename(mesg), "r"))) halt();
      printf("Msg#: %d\n", index.mesg);
      printf("To: %s%s%s\n", index.to, *index.at ? " @ " : "", index.at);
      printf("From: %s\n", index.from);
      printf("Date: %s\n", rfc822_date_string(index.date));
      printf("Subject: %s\n", index.subject);
      printf("Bulletin-ID: %s\n", index.bid);
      *path = 0;
      inheader = 1;
      while (fgets(buf, sizeof(buf), fp)) {
	if (stopped) {
	  fclose(fp);
	  return;
	}
	if (inheader) {
	  if ((p = get_host_from_header(buf))) {
	    strcat(path, *path ? "!" : "Path: ");
	    strcat(path, p);
	    continue;
	  }
	  if (*path) puts(path);
	  putchar('\n');
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

static int collect_message(struct mail *mail, int print_prompt, int check_header)
{

  char line[1024];
  char *p;

  if (print_prompt)
    puts("Enter message: (terminate with ^Z, /EX, or ***END)");
  for (;;) {
    getstring(line);
    if (stopped)
      return 0;
    if (
	 !strcmp(line, "***END") ||
	 !strcmp(line, "***end") ||
	 !strcmp(line, "/EX") ||
	 !strcmp(line, "/ex") ||
	 !strcmp(line, "\032")
	)
      break;
    if (check_header && line[0] == ' ' && line[1] == '[')
      continue;
    append_line(mail, line, TAIL);
    if (check_header) {
      if ((p = get_host_from_header(line)))
	strcpy(mail->fromhost, p);
      else
	check_header = 0;
    }
    if (strchr(line, '\032'))
      break;
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

static void reply_command(int argc, char **argv)
{

  char line[1024];
  char *host;
  char *mesgstr;
  char *p;
  FILE *fp;
  int all;
  int i;
  int mesg;
  struct index index;
  struct mail *mail;

  mesg = all = 0;
  mesgstr = 0;
  for (i = 1; i < argc; i++)
    if (!Strcasecmp(argv[i], "all"))
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
  mail = alloc_mail();
  strcpy(mail->fromuser, user.name);
  if (all) {
    strcpy(mail->touser, index.to);
    strcpy(mail->tohost, index.at);
  } else {
    strcpy(mail->touser, index.from);
    if (!(fp = fopen(getfilename(mesg), "r")))
      halt();
    for (host = 0; fgets(line, sizeof(line), fp); host = p)
      if (!(p = get_host_from_header(line)))
	break;
    fclose(fp);
    strcpy(mail->tohost, host ? host : myhostname);
  }
  for (p = index.subject;;) {
    while (isspace(*p & 0xff))
      p++;
    if (Strncasecmp(p, "Re:", 3))
      break;
    p += 3;
  }
  sprintf(mail->subject, "Re:  %s", p);
  printf("To: %s @ %s\n", mail->touser, mail->tohost);
  printf("Subject: %s\n", mail->subject);
  if (!collect_message(mail, 1, 0)) {
    free_mail(mail);
    return;
  }
  printf("Sending message to %s @ %s.\n", mail->touser, mail->tohost);
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

static void send_command(int argc, char **argv)
{

  char line[1024];
  char *errstr = "The %s option requires an argument.  Type ? SEND for help.\n";
  int got_control_z;
  int i;
  struct mail *mail;

  if ((got_control_z = !strcmp(argv[argc - 1], "\032")))
    argc--;
  mail = alloc_mail();
  for (i = 1; i < argc; i++)
    if (!strcmp("#", argv[i])) {
      nextarg("#");
      mail->lifetime = atoi(argv[i]);
      if (mail->lifetime < 1)
	mail->lifetime = 1;
    } else if (!strcmp("$", argv[i])) {
      nextarg("$");
      strcpy(mail->bid, argv[i]);
    } else if (!strcmp("<", argv[i])) {
      nextarg("<");
      strcpy(mail->fromuser, argv[i]);
    } else if (!strcmp(">", argv[i])) {
      nextarg(">");
      strcpy(mail->touser, argv[i]);
    } else if (!strcmp("@", argv[i])) {
      nextarg("@");
      strcpy(mail->tohost, argv[i]);
    } else if (!*mail->touser) {
      strcpy(mail->touser, argv[i]);
    } else if (!*mail->subject) {
      strcpy(mail->subject, argv[i]);
    } else {
      strcat(mail->subject, " ");
      strcat(mail->subject, argv[i]);
    }
  if (!*mail->touser) {
    errors++;
    puts("No recipient specified.");
    free_mail(mail);
    return;
  }
  if (level == USER && !mail->touser[1]) {
    errors++;
    puts("Invalid recipient specified.");
    free_mail(mail);
    return;
  }
  if (!*mail->tohost)
    strcpy(mail->tohost, myhostname);
  if (!*mail->fromuser || level < MBOX)
    strcpy(mail->fromuser, user.name);
  if (*mail->bid) {
    generate_bid_and_mid(mail);
    if (nntp_channel.fd < 0)
      open_nntp();
    sprintf(line, "stat <%s>\r\n", mail->mid);
    write_nntp(line);
    read_nntp(line);
    if (*line == '2') {
      if (!got_control_z)
	puts("No");
      free_mail(mail);
      return;
    }
  }
  if (!got_control_z) {
    puts((level == MBOX) ? "OK" : "Enter subject:");
    getstring(mail->subject);
    if (stopped) {
      free_mail(mail);
      return;
    }
  }
  strtrim(mail->subject);
  if (!*mail->subject && level != MBOX) {
    errors++;
    puts("No subject specified.");
    free_mail(mail);
    return;
  }
  if (!got_control_z) {
    if (!collect_message(mail, (packetcluster || level != MBOX), 1)) {
      free_mail(mail);
      return;
    }
  }
  if (level != MBOX)
    printf("Sending message to %s @ %s.\n", mail->touser, mail->tohost);
  route_mail(mail);
}

#undef nextarg

/*---------------------------------------------------------------------------*/

static void shell_command(int argc, char **argv)
{

  char command[2048];
  int i;
  int status;
  pid_t pid;

  switch (pid = fork()) {
  case -1:
    puts("Sorry, cannot fork.");
    break;
  case 0:
    setgid(user.gid);
    setuid(user.uid);
    for (i = open_max() - 1; i >= 3; i--) close(i);
    *command = 0;
    for (i = 1; i < argc; i++) {
      if (i > 1) strcat(command, " ");
      strcat(command, argv[i]);
    }
    if (*command)
      execl(user.shell, user.shell, "-c", command, 0);
    else
      execl(user.shell, user.shell, 0);
    _exit(127);
    break;
  default:
    signal(SIGINT,  SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    while (wait(&status) != pid) ;
    signal(SIGINT,  interrupt_handler);
    signal(SIGQUIT, interrupt_handler);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void sid_command(int argc, char **argv)
{
  /*** ignore System IDentifiers (SIDs) for now ***/
}

/*---------------------------------------------------------------------------*/

static void status_command(int argc, char **argv)
{

  int active = 0;
  int deleted = 0;
  int highest = 0;
  int newmsg = 0;
  int n;
  int readable = 0;
  struct index *pi, index[1000];

  printf("DK5SG-BBS  Revision: %s %s\n", revision.number, revision.date);
  if (lseek(fdindex, 0L, SEEK_SET))
    halt();
  for (;;) {
    n = read(fdindex, pi = index, 1000 * SIZEINDEX) / SIZEINDEX;
    if (n < 1)
      break;
    for (; n; n--, pi++) {
      highest = pi->mesg;
      if (pi->deleted) {
	deleted++;
      } else {
	active++;
	if (read_allowed(pi)) {
	  readable++;
	  if (pi->mesg > user.seq)
	    newmsg++;
	}
      }
    }
  }
  printf("%6d  Highest message number\n", highest);
  printf("%6d  Active messages\n", active);
  printf("%6d  Readable messages\n", readable);
  printf("%6d  Deleted messages\n", deleted);
  printf("%6d  Last message listed\n", user.seq);
  printf("%6d  New messages\n", newmsg);
}

/*---------------------------------------------------------------------------*/

static void unknown_command(int argc, char **argv)
{
  errors++;
  printf("Unknown command '%s'.  Type ? for help.\n", *argv);
}

/*---------------------------------------------------------------------------*/

static void xcrunch_command(int argc, char **argv)
{

#define TEMP_INDEXFILE  INDEXFILE ".tmp"

  int f;
  int wflag;
  struct index index;

  if ((f = open(TEMP_INDEXFILE, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0)
    halt();
  if (lseek(fdindex, 0L, SEEK_SET))
    halt();
  wflag = 0;
  while (read(fdindex, &index, SIZEINDEX) == SIZEINDEX) {
    wflag = 1;
    if (!index.deleted) {
      if (write(f, &index, SIZEINDEX) != SIZEINDEX)
	 halt();
      wflag = 0;
    }
  }
  if (wflag)
    if (write(f, &index, SIZEINDEX) != SIZEINDEX)
       halt();
  if (close(f))
    halt();
  if (close(fdindex))
    halt();
  if (rename(TEMP_INDEXFILE, INDEXFILE))
    halt();
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void help_command(int argc, char **argv);

static const struct cmdtable cmdtable[] = {

  { "!",                shell_command,          0,      USER },
  { "?",                help_command,           0,      USER },
  { "BYE",              quit_command,           0,      USER },
  { "DELETE",           delete_command,         2,      USER },
  { "DIR",              dir_command,            0,      USER },
  { "DISCONNECT",       disconnect_command,     0,      USER },
  { "ERASE",            delete_command,         2,      USER },
  { "EXIT",             quit_command,           0,      USER },
  { "F",                f_command,              2,      MBOX },
  { "HELP",             help_command,           0,      USER },
  { "INFO",             info_command,           0,      USER },
  { "KILL",             delete_command,         2,      USER },
  { "LIST",             list_command,           2,      USER },
  { "MYBBS",            mybbs_command,          2,      USER },
  { "PRINT",            read_command,           2,      USER },
  { "PROMPT",           prompt_command,         2,      USER },
  { "QUIT",             quit_command,           0,      USER },
  { "READ",             read_command,           2,      USER },
  { "REPLY",            reply_command,          2,      USER },
  { "RESPOND",          reply_command,          2,      USER },
  { "SB",               send_command,           2,      MBOX },
  { "SEND",             send_command,           2,      USER },
  { "SHELL",            shell_command,          0,      USER },
  { "SP",               send_command,           2,      MBOX },
  { "STATUS",           status_command,         0,      USER },
  { "TYPE",             read_command,           2,      USER },
  { "VERSION",          status_command,         0,      USER },
  { "XCRUNCH",          xcrunch_command,        0,      ROOT },
  { "[",                sid_command,            0,      MBOX },

  { 0,                  unknown_command,        0,      USER }
};

/*---------------------------------------------------------------------------*/

static void help_command(int argc, char **argv)
{

  FILE *fp;
  char line[1024];
  int i;
  int state;
  const struct cmdtable *cmdp;

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
  if (!Strcasecmp(argv[1], "all")) {
    while (!stopped && fgets(line, sizeof(line), fp))
      if (*line != '^') fputs(line, stdout);
  } else {
    state = 0;
    while (!stopped && fgets(line, sizeof(line), fp)) {
      rip(line);
      if (state == 0 && *line == '^' && !Strcasecmp(line + 1, argv[1]))
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

static int connect_addr(const char *host, char *proto, char *addr)
{

  char line[1024];
  char *address;
  char *cp;
  char *mailername;
  char *protocol;
  char *sysname;
  FILE *fp;

  if (!(fp = fopen(CONFIGFILE, "r")))
    return 0;
  while (fgets(line, sizeof(line), fp)) {
    if ((cp = strchr(line, '#')))
      *cp = 0;
    if (!(sysname = strtok(line, ":")))
      continue;
    if (strcmp(sysname, host))
      continue;
    if (!(mailername = strtok(0, ":")))
      continue;
    if (strcmp(mailername, "bbs"))
      continue;
    if (!(protocol = strtok(0, ":")))
      continue;
    if (!(address = strtok(0, ":")))
      continue;
    strtrim(address);
    fclose(fp);
    strcpy(proto, protocol);
    strcpy(addr, address);
    return 1;
  }
  fclose(fp);
  return 0;
}

/*---------------------------------------------------------------------------*/

static void connect_bbs(void)
{

  char address[1024];
  char protocol[1024];
  int addrlen;
  int fd;
  struct sockaddr *addr;

  if (!connect_addr(user.name, protocol, address))
    exit(1);
  if (!(addr = build_sockaddr("unix:/tcp/.sockets/netcmd", &addrlen)))
    exit(1);
  if ((fd = socket(addr->sa_family, SOCK_STREAM, 0)) < 0)
    exit(1);
  if (connect(fd, addr, addrlen))
    exit(1);
  if (fd != 0)
    dup2(fd, 0);
  if (fd != 1)
    dup2(fd, 1);
  if (fd != 2)
    dup2(fd, 2);
  if (fd > 2)
    close(fd);
  fdopen(0, "r+");
  fdopen(1, "r+");
  fdopen(2, "r+");
  printf("connect %s %s\n", protocol, address);
}

/*---------------------------------------------------------------------------*/

static void parse_command_line(char *line)
{

#define STARTDELIM      "#$<["
#define ANYDELIM        "@>"

  char *argv[256];
  char *f;
  char *t;
  char buf[2048];
  int argc;
  int len;
  int quote;
  const struct cmdtable *cmdp;

  argc = 0;
  memset((char *) argv, 0, sizeof(argv));
  for (f = line, t = buf; ; ) {
    while (isspace(*f & 0xff)) f++;
    if (!*f) break;
    argv[argc++] = t;
    if (*f == '"' || *f == '\'') {
      quote = *f++;
      while (*f && *f != quote) *t++ = *f++;
      if (*f) f++;
    } else if (strchr(STARTDELIM ANYDELIM, *f)) {
      *t++ = *f++;
    } else {
      while (*f && !isspace(*f & 0xff) && !strchr(ANYDELIM, *f)) *t++ = *f++;
    }
    *t++ = 0;
  }
  if (!argc) return;
  if (!(len = strlen(*argv))) return;
  for (cmdp = cmdtable; ; cmdp++)
    if (!cmdp->name ||
	(level >= cmdp->level && !Strncasecmp(cmdp->name, *argv, len))) {
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

static void bbs(void)
{

  FILE *fp;
  char line[1024];

  if (level != MBOX)
    printf("DK5SG-BBS  Revision: %s   Type ? for help.\n", revision.number);
  sprintf(line, "%s/%s", user.dir, RCFILE);
  if ((fp = fopen(line, "r"))) {
    while (fgets(line, sizeof(line), fp)) parse_command_line(line);
    fclose(fp);
  }
  if (do_forward) {
    connect_bbs();
    wait_for_prompt();
  }
  if (level == MBOX) printf("[THEBOX-1.8-H$]\n");
  if (do_forward) wait_for_prompt();
  for (; ; ) {
    if (do_forward)
      strcpy(line, "f>");
    else {
      printf("%s", (level == MBOX) ? ">\n" : prompt);
      getstring(line);
    }
    parse_command_line(line);
    do_forward = 0;
    if (stopped) {
      puts("\n*** Interrupt ***");
      stopped = 0;
    }
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char buf[1024];
  char *cp;
  char *sysname = 0;
  int c;
  int err_flag = 0;
  struct passwd *pw;

  if (!*UUCP_DIR ||
      !*SENDMAIL_PROG ||
      !*RNEWS_PROG)
    halt();

  signal(SIGINT,  interrupt_handler);
  signal(SIGQUIT, interrupt_handler);
  signal(SIGALRM, alarm_handler);

  umask(022);

  strcpy(buf, rcsid);           /* HP-UX bug work around */
  sscanf(buf,
	 "%*s %*s %*s %s %s %s %s %s",
	 revision.number,
	 revision.date,
	 revision.time,
	 revision.author,
	 revision.state);

  while ((c = getopt(argc, argv, "f:pw:")) != EOF)
    switch (c) {
    case 'f':
      if (getuid()) {
	puts("The 'f' option is for Store&Forward use only.");
	exit(1);
      }
      sysname = optarg;
      do_forward = 1;
      break;
    case 'p':
      packetcluster = 1;
      break;
    case 'w':
      sleep(atoi(optarg));
      break;
    case '?':
      err_flag = 1;
      break;
    }
  if (optind < argc) err_flag = 1;
  if (err_flag) {
    puts("usage: bbs [-w seconds] [-f system] [-p]");
    exit(1);
  }

  mkdir(WRKDIR, 0755);

  if (gethostname(buf, sizeof(buf))) halt();
  if ((cp = strchr(buf, '.'))) *cp = 0;
  if (!(myhostname = strdup(buf))) halt();

  pw = do_forward ? getpwnam(sysname) : getpwuid(getuid());
  if (!pw) halt();
  if (!(user.name = strdup(pw->pw_name))) halt();
  user.uid = (int) pw->pw_uid;
  user.gid = (int) pw->pw_gid;
  if (!(user.dir = strdup(pw->pw_dir))) halt();
  if (!(user.shell = strdup(pw->pw_shell))) halt();
  endpwent();
  if (!user.uid) level = ROOT;
  if (connect_addr(user.name, buf, buf)) level = MBOX;

  if (!getenv("LOGNAME")) {
    sprintf(buf, "LOGNAME=%s", user.name);
    if (!(cp = strdup(buf))) halt();
    putenv(cp);
  }
  if (!getenv("HOME")) {
    sprintf(buf, "HOME=%s", user.dir);
    if (!(cp = strdup(buf))) halt();
    putenv(cp);
  }
  if (!getenv("SHELL")) {
    sprintf(buf, "SHELL=%s", user.shell);
    if (!(cp = strdup(buf))) halt();
    putenv(cp);
  }
  if (!getenv("PATH"))
    putenv("PATH=/bin:/usr/bin:/usr/contrib/bin:/usr/local/bin");
  if (!getenv("TZ"))
    putenv("TZ=MEZ-1MESZ");

  get_seq();

  if ((fdindex = open(INDEXFILE, O_RDWR | O_CREAT, 0644)) < 0) halt();

  bbs();

  return 0;
}
