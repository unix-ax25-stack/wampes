static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 2.78 1994/09/05 12:47:30 deyke Exp $";

/* Bulletin Board System */

#include <sys/types.h>

#include <stdio.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ndbm.h>
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

#define SEEK_SET L_SET
#define SEEK_CUR L_INCR
#define SEEK_END L_XTND

#define dirent direct

#else

#include <dirent.h>
#include <sys/time.h>
#include <termios.h>

#endif

extern char *optarg;
extern int optind;

#include "bbs.h"
#include "buildsaddr.h"
#include "callvalid.h"
#include "configure.h"
#include "lockfile.h"
#include "seteugid.h"
#include "strdup.h"

#define SECONDS     (1L)
#define MINUTES     (60L*SECONDS)
#define HOURS       (60L*MINUTES)
#define DAYS        (24L*HOURS)

#define USER        0
#define MBOX        1
#define ROOT        2

#define BBS         0
#define MAILorNEWS  1

struct revision {
  char number[16];
  char date[16];
  char time[16];
  char author[16];
  char state[16];
};

struct user {
  char *name;
  uid_t uid;
  gid_t gid;
  char *dir;
  char *shell;
  char *cwd;
  int seq;
};

struct strlist {
  struct strlist *next;
  char str[1];
};

struct mail {
  char from[1024];
  char to[1024];
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
  char to[LEN_TO+1];
};

static char *MYHOSTNAME;
static char *myhostname;
static char prompt[1024] = "bbs> ";
static const char daynames[] = "SunMonTueWedThuFriSat";
static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static int debug;
static int doforward;
static int errors;
static int fdindex;
static int fdseq;
static int level;
static int mode = BBS;
static int packetcluster;
static struct revision revision;
static struct user user;
static volatile int stopped;

/* In bbs.c: */
static void errorstop(int line);
static int Xtolower(int c);
static int Xtoupper(int c);
static char *strupc(char *s);
static char *strlwc(char *s);
static int Strcasecmp(const char *s1, const char *s2);
static int Strncasecmp(const char *s1, const char *s2, int n);
static char *strtrim(char *s);
static const char *strcasepos(const char *str, const char *pat);
static char *getstring(char *s);
static char *timestr(long gmt);
static char *rfc822_date(long gmt);
static long parse_date(const char *str);
static void make_parent_directories(const char *filename);
static char *getfilename(int mesg);
static void get_seq(void);
static void put_seq(void);
static void wait_for_prompt(void);
static int get_index(int n, struct index *index);
static int read_allowed(const struct index *index);
static char *get_user_from_path(char *path);
static char *get_host_from_path(char *path);
static int msg_uniq(const char *bid, const char *mid);
static void send_to_bbs(struct mail *mail);
static void send_to_mail(struct mail *mail);
static void send_to_news(struct mail *mail);
static void fix_address(char *addr);
static void remove_message_delimiters(char *line);
static struct mail *alloc_mail(void);
static void free_mail(struct mail *mail);
static void route_mail(struct mail *mail);
static void append_line(struct mail *mail, const char *line);
static int get_header_value(const char *name, int do822, char *line, char *value);
static char *get_host_from_header(const char *line);
static int host_in_header(const char *fname, const char *host);
static void delete_command(int argc, char **argv);
static void dir_print(struct dir_entry *p);
static void dir_command(int argc, char **argv);
static void disconnect_command(int argc, char **argv);
static void forward_message(const struct index *index, const char *filename, int skip_header);
static void f_command(int argc, char **argv);
static void help_command(int argc, char **argv);
static void info_command(int argc, char **argv);
static void list_command(int argc, char **argv);
static void mybbs_command(int argc, char **argv);
static void prompt_command(int argc, char **argv);
static void quit_command(int argc, char **argv);
static void read_command(int argc, char **argv);
static void reply_command(int argc, char **argv);
static void send_command(int argc, char **argv);
static void shell_command(int argc, char **argv);
static void sid_command(int argc, char **argv);
static void status_command(int argc, char **argv);
static void unknown_command(int argc, char **argv);
static void xcrunch_command(int argc, char **argv);
static void xscreen_command(int argc, char **argv);
static char *connect_addr(const char *host);
static void connect_bbs(void);
static void parse_command_line(char *line);
static void bbs(void);
static void interrupt_handler(int sig);
static void alarm_handler(int sig);
static void recv_from_mail_or_news(void);
int main(int argc, char **argv);

/*---------------------------------------------------------------------------*/

static const struct cmdtable {
  const char *name;
  void (*fnc)(int argc, char **argv);
  int argc;
  int level;
} cmdtable[] = {

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

  0,            unknown_command,        0,      USER
};

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

#if defined _SC_OPEN_MAX && !(defined __386BSD__ || defined __bsdi__)
#define open_max()      sysconf(_SC_OPEN_MAX)
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

static char *strtrim(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(*p & 0xff)) ;
  p[1] = 0;
  return s;
}

/*---------------------------------------------------------------------------*/

static const char *strcasepos(const char *str, const char *pat)
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

static char *getstring(char *s)
{

  char *p;
  static int chr, lastchr;

  fflush(stdout);
  alarm(1 * HOURS);
  for (p = s; ; ) {
    *p = 0;
    lastchr = chr;
    chr = getchar();
    if (stopped) {
      alarm(0);
      clearerr(stdin);
      *s = 0;
      return s;
    }
    if (ferror(stdin) || feof(stdin)) {
      alarm(0);
      return 0;
    }
    switch (chr) {
    case EOF:
      alarm(0);
      return (p == s) ? 0 : s;
    case 0:
      break;
    case '\r':
      alarm(0);
      return s;
    case '\n':
      if (lastchr != '\r') {
	alarm(0);
	return s;
      }
      break;
    default:
      *p++ = chr;
    }
  }
}

/*---------------------------------------------------------------------------*/

static char *timestr(long gmt)
{
  static char buf[20];
  struct tm *tm;

  tm = gmtime(&gmt);
  sprintf(buf, "%02d%.3s%02d/%02d%02d",
	       tm->tm_mday,
	       monthnames + 3 * tm->tm_mon,
	       tm->tm_year % 100,
	       tm->tm_hour,
	       tm->tm_min);
  return buf;
}

/*---------------------------------------------------------------------------*/

static char *rfc822_date(long gmt)
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

static long parse_date(const char *str)
{

  static int mdays[] = {
    31, 0, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
  };

  char *p;
  char monthstr[4];
  int dd;
  int h;
  int i;
  int m;
  int mm;
  int s;
  int yy;
  long jdate;

  if (sscanf((char *) str,
	     "%*s %d %3s %d %d:%d:%d",
	     &dd, monthstr, &yy, &h, &m, &s) != 6) return -1;
  if (yy <= 37) yy += 2000;
  if (yy <= 99) yy += 1900;
  if (strlen(monthstr) != 3) return -1;
  p = strstr(monthnames, monthstr);
  if (!p) return -1;
  mm = (p - monthnames) / 3;
  mdays[1] = 28 + (yy % 4 == 0 && (yy % 100 || yy % 400 == 0));
  if (yy < 1970 || yy > 2037 || dd < 1 || dd > mdays[mm]) return -1;
  jdate = dd - 1;
  for (i = 0; i < mm; i++) jdate += mdays[i];
  for (i = 1970; i < yy; i++) jdate += 365 + (i % 4 == 0);
  jdate *= (24L * 60L * 60L);
  return jdate + 3600 * h + 60 * m + s;
}

/*---------------------------------------------------------------------------*/

#define calleq(c1, c2) (!Strcasecmp((c1), (c2)))

/*---------------------------------------------------------------------------*/

static void make_parent_directories(const char *filename)
{
  char *p, dirname[1024];

  strcpy(dirname, filename);
  p = strrchr(dirname, '/');
  if (!p) halt();
  *p = 0;
  if (!mkdir(dirname, 0755)) return;
  if (errno != ENOENT) halt();
  make_parent_directories(dirname);
  if (mkdir(dirname, 0755)) halt();
}

/*---------------------------------------------------------------------------*/

static char *getfilename(int mesg)
{
  static char buf[12];

  sprintf(buf,
	  "%02x/%02x/%02x/%02x",
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

  if (mode != BBS) return;
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

  if (mode != BBS || debug) return;
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
    if (!getstring(buf)) exit(1);
    l = strlen(buf);
  } while (!l || buf[l-1] != '>');
}

/*---------------------------------------------------------------------------*/

static int get_index(int n, struct index *index)
{

  int i1, i2, im;
  long pos;

  i1 = 0;
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (read(fdindex, index, sizeof(struct index)) != sizeof(struct index)) return 0;
  if (n == index->mesg) return 1;
  if (n < index->mesg) return 0;

  if ((pos = lseek(fdindex, -sizeof(struct index), SEEK_END)) < 0) halt();
  i2 = (int) (pos / sizeof(struct index));
  if (read(fdindex, index, sizeof(struct index)) != sizeof(struct index)) halt();
  if (n == index->mesg) return 1;
  if (n > index->mesg) return 0;

  while (i1 + 1 < i2) {
    im = (i1 + i2) / 2;
    if (lseek(fdindex, im * sizeof(struct index), SEEK_SET) < 0) halt();
    if (read(fdindex, index, sizeof(struct index)) != sizeof(struct index)) halt();
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

static char *get_user_from_path(char *path)
{
  char *cp;

  cp = strrchr(path, '!');
  return cp ? (cp + 1) : path;
}

/*---------------------------------------------------------------------------*/

static char *get_host_from_path(char *path)
{

  char *cp;
  static char tmp[1024];

  strcpy(tmp, path);
  cp = strrchr(tmp, '!');
  if (!cp) return myhostname;
  *cp = 0;
  cp = strrchr(tmp, '!');
  return cp ? (cp + 1) : tmp;
}

/*---------------------------------------------------------------------------*/

static int msg_uniq(const char *bid, const char *mid)
{

  DBM * bid_db;
  datum datum_bid;
  datum datum_offset;
  long offset;
  struct index index;

  datum_bid.dptr = (char *) bid;
  datum_bid.dsize = strlen(bid);
  if (!(bid_db = dbm_open(BID_DB, O_RDWR | O_CREAT, 0644))) halt();
  datum_offset = dbm_fetch(bid_db, datum_bid);
  dbm_close(bid_db);
  if (!datum_offset.dptr) return 1;
  memcpy((char *) &offset, datum_offset.dptr, sizeof(offset));
  if (lseek(fdindex, offset, SEEK_SET) != offset) halt();
  if (read(fdindex, &index, sizeof(struct index )) != sizeof(struct index )) halt();
  if (strcmp(index.bid, bid)) halt();
  return index.date < time((long *) 0) - 90 * DAYS;
}

/*---------------------------------------------------------------------------*/

static void send_to_bbs(struct mail *mail)
{

  DBM *bid_db;
  FILE *fp;
  datum datum_bid;
  datum datum_offset;
  int fdlock;
  long offset;
  struct index index;
  struct strlist *p;

  if (!*mail->subject) return;
  if ((fdlock = lock_file(LOCKFILE, 0)) < 0) halt();
  if (msg_uniq(mail->bid, mail->mid)) {
    if (lseek(fdindex, -sizeof(struct index), SEEK_END) < 0)
      index.mesg = 1;
    else {
      if (read(fdindex, &index, sizeof(struct index)) != sizeof(struct index)) halt();
      index.mesg++;
    }
    index.date = mail->date;
    index.lifetime_h = (mail->lifetime + 1) >> 8;
    index.lifetime_l = (mail->lifetime + 1);
    strcpy(index.bid, mail->bid);
    strncpy(index.subject, mail->subject, LEN_SUBJECT);
    index.subject[LEN_SUBJECT] = 0;
    strncpy(index.to, get_user_from_path(mail->to), LEN_TO);
    index.to[LEN_TO] = 0;
    strupc(index.to);
    strncpy(index.at, get_host_from_path(mail->to), LEN_AT);
    index.at[LEN_AT] = 0;
    strupc(index.at);
    strncpy(index.from, get_user_from_path(mail->from), LEN_FROM);
    index.from[LEN_FROM] = 0;
    strupc(index.from);
    index.deleted = 0;
    index.size = 0;
    if (!(fp = fopen(getfilename(index.mesg), "w"))) {
      make_parent_directories(getfilename(index.mesg));
      if (!(fp = fopen(getfilename(index.mesg), "w"))) halt();
    }
    if (strcmp(index.to, "E") && strcmp(index.to, "M"))
      for (p = mail->head; p; p = p->next) {
	if (fputs(p->str, fp) == EOF) halt();
	if (putc('\n', fp) == EOF) halt();
	index.size += (strlen(p->str) + 1);
      }
    fclose(fp);
    offset = lseek(fdindex, 0L, SEEK_END);
    if (offset < 0) halt();
    if (!(bid_db = dbm_open(BID_DB, O_RDWR | O_CREAT, 0644))) halt();
    datum_bid.dptr = index.bid;
    datum_bid.dsize = strlen(index.bid);
    datum_offset.dptr = (char *) &offset;
    datum_offset.dsize = sizeof(offset);
    if (dbm_store(bid_db, datum_bid, datum_offset, DBM_REPLACE) < 0) halt();
    dbm_close(bid_db);
    if (write(fdindex, &index, sizeof(struct index)) != sizeof(struct index)) halt();
    if (index.mesg == user.seq + 1) {
      user.seq = index.mesg;
      put_seq();
    }
  }
  close(fdlock);
}

/*---------------------------------------------------------------------------*/

static void send_to_mail(struct mail *mail)
{

  FILE *fp;
  char command[1024];
  int i;
  struct strlist *p;

  switch (fork()) {
  case -1:
    halt();
  case 0:
    setgid(0);
    setuid(0);
    for (i = open_max() - 1; i >= 0; i--) close(i);
    setsid();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    switch (fork()) {
    case -1:
      _exit(1);
    case 0:
      sprintf(command, "%s -oi -oem -f %s %s", SENDMAIL_PROG, mail->from, mail->to);
      if (!(fp = popen(command, "w"))) _exit(1);
      fprintf(fp, "From: %s\n", mail->from);
      fprintf(fp, "To: %s\n", mail->to);
      fprintf(fp, "Date: %s\n", rfc822_date(mail->date));
      if (*mail->subject) fprintf(fp, "Subject: %s\n", mail->subject);
      fprintf(fp, "Message-ID: <%s>\n", mail->mid);
      if (mail->lifetime != -1)
	fprintf(fp, "Expires: %s\n", rfc822_date(mail->date + DAYS * mail->lifetime));
      fprintf(fp, "Bulletin-ID: <%s>\n", mail->bid);
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

static void send_to_news(struct mail *mail)
{

  FILE *fp;
  char *fromhost;
  char *pp;
  int i;
  struct strlist *p;

  if (!*mail->subject) return;

  if (!*RNEWS_PROG) return;
  switch (fork()) {
  case -1:
    halt();
  case 0:
    setgid(0);
    setuid(0);
    for (i = open_max() - 1; i >= 0; i--) close(i);
    setsid();
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    fopen("/dev/null", "r+");
    switch (fork()) {
    case -1:
      _exit(1);
    case 0:
      if (!(fp = popen(RNEWS_PROG, "w"))) _exit(1);
      fromhost = get_host_from_path(mail->from);
      fprintf(fp, "From: %s@%s%s\n", get_user_from_path(mail->from), fromhost, strchr(fromhost, '.') ? "" : ".ampr.org");
      fprintf(fp, "Date: %s\n", rfc822_date(mail->date));
      if (debug)
	fprintf(fp, "Newsgroups: %s.test\n", myhostname);
      else
	fprintf(fp, "Newsgroups: ampr.bbs.%s\n", get_user_from_path(mail->to));
      fprintf(fp, "Subject: %s\n", mail->subject);
      fprintf(fp, "Message-ID: <%s>\n", mail->mid);
      i = strlen(myhostname);
      if (!strncmp(mail->from, myhostname, i) && mail->from[i] == '!')
	pp = mail->from + i + 1;
      else
	pp = mail->from;
      fprintf(fp, "Path: %s\n", pp);
      if (mail->lifetime != -1)
	fprintf(fp, "Expires: %s\n", rfc822_date(mail->date + DAYS * mail->lifetime));
      fprintf(fp, "Distribution: %s\n", get_host_from_path(mail->to));
      fprintf(fp, "Bulletin-ID: <%s>\n", mail->bid);
      putc('\n', fp);
      p = mail->head;
      while (p && p->str[0] == 'R' && p->str[1] == ':')
	p = p->next;
      while (p &&
	  ((p->str[0] == 'd' && p->str[1] == 'e' && p->str[2] == ' ') ||
	   (p->str[0] == 't' && p->str[1] == 'o' && p->str[2] == ' ')))
	p = p->next;
      while (p && p->str[0] == 0)
	p = p->next;
      while (p) {
	fputs(p->str, fp);
	putc('\n', fp);
	p = p->next;
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

static void fix_address(char *addr)
{

  char *p1, *p2;
  char tmp[1024];
  int c, skip;

  for (p1 = addr; *p1; p1++)
    switch (*p1) {
    case '%':
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
    *p1 = *p2 = 0;
    sprintf(tmp, "%s%s!%s", addr, p1 + 1, p2 + 1);
    strcpy(addr, tmp);
  }

  while ((p2 = strrchr(addr, '@'))) {
    *p2 = 0;
    p1 = p2;
    while (p1 > addr && *p1 != '!') p1--;
    if (p1 == addr)
      sprintf(tmp, "%s!%s", p2 + 1, addr);
    else {
      *p1 = 0;
      sprintf(tmp, "%s!%s!%s", addr, p2 + 1, p1 + 1);
    }
    strcpy(addr, tmp);
  }

  for (skip = 0, p1 = p2 = addr; (c = *p1); p1++) {
    if (c == '.')
      skip = 1;
    else if (c == '!')
      skip = 0;
    if (!skip) *p2++ = c;
  }
  *p2 = 0;

  if (!strchr(addr, '!')) {
    sprintf(tmp, "%s!%s", myhostname, addr);
    strcpy(addr, tmp);
  }

  strlwc(addr);
}

/*---------------------------------------------------------------------------*/

static void remove_message_delimiters(char *line)
{

  char *f;
  char *t;

  if (*line == '.' && !line[1]) *line = 0;
  for (t = f = line; *f; f++)
    if (*f != '\004' && *f != '\032') *t++ = *f;
  *t = 0;
  if (!Strncasecmp(line, "***end", 6)) *line = ' ';
}

/*---------------------------------------------------------------------------*/

static struct mail *alloc_mail(void)
{
  struct mail *mail;

  mail = (struct mail *) calloc(1, sizeof(*mail));
  mail->lifetime = -1;
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

static void route_mail(struct mail *mail)
{

#define MidSuffix "@bbs.net"

  char *cp;
  char *tohost;
  char *touser;
  char buf[16];
  int fdbid;
  int n;
  struct strlist *p;

  /* Set date */

  mail->date = time((long *) 0);

  /* Fix addresses */

  fix_address(mail->from);
  fix_address(mail->to);

  /* Check for bogus mails */

  strtrim(mail->subject);
  if ((cp = get_host_from_header(mail->subject)) && callvalid(cp)) goto Done;
  if (level == MBOX && !packetcluster) {
    cp = get_user_from_path(mail->to);
    if (!cp) goto Done;
    if (strlen(cp) > 1) {
      if (!mail->head) goto Done;
      cp = get_host_from_header(mail->head->str);
      if (!cp || !calleq(cp, user.name)) goto Done;
    }
  }

  /* Set bid */

  if (!*mail->bid && (cp = strchr(mail->mid, '@')) && !strcmp(cp, MidSuffix)) {
    strcpy(mail->bid, mail->mid);
    mail->bid[strlen(mail->bid)-strlen(MidSuffix)] = 0;
  }
  if (!*mail->bid) {
    if ((fdbid = lock_file(BIDFILE, 0)) < 0) halt();
    n = (read(fdbid, buf, sizeof(buf)) >= 2) ? atoi(buf) : 0;
    n++;
    sprintf(buf, "%d\n", n);
    if (lseek(fdbid, 0L, SEEK_SET)) halt();
    if (write(fdbid, buf, strlen(buf)) != strlen(buf)) halt();
    close(fdbid);
    sprintf(mail->bid, "%012d", n);
    strncpy(mail->bid, myhostname, strlen(myhostname));
  }
  mail->bid[LEN_BID] = 0;
  strupc(mail->bid);

  /* Set mid */

  if (!*mail->mid) {
    strcpy(mail->mid, mail->bid);
    strcat(mail->mid, MidSuffix);
  }

  /* Remove message delimiters */

  for (p = mail->head; p; p = p->next)
    remove_message_delimiters(p->str);

  /* Call delivery agents */

  touser = get_user_from_path(mail->to);
  tohost = get_host_from_path(mail->to);
  if (callvalid(touser)) {
    send_to_mail(mail);
  } else {
    if (calleq(tohost, myhostname))
      send_to_bbs(mail);
    else if (callvalid(tohost))
      send_to_mail(mail);
    else
      send_to_bbs(mail);
    if (strcmp(touser, "e") && strcmp(touser, "m")) send_to_news(mail);
  }

  /* Free mail */

Done:
  free_mail(mail);

}

/*---------------------------------------------------------------------------*/

static void append_line(struct mail *mail, const char *line)
{
  struct strlist *p;

  p = (struct strlist *) malloc(sizeof(*p) + strlen(line));
  p->next = 0;
  strcpy(p->str, line);
  if (!mail->head)
    mail->head = p;
  else
    mail->tail->next = p;
  mail->tail = p;
}

/*---------------------------------------------------------------------------*/

static int get_header_value(const char *name, int do822, char *line, char *value)
{

  char *p1, *p2;
  int c, comment;

  for (; *name; name++, line++)
    if (Xtolower(*name) != Xtolower(*line)) return 0;

  if (do822) {
    for (comment = 0, p1 = line; (c = *p1); p1++) {
      if (c == '(') comment++;
      if (comment) *p1 = ' ';
      if (comment && c == ')') comment--;
    }
    while ((p1 = strchr(line, '<')) && (p2 = strrchr(p1, '>'))) {
      *p2 = 0;
      line = p1 + 1;
    }
  }

  while (isspace(*line & 0xff)) line++;
  strcpy(value, strtrim(line));
  return 1;
}

/*---------------------------------------------------------------------------*/

static char *get_host_from_header(const char *line)
{

  char *p, *q;
  static char buf[1024];

  if (*line == 'R' && line[1] == ':' && (p = strchr(strcpy(buf, line), '@'))) {
    p++;
    while (*p == ':' || isspace(*p & 0xff)) p++;
    for (q = p; isalnum(*q & 0xff); q++) ;
    *q = 0;
    return p;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static int host_in_header(const char *fname, const char *host)
{

  FILE *fp;
  char buf[1024];
  char *p;

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
	if (unlink(getfilename(mesg))) halt();
	index.deleted = 1;
	if (lseek(fdindex, -sizeof(struct index), SEEK_CUR) < 0) halt();
	if (write(fdindex, &index, sizeof(struct index)) != sizeof(struct index)) halt();
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
    n = read(fdindex, pi = index, 1000 * sizeof(struct index)) / sizeof(struct index);
    if (n < 1) break;
    for (; n; n--, pi++)
      if (read_allowed(pi))
	for (prev = 0, curr = head; ; ) {
	  if (!curr) {
	    curr = (struct dir_entry *) malloc(sizeof(*curr));
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

static void forward_message(const struct index *index, const char *filename, int skip_header)
{

  FILE * fp;
  char *cp;
  char buf[1024];
  int lifetime;
  struct tm *tm;

  lifetime = ((index->lifetime_h << 8) & 0xff00) + (index->lifetime_l & 0xff) - 1;
  if (lifetime != -1)
    printf("S %s%s%s < %s%s%s # %d\n",
	   index->to,
	   *index->at ? " @ " : "",
	   index->at,
	   index->from,
	   *index->bid ? " $" : "",
	   index->bid,
	   lifetime);
  else
    printf("S %s%s%s < %s%s%s\n",
	   index->to,
	   *index->at ? " @ " : "",
	   index->at,
	   index->from,
	   *index->bid ? " $" : "",
	   index->bid);
  if (!getstring(buf)) exit(1);
  switch (*buf) {
  case 'O':
  case 'o':
    puts(*index->subject ? index->subject : "no subject");
    sleep(5); /* Bugfix for TheBox 1.9 */
    if (!strcmp(index->to, "E") || !strcmp(index->to, "M"))
      putchar('\n');
    else {
      tm = gmtime(&index->date);
      printf("R:%02d%02d%02d/%02d%02dz @:%s.%s\n",
	     tm->tm_year % 100,
	     tm->tm_mon + 1,
	     tm->tm_mday,
	     tm->tm_hour,
	     tm->tm_min,
	     MYHOSTNAME,
	     MYDOMAIN);
      if (!(fp = fopen(filename, "r"))) halt();
      if (skip_header)
	while (fgets(buf, sizeof(buf), fp) && *buf != '\n') ;
      while (fgets(buf, sizeof(buf), fp)) {
	if ((cp = strchr(buf, '\n'))) *cp = 0;
	remove_message_delimiters(buf);
	puts(buf);
      }
      fclose(fp);
    }
    sleep(5); /* Bugfix for TheBox 1.9 */
    puts("\032");
    wait_for_prompt();
    break;
  case 'N':
  case 'n':
    wait_for_prompt();
    break;
  default:
    exit(1);
  }
}

/*---------------------------------------------------------------------------*/

static void f_command(int argc, char **argv)
{

  struct filelist {
    struct filelist *next;
    char name[16];
  };

  DIR *dirp;
  FILE * fp;
  char bid[1024];
  char cfile[1024];
  char dfile[1024];
  char dirname[1024];
  char expire[1024];
  char from[1024];
  char line[1024];
  char subject[1024];
  char tmp1[1024];
  char tmp2[1024];
  char tmp3[1024];
  char to[1024];
  char xfile[1024];
  int do_not_exit;
  int lifetime;
  long expiretime;
  struct dirent *dp;
  struct filelist *filelist = 0;
  struct filelist *p;
  struct filelist *q;
  struct index index;
  struct stat statbuf;

  do_not_exit = doforward;

  sprintf(dirname, "%s/%s", UUCP_DIR, user.name);
  if ((dirp = opendir(dirname))) {
    for (dp = readdir(dirp); dp; dp = readdir(dirp)) {
      if (*dp->d_name != 'C') continue;
      p = (struct filelist *) malloc(sizeof(*p));
      strcpy(p->name, dp->d_name);
      if (!filelist || strcmp(p->name, filelist->name) < 0) {
	p->next = filelist;
	filelist = p;
      } else {
	for (q = filelist; q->next && strcmp(p->name, q->next->name) > 0; q = q->next) ;
	p->next = q->next;
	q->next = p;
      }
    }
    closedir(dirp);
    for (; (p = filelist); filelist = p->next, free(p)) {
      sprintf(cfile, "%s/%s/%s", UUCP_DIR, user.name, p->name);
      if (!(fp = fopen(cfile, "r"))) continue;
      *dfile = *xfile = *to = 0;
      while (fgets(line, sizeof(line), fp)) {
	if (*line == 'E' && sscanf(line, "%*s %*s %*s %*s %*s %s %*s %*s %*s %s %s", tmp1, tmp2, tmp3) == 3 && *tmp1 == 'D' && !strcmp(tmp2, "rmail")) {
	  sprintf(dfile, "%s/%s/%s", UUCP_DIR, user.name, tmp1);
	  sprintf(to, "%s!%s", user.name, tmp3);
	  strtrim(to);
	}
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'D')
	  sprintf(dfile, "%s/%s/%s", UUCP_DIR, user.name, tmp2);
	if (*line == 'S' && sscanf(line, "%*s %*s %s %*s %*s %s", tmp1, tmp2) == 2 && *tmp1 == 'X')
	  sprintf(xfile, "%s/%s/%s", UUCP_DIR, user.name, tmp2);
      }
      fclose(fp);
      if (!*dfile) continue;
      if (*xfile) {
	if (!(fp = fopen(xfile, "r"))) continue;
	while (fgets(line, sizeof(line), fp))
	  if (!strncmp(line, "C rmail ", 8)) {
	    sprintf(to, "%s!%s", user.name, line + 8);
	    strtrim(to);
	    break;
	  }
	fclose(fp);
      }
      if (!*to) continue;
      if (!(fp = fopen(dfile, "r"))) continue;
      *from = *subject = *bid = *expire = 0;
      if (fscanf(fp, "From %s", tmp1) == 1) {
	if (!strcmp(tmp1, "MAILER-DAEMON") || tmp1[strlen(tmp1) - 1] == '!')
	  strcpy(tmp1, myhostname);
	sprintf(from, "%s!%s", myhostname, tmp1);
	strtrim(from);
	while (fgets(line, sizeof(line), fp)) {
	  if (*line == '\n') break;
	  get_header_value("Subject:", 0, line, subject);
	  get_header_value("Expires:", 1, line, expire);
	  get_header_value("Bulletin-ID:", 1, line, bid);
	}
      }
      fclose(fp);
      if (!*from) continue;
      lifetime = -1;
      if ((expiretime = parse_date(expire)) != -1) {
	lifetime = (expiretime - time((long *) 0) + DAYS - 1) / DAYS;
	if (lifetime < 1) lifetime = 1;
      }
      if (stat(cfile, &statbuf)) continue;
      memset((char *) &index, 0, sizeof(index));
      index.date = statbuf.st_mtime;
      index.lifetime_h = (lifetime + 1) >> 8;
      index.lifetime_l = (lifetime + 1);
      strncpy(index.bid, bid, LEN_BID);
      index.bid[LEN_BID] = 0;
      strupc(index.bid);
      strncpy(index.subject, subject, LEN_SUBJECT);
      index.subject[LEN_SUBJECT] = 0;
      strncpy(index.to, get_user_from_path(to), LEN_TO);
      index.to[LEN_TO] = 0;
      strupc(index.to);
      strncpy(index.at, get_host_from_path(to), LEN_AT);
      index.at[LEN_AT] = 0;
      strupc(index.at);
      strncpy(index.from, get_user_from_path(from), LEN_FROM);
      index.from[LEN_FROM] = 0;
      strupc(index.from);
      forward_message(&index, dfile, 1);
      if (unlink(cfile)) halt();
      if (unlink(dfile)) halt();
      if (*xfile && unlink(xfile)) halt();
      do_not_exit = 1;
    }
  }

  if (!get_index(user.seq, &index))
    if (lseek(fdindex, 0L, SEEK_SET)) halt();
  while (read(fdindex, &index, sizeof(struct index)) == sizeof(struct index)) {
    if (!index.deleted                &&
	index.mesg > user.seq         &&
	!calleq(index.at, myhostname) &&
	!host_in_header(getfilename(index.mesg), user.name)) {
      forward_message(&index, getfilename(index.mesg), 0);
      do_not_exit = 1;
    }
    if (user.seq < index.mesg) {
      user.seq = index.mesg;
      put_seq();
    }
  }

  if (!do_not_exit) exit(0);
  putchar('F');
}

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
      strtrim(line);
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
  if (lseek(fdindex, -sizeof(struct index), SEEK_END) >= 0) {
    for (; ; ) {
      if (stopped) return;
      if (read(fdindex, &index, sizeof(struct index)) != sizeof(struct index)) halt();
      if (index.mesg < min) break;
      if (index.mesg <= max                            &&
	  read_allowed(&index)                         &&
	  (!bid      || !strcmp(index.bid, bid))       &&
	  (!from     || !strcmp(index.from, from))     &&
	  (!to       || !strcmp(index.to, to))         &&
	  (!at       || !strcmp(index.at, at))         &&
	  (!subject  || strcasepos(index.subject, subject))) {
	if (!found) {
	  puts("  Msg#  Size To      @ BBS     From     Date    Subject");
	  found = 1;
	}
	printf("%6d %5ld %-8s%c%-8s %-8s %-7.7s %.31s\n",
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
      if (lseek(fdindex, -2L * sizeof(struct index), SEEK_CUR) < 0) break;
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

  if (!callvalid(strupc(argv[1]))) {
    printf("Invalid call '%s'.\n", argv[1]);
    return;
  }
  mail = alloc_mail();
  strcpy(mail->from, user.name);
  strcpy(mail->to, "m@thebox");
  sprintf(mail->subject, "%s %ld", argv[1], time((long *) 0));
  append_line(mail, "");
  printf("Setting MYBBS to %s.\n", argv[1]);
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
      printf("Msg# %d   To: %s%s%s   From: %s   Date: %s\n",
	     index.mesg,
	     index.to,
	     *index.at ? " @" : "",
	     index.at,
	     index.from,
	     timestr(index.date));
      if (*index.subject) printf("Subject: %s\n", index.subject);
      if (*index.bid) printf("Bulletin ID: %s\n", index.bid);
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

static void reply_command(int argc, char **argv)
{

  FILE *fp;
  char *host;
  char *mesgstr;
  char *p;
  char line[1024];
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
  strcpy(mail->from, user.name);
  if (all) {
    strcpy(mail->to, index.to);
    if (*index.at) {
      strcat(mail->to, "@");
      strcat(mail->to, index.at);
    }
  } else {
    strcpy(mail->to, index.from);
    if (!(fp = fopen(getfilename(mesg), "r"))) halt();
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
    while (isspace(*p & 0xff)) p++;
    if (Strncasecmp(p, "Re:", 3)) break;
    p += 3;
  }
  sprintf(mail->subject, "Re:  %s", p);
  printf("To: %s\n", mail->to);
  printf("Subject: %s\n", mail->subject);
  puts("Enter message: (terminate with ^Z or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (stopped) {
      free_mail(mail);
      return;
    }
    if (*line == '\032') break;
    if (!Strncasecmp(line, "***end", 6)) break;
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

static void send_command(int argc, char **argv)
{

  char *errstr = "The %s option requires an argument.  Type ? SEND for help.\n";
  char *p;
  char at[1024];
  char line[1024];
  char path[1024];
  int check_header = 1;
  int fdlock;
  int i;
  int unique;
  struct mail *mail;

  mail = alloc_mail();
  *at = *path = 0;
  for (i = 1; i < argc; i++)
    if (!strcmp("#", argv[i])) {
      nextarg("#");
      mail->lifetime = atoi(argv[i]);
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
    mail->bid[LEN_BID] = 0;
    strupc(mail->bid);
    if ((fdlock = lock_file(LOCKFILE, 0)) < 0) halt();
    unique = msg_uniq(mail->bid, mail->mid);
    close(fdlock);
    if (!unique) {
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
  strtrim(mail->subject);
  if (!*mail->subject && level != MBOX) {
    errors++;
    puts("No subject specified.");
    free_mail(mail);
    return;
  }
  if (packetcluster || level != MBOX)
    puts("Enter message: (terminate with ^Z or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (stopped) {
      free_mail(mail);
      return;
    }
    if (*line == '\032') break;
    if (!Strncasecmp(line, "***end", 6)) break;
    if (!(check_header && line[0] == ' ' && line[1] == '[')) {
      append_line(mail, line);
      if (check_header) {
	if ((p = get_host_from_header(line))) {
	  if (*path) strcat(path, "!");
	  strcat(path, p);
	} else if (*path)
	  check_header = 0;
      }
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
    chdir(user.cwd);
    *command = 0;
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
  int crunchok = 0;
  int deleted = 0;
  int highest = 0;
  int n;
  int new = 0;
  int readable = 0;
  long validdate;
  struct index *pi, index[1000];

  validdate = time((long *) 0) - 90 * DAYS;
  printf("DK5SG-BBS  Revision: %s %s\n", revision.number, revision.date);
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  for (; ; ) {
    n = read(fdindex, pi = index, 1000 * sizeof(struct index)) / sizeof(struct index);
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
  printf("%6d  Highest message number\n", highest);
  printf("%6d  Active messages\n", active);
  printf("%6d  Readable messages\n", readable);
  if (level == ROOT) {
    printf("%6d  Deleted messages\n", deleted);
    printf("%6d  Messages may be crunched\n", crunchok);
  }
  printf("%6d  Last message listed\n", user.seq);
  printf("%6d  New messages\n", new);
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

#define TEMP_BID_DB     BID_DB ".tmp"
#define TEMP_INDEXFILE  INDEXFILE ".tmp"

  DBM * bid_db;
  datum datum_bid;
  datum datum_offset;
  int f;
  int wflag;
  long offset;
  long validdate;
  struct index index;

  validdate = time((long *) 0) - 90 * DAYS;
  if ((f = open(TEMP_INDEXFILE, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) halt();
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (!(bid_db = dbm_open(TEMP_BID_DB, O_RDWR | O_CREAT, 0644))) halt();
  offset = 0;
  wflag = 0;
  while (read(fdindex, &index, sizeof(struct index)) == sizeof(struct index)) {
    wflag = 1;
    if (!index.deleted || (*index.bid && index.date >= validdate)) {
      if (write(f, &index, sizeof(struct index)) != sizeof(struct index)) halt();
      wflag = 0;
      if (*index.bid && index.date >= validdate) {
	datum_bid.dptr = index.bid;
	datum_bid.dsize = strlen(index.bid);
	datum_offset.dptr = (char *) &offset;
	datum_offset.dsize = sizeof(offset);
	if (dbm_store(bid_db, datum_bid, datum_offset, DBM_REPLACE) < 0) halt();
      }
      offset += sizeof(struct index);
    }
  }
  if (wflag)
    if (write(f, &index, sizeof(struct index)) != sizeof(struct index)) halt();
  if (close(f)) halt();
  dbm_close(bid_db);
  if (close(fdindex)) halt();
  if (rename(TEMP_INDEXFILE, INDEXFILE)) halt();
  if (rename(TEMP_BID_DB ".db", BID_DB ".db")) {
    if (rename(TEMP_BID_DB ".dir", BID_DB ".dir")) halt();
    if (rename(TEMP_BID_DB ".pag", BID_DB ".pag")) halt();
  }
  exit(0);
}

/*---------------------------------------------------------------------------*/

#define Invalid (                                     \
   pi->deleted                                     || \
   (*from    &&  strcmp(pi->from, from))           || \
   (*to      &&  strcmp(pi->to,   to))             || \
   (*at      &&  strcmp(pi->at,   at))             || \
   (*subject && !strcasepos(pi->subject, subject))    \
   )

static void xscreen_command(int argc, char **argv)
{

#ifndef ibm032

  FILE *fp = 0;
  char at[1024];
  char buf[1024];
  char from[1024];
  char subject[1024];
  char to[1024];
  int cmd;
  int indexarrayentries;
  int lines = 0;
  int maxlines;
  int mesg;
  struct index *indexarray;
  struct index *pi;
  struct stat statbuf;
  struct termios curr_termios, prev_termios;
  unsigned int indexarraysize;

  if (fstat(fdindex, &statbuf)) halt();
  indexarraysize = statbuf.st_size;
  indexarrayentries = indexarraysize / sizeof(struct index);
  if (!indexarrayentries) return;
  if (!(indexarray = (struct index *) malloc(indexarraysize))) halt();
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (read(fdindex, indexarray, indexarraysize) != indexarraysize) halt();

  tcgetattr(0, &prev_termios);
  curr_termios = prev_termios;
  curr_termios.c_iflag = BRKINT | ICRNL | IXON | IXANY | IXOFF;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 1;
  curr_termios.c_cc[VTIME] = 0;
  tcsetattr(0, TCSANOW, &curr_termios);
  if (!(maxlines = atoi(getenv("LINES")))) maxlines = 24;

  *from = *to = *at = *subject = 0;
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
      if (unlink(getfilename(pi->mesg))) halt();
      pi->deleted = 1;
      if (lseek(fdindex, (pi - indexarray) * sizeof(struct index), SEEK_SET) < 0) halt();
      if (write(fdindex, pi, sizeof(struct index)) != sizeof(struct index)) halt();

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
      if (!(fp = fopen(getfilename(pi->mesg), "r"))) halt();
      printf("\033[H\033[J\033H\033JMsg# %d   To: %s%s%s   From: %s   Date: %s\n", pi->mesg, pi->to, *pi->at ? " @" : "", pi->at, pi->from, timestr(pi->date));
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
      tcsetattr(0, TCSANOW, &prev_termios);
      printf("\nMessage number: %c", cmd);
      *buf = cmd;
      gets(buf + 1);
      tcsetattr(0, TCSANOW, &curr_termios);
      mesg = atoi(buf);
      pi = indexarray;
      while ((mesg > pi->mesg || Invalid) && pi < indexarray + (indexarrayentries - 1)) pi++;
      while (Invalid && pi > indexarray) pi--;
      cmd = Invalid ? 'q' : 'r';
      break;

    case '<':
      *from = *to = *at = *subject = 0;
      tcsetattr(0, TCSANOW, &prev_termios);
      printf("\nFROM field: ");
      gets(from);
      tcsetattr(0, TCSANOW, &curr_termios);
      strupc(from);
      cmd = Invalid ? '\n' : 'r';
      break;

    case '>':
      *from = *to = *at = *subject = 0;
      tcsetattr(0, TCSANOW, &prev_termios);
      printf("\nTO field: ");
      gets(to);
      tcsetattr(0, TCSANOW, &curr_termios);
      strupc(to);
      cmd = Invalid ? '\n' : 'r';
      break;

    case '@':
      *from = *to = *at = *subject = 0;
      tcsetattr(0, TCSANOW, &prev_termios);
      printf("\nAT field: ");
      gets(at);
      tcsetattr(0, TCSANOW, &curr_termios);
      strupc(at);
      cmd = Invalid ? '\n' : 'r';
      break;

    case 's':
      *from = *to = *at = *subject = 0;
      tcsetattr(0, TCSANOW, &prev_termios);
      printf("\nSUBJECT substring: ");
      gets(subject);
      tcsetattr(0, TCSANOW, &curr_termios);
      cmd = Invalid ? '\n' : 'r';
      break;

    case 'v':
      sprintf(buf, "vi %s", getfilename(pi->mesg));
      tcsetattr(0, TCSANOW, &prev_termios);
      system(buf);
      tcsetattr(0, TCSANOW, &curr_termios);
      cmd = 'r';
      break;

    case 'q':
      tcsetattr(0, TCSANOW, &prev_termios);
      free(indexarray);
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
      puts("q          quit screen mode");
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

#endif

}

#undef Invalid

/*---------------------------------------------------------------------------*/

static char *connect_addr(const char *host)
{

  FILE *fp;
  char *addr;
  char *h;
  char *p;
  static char line[1024];

  if (!(fp = fopen(CONFIGFILE, "r"))) rename("config", CONFIGFILE);
  if (!(fp = fopen(CONFIGFILE, "r"))) return 0;
  addr = 0;
  while (fgets(line, sizeof(line), fp)) {
    if ((p = strchr(line, '#'))) *p = 0;
    for (p = line; isspace(*p & 0xff); p++) ;
    for (h = p; *p && !isspace(*p & 0xff); p++) ;
    if (*p) *p++ = 0;
    if (!strcmp(h, host)) {
      addr = strtrim(p);
      break;
    }
  }
  fclose(fp);
  return addr;
}

/*---------------------------------------------------------------------------*/

static void connect_bbs(void)
{

  char *address;
  int addrlen;
  int fd;
  struct sockaddr *addr;

  if (!(address = connect_addr(user.name))) exit(1);
  if (!(addr = build_sockaddr("unix:/tcp/.sockets/netcmd", &addrlen))) exit(1);
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
  if (doforward) {
    connect_bbs();
    wait_for_prompt();
  }
  if (level == MBOX) printf("[THEBOX-1.8-H$]\n");
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

static void recv_from_mail_or_news(void)
{

  char *cp;
  char distr[1024];
  char expire[1024];
  char line[1024];
  int from_priority;
  int n;
  int state;
  long expiretime;
  struct mail *mail;

  while (fgets(line, sizeof(line), stdin)) {
    mail = alloc_mail();
    *distr = 0;
    *expire = 0;
    from_priority = 0;
    state = 0;
    n = strncmp(line, "#! rnews ", 9) ? 0x7fffffff : atoi(line + 9);
    for (; ; ) {
      switch (state) {
      case 0:
	if (*line) {
	  if (from_priority < 1 && !strncmp(line, "From ", 5) && sscanf(line, "From %s", mail->from) == 1)
	    from_priority = 1;
	  if (from_priority < 2 && get_header_value("Path:", 1, line, mail->from))
	    from_priority = 2;
	  if (from_priority < 3 && get_header_value("From:", 1, line, mail->from))
	    from_priority = 3;
	  get_header_value("Newsgroups:", 1, line, mail->to);
	  get_header_value("Subject:", 0, line, mail->subject);
	  get_header_value("Message-ID:", 1, line, mail->mid);
	  get_header_value("Distribution:", 1, line, distr);
	  get_header_value("Expires:", 1, line, expire);
	  get_header_value("Bulletin-ID:", 1, line, mail->bid);
	} else
	  state = 1;
	break;
      case 1:
	if (!*line) break;
	state = 2;
      case 2:
	append_line(mail, line);
      }
      if (n <= 0 || !fgets(line, sizeof(line), stdin)) break;
      n -= strlen(line);
      strtrim(line);
    }
    while (*mail->to && strncmp(mail->to, "ampr.bbs.", 9))
      if ((cp = strchr(mail->to, ',')))
	strcpy(mail->to, cp + 1);
      else
	*mail->to = 0;
    if (*mail->to) strcpy(mail->to, mail->to + 9);
    if (*mail->to && from_priority && state == 2) {
      if ((cp = strchr(mail->to, ','))) *cp = 0;
      if ((cp = strrchr(mail->to, '.'))) strcpy(mail->to, cp + 1);
      if ((cp = strchr(distr, ','))) *cp = 0;
      if ((cp = strrchr(distr, '.'))) strcpy(distr, cp + 1);
      if (*distr) {
	strcat(mail->to, "@");
	strcat(mail->to, distr);
      }
      if ((expiretime = parse_date(expire)) != -1) {
	mail->lifetime = (expiretime - time((long *) 0) + DAYS - 1) / DAYS;
	if (mail->lifetime < 1) mail->lifetime = 1;
      }
      route_mail(mail);
    } else
      free_mail(mail);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char *cp;
  char *dir = WRKDIR;
  char *sysname = 0;
  char buf[1024];
  int c;
  int err_flag = 0;
  struct passwd *pw;

  if (!*UUCP_DIR || !*SENDMAIL_PROG) halt();

  signal(SIGINT,  interrupt_handler);
  signal(SIGQUIT, interrupt_handler);
  signal(SIGALRM, alarm_handler);

  umask(022);

  sscanf((char *) rcsid, "%*s %*s %*s %s %s %s %s %s",
		revision.number,
		revision.date,
		revision.time,
		revision.author,
		revision.state);

  while ((c = getopt(argc, argv, "df:mpw:")) != EOF)
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
      mode = MAILorNEWS;
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
    puts("usage: bbs [-d] [-w seconds] [-f system|-m]");
    exit(1);
  }

  if (!getcwd(buf, sizeof(buf))) halt();
  user.cwd = strdup(buf);

  if (chdir(dir)) {
    mkdir(dir, 0755);
    if (chdir(dir)) halt();
  }

  if (gethostname(buf, sizeof(buf))) halt();
  if ((cp = strchr(buf, '.'))) *cp = 0;
  myhostname = strdup(buf);
  MYHOSTNAME = strdup(strupc(buf));

  pw = doforward ? getpwnam(sysname) : getpwuid(getuid());
  if (!pw) halt();
  user.name = strdup(pw->pw_name);
  user.uid = pw->pw_uid;
  user.gid = pw->pw_gid;
  user.dir = strdup(pw->pw_dir);
  user.shell = strdup(pw->pw_shell);
  endpwent();
  if (!user.uid) level = ROOT;
  if (connect_addr(user.name)) level = MBOX;

  if (!getenv("LOGNAME")) {
    sprintf(buf, "LOGNAME=%s", user.name);
    putenv(strdup(buf));
  }
  if (!getenv("HOME")) {
    sprintf(buf, "HOME=%s", user.dir);
    putenv(strdup(buf));
  }
  if (!getenv("SHELL")) {
    sprintf(buf, "SHELL=%s", user.shell);
    putenv(strdup(buf));
  }
  if (!getenv("PATH"))
    putenv("PATH=/bin:/usr/bin:/usr/contrib/bin:/usr/local/bin");
  if (!getenv("TZ"))
    putenv("TZ=MEZ-1MESZ");

  get_seq();

  if ((fdindex = open(INDEXFILE, O_RDWR | O_CREAT, 0644)) < 0) halt();

  switch (mode) {
  case BBS:
    bbs();
    break;
  case MAILorNEWS:
    recv_from_mail_or_news();
    break;
  }
  return 0;
}

