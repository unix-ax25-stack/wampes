static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 3.0 1995/11/07 22:54:41 deyke Exp $";

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

#include "buildsaddr.h"
#include "callvalid.h"
#include "configure.h"
#include "lockfile.h"
#include "seteugid.h"
#include "strdup.h"
#include "strmatch.h"

#define DEBUG_NNTP      0

#define MIDSUFFIX               "@bbs.net"
#define NEWSGROUPSPREFIX        "ampr.bbs."
#define NEWSGROUPSPREFIXLENGTH  9               /* strlen(NEWSGROUPSPREFIX) */

#define BBSCONFIGFILE   "/tcp/bbs.conf"
#define BBSRCFILE       ".bbsrc"
#define HELPFILE        "/usr/local/lib/bbs.help"
#define INFOFILE        "/usr/local/lib/station.data"
#define LOCKDIR         "/tcp/locks"
#define MAILCONFIGFILE  "/tcp/mail.conf"
#define NEWSRCFILE      ".newsrc.bbs"

#define BIDLOCKFILE     LOCKDIR "/bbs.bid"
#define FWDLOCKFILE     LOCKDIR "/bbs.fwd."     /* Append name of host */

#define SECONDS         (1L)
#define MINUTES         (60L*SECONDS)
#define HOURS           (60L*MINUTES)
#define DAYS            (24L*HOURS)

#define LEN_BID         12

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

enum e_what {
  HEADER_ONLY,
  HEADER_AND_BODY
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

struct group {
  char *name;
  int low;
  int high;
  int subscribed;
  int unread;
  char *bitarray;
  struct group *next;
};

static struct group *current_group;
static struct group *groups;

struct cmdtable {
  const char *name;
  void (*fnc)(int argc, const char **argv);
  int minargs;
  int maxargs;
  enum e_level level;
};

struct alias {
  char *name;
  char *value;
  struct alias *next;
};

static struct alias *aliases;

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

static char headers[1024] = "DFS";
static char mydomain[1024];
static char myhostname[1024];
static char prompt[1024] = "\\w:\\c > ";
static const char daynames[] = "SunMonTueWedThuFriSat";
static const char monthnames[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
static int current_article;
static int did_forward;
static int do_forward;
static int errors;
static int export;
static int level;
static int maxage = 7;
static int output_only;
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

#define set_read(p, i) \
	(p)->bitarray[((i) - (p)->low) >> 3] |= (1 << (((i) - (p)->low) & 7))

/*---------------------------------------------------------------------------*/

#define unset_read(p, i) \
	(p)->bitarray[((i) - (p)->low) >> 3] &= ~(1 << (((i) - (p)->low) & 7))

/*---------------------------------------------------------------------------*/

#define is_read(p, i) \
	((p)->bitarray[((i) - (p)->low) >> 3] & (1 << (((i) - (p)->low) & 7)))

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

static int stringcasematch(const char *s, const char *p)
{

  char pat[1024];
  char str[1024];
  char *t;
  const char *f;

  t = str;
  for (f = s; *f; f++) {
    *t++ = Xtolower(*f);
  }
  *t = 0;

  t = pat;
  for (f = p; *f; f++) {
    *t++ = Xtolower(*f);
  }
  *t = 0;

  return stringmatch(str, pat);
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

  char buf[1024];
  char monthstr[1024];
  char timestr[1024];
  char tzstr[1024];
  char *cp;
  char *p;
  int dd;
  int h;
  int m;
  int s;
  int tzcorrection = 0;
  int yy;

  strcpy(buf, str);

  /* Skip day "," */

  if ((cp = strchr(buf, ',')))
    cp++;
  else
    cp = buf;

  switch (sscanf(cp, "%d %s %d %s %s", &dd, monthstr, &yy, timestr, tzstr)) {
  case 4:
    break;
  case 5:
    if (!strcmp(tzstr, "EDT"))
      strcpy(tzstr, "-0400");
    else if (!strcmp(tzstr, "EST") || !strcmp(tzstr, "CDT"))
      strcpy(tzstr, "-0500");
    else if (!strcmp(tzstr, "CST") || !strcmp(tzstr, "MDT"))
      strcpy(tzstr, "-0600");
    else if (!strcmp(tzstr, "MST") || !strcmp(tzstr, "PDT"))
      strcpy(tzstr, "-0700");
    else if (!strcmp(tzstr, "PST"))
      strcpy(tzstr, "-0800");
    if ((tzstr[0] == '-' || tzstr[0] == '+') &&
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
    }
    break;
  default:
    return -1;
  }

  switch (sscanf(timestr, "%d:%d:%d", &h, &m, &s)) {
  case 2:
    s = 0;
    break;
  case 3:
    break;
  default:
    return -1;
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

static void generate_bid_and_mid(struct mail *mail, int generate_bid)
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

  if (generate_bid && !*mail->bid) {
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

  if (*mail->bid && !*mail->mid) {
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

  generate_bid_and_mid(mail, 1);

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
	  mydomain);
  append_line(mail, strupc(buf), HEAD);

  /* Call delivery agents */

  if (callvalid(mail->touser) ||
      (callvalid(mail->tohost) && Strcasecmp(mail->tohost, myhostname))) {
    send_to_mail_or_news(mail, MAIL);
  } else {
    send_to_mail_or_news(mail, NEWS);
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

static struct group *find_group(const char *name)
{

  int r;
  struct group *p;

  for (p = groups; p; p = p->next) {
    r = strcmp(p->name, name);
    if (!r)
      return p;
    if (r > 0)
      return 0;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static void store_groups(void)
{

  char fname[1024];
  FILE *fp;
  int firstarticel;
  int i;
  int laststate;
  int state;
  int writecomma;
  struct group *p;

  sprintf(fname, "%s/%s", user.dir, NEWSRCFILE);
  seteugid(user.uid, user.gid);
  fp = fopen(fname, "w");
  seteugid(0, 0);
  if (!fp)
    halt();
  for (p = groups; p; p = p->next) {
    fprintf(fp, "%s%c", p->name, p->subscribed ? ':' : '!');
    writecomma = laststate = firstarticel = 0;
    for (i = p->low; i <= p->high + 1; i++) {
      state = (i <= p->high && is_read(p, i)) ? 1 : 0;
      if (state != laststate) {
	if (state) {
	  firstarticel = i;
	} else {
	  if (writecomma) {
	    putc(',', fp);
	  } else {
	    putc(' ', fp);
	    writecomma = 1;
	  }
	  fprintf(fp, "%d", firstarticel);
	  if (firstarticel != i - 1) {
	    fprintf(fp, "-%d", i - 1);
	  }
	}
	laststate = state;
      }
    }
    putc('\n', fp);
  }
  fclose(fp);
}

/*---------------------------------------------------------------------------*/

static void load_groups(void)
{

  char current_group_name[1024];
  char line[16 * 1024];
  char name[1024];
  char *cp;
  FILE *fp;
  int changed;
  int current_article_save;
  int high;
  int i;
  int low;
  int subscribed;
  struct group *p;
  struct group *q;

  sprintf(line, "%s/%s", user.dir, NEWSRCFILE);
  fp = fopen(line, "r");

  if (current_group) {
    strcpy(current_group_name, current_group->name);
    current_article_save = current_article;
  } else {
    *current_group_name = 0;
    current_article_save = 0;
  }
  current_group = 0;
  current_article = 0;

  while ((p = groups)) {
    groups = p->next;
    free(p->name);
    free(p->bitarray);
    free(p);
  }

  if (nntp_channel.fd < 0)
    open_nntp();
  write_nntp("list active\r\n");
  read_nntp(line);
  if (*line != '2')
    halt();

  for (;;) {
    read_nntp(line);
    if (*line == '.')
      break;
    if (sscanf(line, "%s %d %d", name, &high, &low) != 3)
      halt();
    if (strncmp(name, NEWSGROUPSPREFIX, NEWSGROUPSPREFIXLENGTH))
      continue;
    if (!(p = (struct group *) malloc(sizeof(struct group))))
      halt();
    if (!(p->name = strdup(name)))
      halt();
    p->low = low;
    p->high = high;
    p->subscribed = !fp;
    p->unread = high - low + 1;
    if (!(p->bitarray = (char *) calloc(1, p->unread / 8 + 1)))
      halt();
    if (!groups || strcmp(name, groups->name) < 0) {
      p->next = groups;
      groups = p;
    } else {
      for (q = groups;
	   q->next && strcmp(q->next->name, name) < 0;
	   q = q->next) ;
      p->next = q->next;
      q->next = p;
    }
  }

  if (*current_group_name && (current_group = find_group(current_group_name))) {
    current_article = current_article_save;
    if (current_article < current_group->low)
      current_article = current_group->low;
  }

  if (!fp)
    return;
  changed = 0;
  while (fgets(line, sizeof(line), fp)) {
    for (cp = line; *cp && *cp != ':' && *cp != '!'; cp++) ;
    if (!*cp) {
      changed = 1;
      continue;
    }
    subscribed = (*cp == ':');
    *cp++ = 0;
    p = find_group(line);
    if (!p) {
      changed = 1;
      continue;
    }
    p->subscribed = subscribed;
    if (*cp++ != ' ') {
      changed = 1;
      continue;
    }
    for (;;) {
      if (!isdigit(*cp & 0xff))
	break;
      for (low = 0; isdigit(*cp & 0xff); cp++)
	low = 10 * low + *cp - '0';
      if (*cp == '-') {
	cp++;
	if (!isdigit(*cp & 0xff))
	  break;
	for (high = 0; isdigit(*cp & 0xff); cp++)
	  high = 10 * high + *cp - '0';
      } else {
	high = low;
      }
      if (high > p->high) {
	memset(p->bitarray, 0, (p->high - p->low + 1) / 8 + 1);
	p->unread = p->high - p->low + 1;
	changed = 1;
	break;
      }
      if (low < p->low) {
	low = p->low;
	changed = 1;
      }
      for (i = low; i <= high; i++) {
	set_read(p, i);
	p->unread--;
      }
      if (*cp++ != ',')
	break;
    }
  }
  fclose(fp);
  if (changed) {
    store_groups();
  }
}

/*---------------------------------------------------------------------------*/

static int set_position(int find_next_unread)
{
  struct group *p;

  if (!groups) {
    load_groups();
    if (!groups) {
      printf("No groups available.\n");
      return 0;
    }
  }

  if (!find_next_unread) {
    if (!current_group) {
      printf("Need to specify group first.\n");
      return 0;
    } else {
      return 1;
    }
  }

  if (!current_group) {
    for (current_group = groups;
	 current_group;
	 current_group = current_group->next) {
      if (current_group->subscribed)
	break;
    }
    if (!current_group) {
      printf("No subscribed-to groups available.\n");
      return 0;
    }
    current_article = current_group->low;
  }

  if (!current_group->unread) {
    p = current_group;
    for (;;) {
      if (!(p = p->next))
	p = groups;
      if (p == current_group) {
	printf("No more articels to read.\n");
	return 0;
      }
      if (p->subscribed && p->unread) {
	current_group = p;
	current_article = p->low;
	break;
      }
    }
  }

  for (;;) {
    if (!is_read(current_group, current_article))
      return current_article;
    current_article++;
    if (current_article > current_group->high)
      current_article = current_group->low;
  }

}

/*---------------------------------------------------------------------------*/

static void parse_range(const struct group *p, const char *s, int *low, int *high)
{
  if ((*s == '-' || *s == '*') && !s[1]) {      /* '-' or '*' */
    *low = p->low;
    *high = p->high;
  } else if (*s == '-') {       /* -number */
    *low = p->low;
    *high = atoi(s + 1) + p->low - 1;
  } else {
    *low = atoi(s) + p->low - 1;
    s = strchr(s, '-');
    if (!s) {                   /* number */
      *high = *low;
    } else if (s[1]) {          /* number-number */
      *high = atoi(s + 1) + p->low - 1;
    } else {
      *high = p->high;          /* number- */
    }
  }
}

/*---------------------------------------------------------------------------*/

static void change_alias(const char *name, const char *value)
{

  char buf[1024];
  struct alias *p;
  struct alias *q;

  name = strupc(strcpy(buf, name));

  for (q = 0, p = aliases; p; q = p, p = p->next) {
    if (!strcmp(p->name, name)) {
      if (q)
	q->next = p->next;
      else
	aliases = p->next;
      free(p->name);
      free(p->value);
      free(p);
    }
  }

  if (*value) {
    if (!(p = (struct alias *) malloc(sizeof(struct alias))))
      halt();
    if (!(p->name = strdup((char *) name)))
      halt();
    if (!(p->value = strdup((char *) value)))
      halt();
    if (!aliases || strcmp(name, aliases->name) < 0) {
      p->next = aliases;
      aliases = p;
    } else {
      for (q = aliases;
	   q->next && strcmp(q->next->name, name) < 0;
	   q = q->next) ;
      p->next = q->next;
      q->next = p;
    }
  }
}

/*---------------------------------------------------------------------------*/

static struct mail *read_mail_or_news_file(const char *filename, enum e_type src, enum e_what what)
{

  char date[1024];
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

  while (fgets(line, sizeof(line), fp) &&
	 (in_header || what == HEADER_AND_BODY)) {
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
	get_header_value("Date:", 0, line, date);
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
    while (*newsgroups &&
	   strncmp(newsgroups, NEWSGROUPSPREFIX, NEWSGROUPSPREFIXLENGTH)) {
      if ((cp = strchr(newsgroups, ',')))
	strcpy(newsgroups, cp + 1);
      else
	*newsgroups = 0;
    }
    if (*newsgroups)
      strcpy(newsgroups, newsgroups + NEWSGROUPSPREFIXLENGTH);
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

  generate_bid_and_mid(mail, src != NEWS);
  if (!*mail->bid) {
    free_mail(mail);
    return 0;
  }

  mail->date = parse_rfc822_date(date);
  if (mail->date < 0)
    mail->date = 0;

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
    return 0;
  }

  return mail;
}

/*---------------------------------------------------------------------------*/

static struct mail *get_article(const struct group *p, int i, enum e_what what)
{

  char filename[1024];
  char *cp;
  struct mail *mail;
  struct stat statbuf;

  if (i < p->low || i > p->high)
    return 0;
  sprintf(filename, NEWS_DIR "/%s/%d", p->name, i);
  for (cp = filename; *cp; cp++) {
    if (*cp == '.')
      *cp = '/';
  }
  if (stat(filename, &statbuf) ||
      !(mail = read_mail_or_news_file(filename, NEWS, what))) {
    return 0;
  }
  return mail;
}

/*---------------------------------------------------------------------------*/

static void output_message(FILE *fp)
{

  char path[1024];
  char *cp;
  int inheader;
  struct mail *mail;
  struct strlist *s;

  if (!is_read(current_group, current_article)) {
    set_read(current_group, current_article);
    current_group->unread--;
    store_groups();
  }
  if (!(mail = get_article(current_group, current_article, HEADER_AND_BODY))) {
    printf("No such message: '%d'.\n", current_article - current_group->low + 1);
    return;
  }
  if (strchr(headers, 'M'))
    fprintf(fp, "Msg#: %d\n", current_article - current_group->low + 1);
  if (strchr(headers, 'F'))
    fprintf(fp, "From: %s@%s\n", mail->fromuser, mail->fromhost);
  if (strchr(headers, 'T'))
    fprintf(fp, "To: %s@%s\n", mail->touser, mail->tohost);
  if (strchr(headers, 'D'))
    fprintf(fp, "Date: %s\n", rfc822_date_string(mail->date));
  if (strchr(headers, 'S'))
    fprintf(fp, "Subject: %s\n", mail->subject);
  if (strchr(headers, 'B'))
    fprintf(fp, "Bulletin-ID: %s\n", mail->bid);
  if (mail->lifetime) {
    if (strchr(headers, 'E'))
      fprintf(fp, "Expires: %s\n", rfc822_date_string(mail->date + DAYS * mail->lifetime));
    if (strchr(headers, 'L'))
      fprintf(fp, "Lifetime: %d\n", mail->lifetime);
  }
  *path = 0;
  inheader = 1;
  for (s = mail->head; s && !stopped; s = s->next) {
    if (inheader) {
      if ((cp = get_host_from_header(s->str))) {
	if (strchr(headers, 'R'))
	  fprintf(fp, "%s\n", s->str);
	strcat(path, *path ? "!" : "Path: ");
	strcat(path, cp);
	continue;
      }
      if (*path && strchr(headers, 'P'))
	fprintf(fp, "%s\n", path);
      fprintf(fp, "\n");
      inheader = 0;
    }
    fprintf(fp, "%s\n", s->str);
  }
  fprintf(fp, "\n");
  free_mail(mail);
}

/*---------------------------------------------------------------------------*/

static void alias_command(int argc, const char **argv)
{

  int len;
  struct alias *p;

  switch (argc) {

  case 1:
    for (p = aliases; p; p = p->next) {
      printf("%-10s \"%s\"\n", p->name, p->value);
    }
    break;

  case 2:
    if ((len = strlen(argv[1]))) {
      for (p = aliases; p; p = p->next) {
	if (!Strncasecmp(p->name, argv[1], len)) {
	  printf("%-10s \"%s\"\n", p->name, p->value);
	  return;
	}
      }
    }
    break;

  case 3:
    change_alias(argv[1], argv[2]);
    break;

  }
}

/*---------------------------------------------------------------------------*/

static void delete_command(int argc, const char **argv)
{

  int high;
  int i;
  int low;
  struct mail *delete_mail;
  struct mail *orig_mail;

  if (!set_position(0))
    return;
  argc--;
  argv++;
  for (; argc; argc--, argv++) {
    parse_range(current_group, *argv, &low, &high);
    if (low < current_group->low || high > current_group->high) {
      printf("Message number out of range.\n");
      continue;
    }
    for (i = low; i <= high; i++) {
      if (!(orig_mail = get_article(current_group, i, HEADER_ONLY))) {
	printf("No such message: '%d'.\n", i - current_group->low + 1);
	continue;
      }
      if (level < ROOT && Strcasecmp(orig_mail->fromuser, user.name)) {
	printf("Article %d not deleted: Permission denied.\n", i - current_group->low + 1);
	free_mail(orig_mail);
	continue;
      }
      delete_mail = alloc_mail();
      strcpy(delete_mail->fromuser, user.name);
      strcpy(delete_mail->touser, "e");
      strcpy(delete_mail->tohost, "thebox");
      sprintf(delete_mail->subject, "%s", orig_mail->bid);
      free_mail(orig_mail);
      route_mail(delete_mail);
    }
  }
}

/*---------------------------------------------------------------------------*/

static void dir_command(int argc, const char **argv)
{

  char pattern[1024];
  int column;
  int count;
  int i;
  struct group *p;

  if (!groups)
    load_groups();

  column = 0;
  for (p = groups; p && !stopped; p = p->next) {
    if (argc >= 2) {
      for (i = 1; i < argc; i++) {
	strcpy(pattern, NEWSGROUPSPREFIX);
	strcat(pattern, argv[i]);
	if (stringcasematch(p->name, pattern))
	  break;
      }
      if (i >= argc)
	continue;
      count = p->high - p->low + 1;
    } else {
      if (!p->subscribed || !p->unread)
	continue;
      count = p->unread;
    }
    printf("%c%6d %-8s   ",
	   p->subscribed ? ' ' : 'u',
	   count,
	   p->name + NEWSGROUPSPREFIXLENGTH);
    if (++column >= 4) {
      putchar('\n');
      column = 0;
    }
  }

  if (column)
    putchar('\n');
}

/*---------------------------------------------------------------------------*/

static void disconnect_command(int argc, const char **argv)
{
  puts("Disconnecting...");
  kill(0, 1);
  exit(0);
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

static void export_message(struct mail *mail)
{

  char buf[1024];
  struct strlist *p;

  sprintf(buf, "SB %s", mail->touser);
  if (*mail->tohost)
    sprintf(buf + strlen(buf), " @ %s", mail->tohost);
  if (*mail->fromuser)
    sprintf(buf + strlen(buf), " < %s", mail->fromuser);
  if (*mail->bid)
    sprintf(buf + strlen(buf), " $%s", mail->bid);
  puts(strupc(buf));
  puts(*mail->subject ? mail->subject : "(no subject)");
  for (p = mail->head; p; p = p->next)
    puts(p->str);
  puts("/EX");
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
    if ((mail = read_mail_or_news_file(dfile, MAIL, HEADER_AND_BODY))) {
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

  sprintf(batchfile, NEWS_DIR "/out.going/%s", user.name);
  sprintf(workfile, "%s.bbs", batchfile);
  if (!(fp = fopen(workfile, "r"))) {
    if (rename(batchfile, workfile))
      return;
    sprintf(line, CTLINND_PROG " -s flush %s", user.name);
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
      sprintf(tmp, NEWS_DIR "/%s", line);
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
      if ((mail = read_mail_or_news_file(filelist->str, NEWS, HEADER_AND_BODY))) {
	if (export)
	  export_message(mail);
	else
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

static void f_command(int argc, const char **argv)
{

  char fname[1024];
  int did_any_forward;
  int fdlock;

  did_any_forward = 0;
  sprintf(fname, FWDLOCKFILE "%s", user.name);
  if ((fdlock = lock_file(fname, 1)) >= 0) {
    for (;;) {
      did_forward = 0;
      forward_mail();
      forward_news();
      if (!did_forward)
	break;
      did_any_forward = 1;
    }
    close(fdlock);
  }
  if (output_only || (!do_forward && !did_any_forward))
    exit(0);
  putchar('F');
}

/*---------------------------------------------------------------------------*/

static void group_command(int argc, const char **argv)
{

  char name[1024];
  struct group *p;

  if (!groups)
    load_groups();

  switch (argc) {

  case 1:
    printf("GROUP: %s\n",
	   current_group ?
		   current_group->name + NEWSGROUPSPREFIXLENGTH :
		   "NONE");
    break;

  case 2:
    strcpy(name, NEWSGROUPSPREFIX);
    strcat(name, argv[1]);
    p = find_group(name);
    if (!p) {
      printf("No such group: '%s'.\n", argv[1]);
      return;
    }
    current_group = p;
    current_article = p->low;
    break;

  }
}

/*---------------------------------------------------------------------------*/

static void headers_command(int argc, const char **argv)
{
  switch (argc) {
  case 1:
    printf("HEADERS: %s\n", headers);
    break;
  case 2:
    strupc(strcpy(headers, argv[1]));
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void info_command(int argc, const char **argv)
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

static void list_command(int argc, const char **argv)
{

  char from[1024];
  char pattern[1024];
  int changed;
  int i;
  int j;
  int unread;
  long timeout;
  struct mail *mail;

  if (!set_position(argc < 2))
    return;
  printf("GROUP: %s\n", current_group->name + NEWSGROUPSPREFIXLENGTH);
  changed = 0;
  timeout = time(0) - maxage * DAYS;
  for (i = current_group->low; i <= current_group->high && !stopped; i++) {
    unread = !is_read(current_group, i);
    if (!unread && argc < 2)
      continue;
    if (!(mail = get_article(current_group, i, HEADER_ONLY))) {
      if (unread) {
	set_read(current_group, i);
	current_group->unread--;
	unread = 0;
	changed = 1;
      }
      continue;
    }
    if (mail->date < timeout) {
      if (unread) {
	set_read(current_group, i);
	current_group->unread--;
	unread = 0;
	changed = 1;
      }
      if (argc < 2) {
	free_mail(mail);
	continue;
      }
    }
    sprintf(from, "%s@%s", mail->fromuser, mail->fromhost);
    if (argc >= 2) {
      for (j = 1; j < argc; j++) {
	*pattern = '*';
	strcpy(pattern + 1, argv[j]);
	strcat(pattern + 1, "*");
	if (stringcasematch(from, pattern))
	  break;
	if (stringcasematch(mail->subject, pattern))
	  break;
      }
      if (j >= argc) {
	free_mail(mail);
	continue;
      }
    }
    printf("%5d %c %-55.55s %-15.15s\n",
	   i - current_group->low + 1,
	   unread ? '+' : ' ',
	   mail->subject,
	   from);
    free_mail(mail);
  }
  if (changed) {
    store_groups();
  }
}

/*---------------------------------------------------------------------------*/

static void mark_command(int argc, const char **argv)
{

  char argvbuf[16];
  int a;
  int changed;
  int high;
  int i;
  int low;
  int mark;

  mark = (argv[0][0] == 'M' || argv[0][0] == 'm');
  if (!set_position(0))
    return;
  if (argc < 2) {
    sprintf(argvbuf, "%d", current_article - current_group->low + 1);
    argc = 2;
    argv[1] = argvbuf;
  }
  changed = 0;
  for (a = 1; a < argc; a++) {
    parse_range(current_group, argv[a], &low, &high);
    if (low < current_group->low || high > current_group->high) {
      printf("Message number out of range.\n");
      continue;
    }
    for (i = low; i <= high; i++) {
      if (mark) {
	if (!is_read(current_group, i)) {
	  set_read(current_group, i);
	  current_group->unread--;
	  changed = 1;
	}
      } else {
	if (is_read(current_group, i)) {
	  unset_read(current_group, i);
	  current_group->unread++;
	  changed = 1;
	}
      }
    }
  }
  if (changed) {
    store_groups();
  }
}

/*---------------------------------------------------------------------------*/

static void maxage_command(int argc, const char **argv)
{
  switch (argc) {
  case 1:
    printf("MAXAGE: %d\n", maxage);
    break;
  case 2:
    maxage = atoi(argv[1]);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void mybbs_command(int argc, const char **argv)
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

static void prompt_command(int argc, const char **argv)
{
  switch (argc) {
  case 1:
    printf("PROMPT: \"%s\"\n", prompt);
    break;
  case 2:
    strcpy(prompt, argv[1]);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void quit_command(int argc, const char **argv)
{
  puts("BBS terminated.");
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void read_command(int argc, const char **argv)
{

  char argvbuf[16];
  FILE *fp;
  int high;
  int low;
  int mode;

  mode = Xtolower(argv[0][0]);
  fp = stdout;
  argc--;
  argv++;
  if (mode == 'w' || mode == 'p') {
    seteugid(user.uid, user.gid);
    if (mode == 'w')
      fp = fopen(*argv, "a");
    else
      fp = popen(*argv, "w");
    seteugid(0, 0);
    if (!fp) {
      perror(*argv);
      return;
    }
    argc--;
    argv++;
  }
  if (!argc) {
    if (!set_position(1))
      goto Done;
    sprintf(argvbuf, "%d", current_article - current_group->low + 1);
    argc = 1;
    *argv = argvbuf;
  } else {
    if (!set_position(0))
      goto Done;
  }
  for (; argc && !stopped; argc--, argv++) {
    parse_range(current_group, *argv, &low, &high);
    if (low < current_group->low || high > current_group->high) {
      printf("Message number out of range.\n");
      continue;
    }
    for (current_article = low; !stopped;) {
      output_message(fp);
      if (current_article >= high)
	break;
      current_article++;
    }
  }
Done:
  switch (mode) {
  case 'w':
    fclose(fp);
    break;
  case 'p':
    pclose(fp);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static int collect_message(struct mail *mail, int do_prompt, int check_header)
{

  char line[1024];
  char *p;

  if (do_prompt)
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

static void reply_command(int argc, const char **argv)
{

  char *cp;
  int all;
  int i;
  struct mail *orig_mail;
  struct mail *reply_mail;

  if (!set_position(0))
    return;
  argc--;
  argv++;
  all = 0;
  if (argc && !Strcasecmp(*argv, "all")) {
    all = 1;
    argc--;
    argv++;
  }
  if (argc < 1) {
    i = current_article;
  } else if (argc == 1) {
    i = atoi(*argv) + current_group->low - 1;
  } else {
    printf("Too many arguments.  Type HELP REPLY for help.\n");
    return;
  }
  if (!(orig_mail = get_article(current_group, i, HEADER_ONLY))) {
    printf("No such message: '%d'.\n", i - current_group->low + 1);
    return;
  }
  current_article = i;
  reply_mail = alloc_mail();
  strcpy(reply_mail->fromuser, user.name);
  if (all) {
    strcpy(reply_mail->touser, orig_mail->touser);
    strcpy(reply_mail->tohost, orig_mail->tohost);
  } else {
    strcpy(reply_mail->touser, orig_mail->fromuser);
    strcpy(reply_mail->tohost, orig_mail->fromhost);
  }
  for (cp = orig_mail->subject;;) {
    while (isspace(*cp & 0xff))
      cp++;
    if (Strncasecmp(cp, "Re:", 3))
      break;
    cp += 3;
  }
  sprintf(reply_mail->subject, "Re: %s", cp);
  free_mail(orig_mail);
  printf("To: %s@%s\n", reply_mail->touser, reply_mail->tohost);
  printf("Subject: %s\n", reply_mail->subject);
  if (!collect_message(reply_mail, 1, 0)) {
    free_mail(reply_mail);
    return;
  }
  printf("Sending message to %s@%s.\n", reply_mail->touser, reply_mail->tohost);
  route_mail(reply_mail);
}

/*---------------------------------------------------------------------------*/

static void reload_command(int argc, const char **argv)
{
  load_groups();
}

/*---------------------------------------------------------------------------*/

#define nextarg(name)     \
  if (++i >= argc) {      \
    errors++;             \
    printf(errstr, name); \
    free_mail(mail);      \
    return;               \
  }

static void send_command(int argc, const char **argv)
{

  char line[1024];
  char *errstr = "The %s option requires an argument.  Type HELP SEND for help.\n";
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
  if (output_only) {
    if (!got_control_z)
      puts("No");
    free_mail(mail);
    return;
  }
  if (*mail->bid) {
    generate_bid_and_mid(mail, 1);
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

static void shell_command(int argc, const char **argv)
{

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
    for (i = open_max() - 1; i >= 3; i--)
      close(i);
    if (argc == 1)
      execl(user.shell, user.shell, 0);
    else
      execl(user.shell, user.shell, "-c", argv[1], 0);
    _exit(127);
    break;
  default:
    signal(SIGINT, SIG_IGN);
    signal(SIGQUIT, SIG_IGN);
    while (wait(&status) != pid) ;
    signal(SIGINT, interrupt_handler);
    signal(SIGQUIT, interrupt_handler);
    break;
  }
}

/*---------------------------------------------------------------------------*/

static void sid_command(int argc, const char **argv)
{
  /*** ignore System IDentifiers (SIDs) for now ***/
}

/*---------------------------------------------------------------------------*/

static void subscribe_command(int argc, const char **argv)
{

  char pattern[1024];
  int a;
  int changed;
  int subscribed;
  struct group *p;

  subscribed = (argv[0][0] == 'S' || argv[0][0] == 's');
  if (argc < 2) {
    if (!set_position(0))
      return;
    argc = 2;
    argv[1] = current_group->name + NEWSGROUPSPREFIXLENGTH;
  } else {
    if (!groups)
      load_groups();
  }
  changed = 0;
  for (a = 1; a < argc; a++) {
    strcpy(pattern, NEWSGROUPSPREFIX);
    strcat(pattern, argv[a]);
    for (p = groups; p; p = p->next) {
      if (stringcasematch(p->name, pattern)) {
	if (p->subscribed != subscribed) {
	  p->subscribed = subscribed;
	  if (!changed) {
	    if (subscribed)
	      printf("Subscribing to");
	    else
	      printf("Unsubscribing from");
	  }
	  changed = 1;
	  printf(" %s", p->name + NEWSGROUPSPREFIXLENGTH);
	}
      }
    }
  }
  if (changed) {
    putchar('\n');
    store_groups();
  }
}

/*---------------------------------------------------------------------------*/

static void version_command(int argc, const char **argv)
{

  int art = 0;
  int grp = 0;
  int sub = 0;
  int unr = 0;
  struct group *p;

  if (!groups)
    load_groups();

  for (p = groups; p; p = p->next) {
    grp++;
    if (p->subscribed)
      sub++;
    art += (p->high - p->low + 1);
    unr += p->unread;
  }

  printf("DK5SG-BBS  Revision: %s %s\n", revision.number, revision.date);
  printf("\n");
  printf("Headers:              %8s\n", headers);
  printf("Maxage:               %8d\n", maxage);
  printf("\n");
  printf("Groups:               %8d\n", grp);
  printf("Subscribed-to groups: %8d\n", sub);
  printf("Articles:             %8d\n", art);
  printf("Unread articles:      %8d\n", unr);
  printf("\n");
  printf("Current group:        %8s\n", current_group ?
	 current_group->name + NEWSGROUPSPREFIXLENGTH :
	 "NONE");
  if (current_group) {
    printf("Articles:             %8d\n", current_group->high - current_group->low + 1);
    printf("Unread articles:      %8d\n", current_group->unread);
    printf("Current article:      %8d\n", current_article - current_group->low + 1);
  }
}

/*---------------------------------------------------------------------------*/

static void help_command(int argc, const char **argv);

static const struct cmdtable cmdtable[] = {

  { "ALIAS",            alias_command,          1,      3,      USER },
  { "DIR",              dir_command,            1,      99999,  USER },
  { "DELETE",           delete_command,         2,      99999,  USER },
  { "DISCONNECT",       disconnect_command,     1,      1,      USER },
  { "F",                f_command,              2,      99999,  MBOX },
  { "GROUP",            group_command,          1,      2,      USER },
  { "HELP",             help_command,           1,      2,      USER },
  { "HEADERS",          headers_command,        1,      2,      USER },
  { "INFO",             info_command,           1,      1,      USER },
  { "LIST",             list_command,           1,      99999,  USER },
  { "MARK",             mark_command,           1,      99999,  USER },
  { "MAXAGE",           maxage_command,         1,      2,      USER },
  { "MYBBS",            mybbs_command,          2,      2,      USER },
  { "PIPE",             read_command,           2,      99999,  USER },
  { "PROMPT",           prompt_command,         1,      2,      USER },
  { "QUIT",             quit_command,           1,      1,      USER },
  { "READ",             read_command,           1,      99999,  USER },
  { "REPLY",            reply_command,          1,      3,      USER },
  { "RELOAD",           reload_command,         1,      1,      USER },
  { "SEND",             send_command,           2,      99999,  USER },
  { "SHELL",            shell_command,          1,      2,      USER },
  { "SUBSCRIBE",        subscribe_command,      1,      99999,  USER },
  { "UNMARK",           mark_command,           1,      99999,  USER },
  { "UNSUBSCRIBE",      subscribe_command,      1,      99999,  USER },
  { "VERSION",          version_command,        1,      1,      USER },
  { "WRITE",            read_command,           2,      99999,  USER },
  { "[",                sid_command,            1,      99999,  MBOX },

  { 0,                  0,                      0,      0,      USER }
};

/*---------------------------------------------------------------------------*/

static void help_command(int argc, const char **argv)
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

  if (!(fp = fopen(MAILCONFIGFILE, "r")))
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

static void parse_command_line(const char *line, int add_argc, const char **add_argv, int recursion_level);

static void execute_command(int argc, const char **argv, int recursion_level)
{

  const struct cmdtable *cmdp;
  int len;
  struct alias *p;

  if (recursion_level >= 20) {
    errors++;
    printf("Alias loop.\n");
    goto Done;
  }

  if (!(len = strlen(*argv))) {
    errors++;
    printf("Unknown command ''.  Type HELP for help.\n");
    goto Done;
  }

  /* Check for full match on alias */

  for (p = aliases; p; p = p->next) {
    if (!Strcasecmp(p->name, *argv)) {
      parse_command_line(p->value, argc - 1, argv + 1, recursion_level + 1);
      goto Done;
    }
  }

  /* Check for substring match on built-in command */

  for (cmdp = cmdtable; cmdp->name; cmdp++) {
    if (level >= cmdp->level && !Strncasecmp(cmdp->name, *argv, len)) {
      if (argc < cmdp->minargs) {
	errors++;
	printf("Too few arguments.  Type HELP %s for help.\n", cmdp->name);
      } else if (argc > cmdp->maxargs) {
	errors++;
	printf("Too many arguments.  Type HELP %s for help.\n", cmdp->name);
      } else {
	if (cmdp->name)
	  argv[0] = cmdp->name;
	(*cmdp->fnc)(argc, argv);
      }
      goto Done;
    }
  }

  /* Check for substring match on alias */

  for (p = aliases; p; p = p->next) {
    if (!Strncasecmp(p->name, *argv, len)) {
      parse_command_line(p->value, argc - 1, argv + 1, recursion_level + 1);
      goto Done;
    }
  }

  errors++;
  printf("Unknown command '%s'.  Type HELP for help.\n", *argv);

Done:
  if (level == MBOX && errors >= 3)
    exit(1);
}

/*---------------------------------------------------------------------------*/

static void parse_command_line(const char *line, int add_argc, const char **add_argv, int recursion_level)
{

#define STARTDELIM      "#$<["
#define ANYDELIM        "@>;"

  char buf[2048];
  char quoted[256];
  char *t;
  const char *argv[256];
  const char *f;
  const char **ap;
  int argc;
  int i;
  int quote;

  argc = 0;
  for (f = line, t = buf;;) {
    while (isspace(*f & 0xff))
      f++;
    if (!*f)
      break;
    argv[argc] = t;
    quoted[argc] = 0;
    if (*f == '"' || *f == '\'') {
      quoted[argc] = 1;
      quote = *f++;
      while (*f && *f != quote)
	*t++ = *f++;
      if (*f)
	f++;
    } else if (strchr(STARTDELIM ANYDELIM, *f)) {
      *t++ = *f++;
    } else {
      while (*f && !isspace(*f & 0xff) && !strchr(ANYDELIM, *f))
	*t++ = *f++;
    }
    *t++ = 0;
    argc++;
  }

  while (add_argc-- > 0)
    argv[argc++] = *add_argv++;

  for (ap = argv; argc > 0;) {
    for (i = 0; i < argc; i++) {
      if (!quoted[i] && !strcmp(ap[i], ";"))
	break;
    }
    execute_command(i, ap, recursion_level);
    argc -= i + 1;
    ap += i + 1;
  }
}

/*---------------------------------------------------------------------------*/

static void print_prompt(void)
{

  char line[1024];
  char *f;
  char *t;
  int i;
  int num;
  long gmt;
  struct tm *tm = 0;

  if (level == MBOX) {
    printf(">\n");
    return;
  }
  t = line;
  f = prompt;
  while (*f) {
    if (*f == '\\') {
      f++;
      switch (*f) {
      case 0:
	break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
	num = 0;
	for (i = 0; i < 3; i++) {
	  if (*f < '0' || *f > '7')
	    break;
	  num = 8 * num + *f++ - '0';
	}
	*t++ = (char) num;
	break;
      case 'c':
	f++;
	sprintf(t, "%d", current_group ?
		current_article - current_group->low + 1 :
		0);
	while (*t)
	  t++;
	break;
      case 'd':
	f++;
	if (!tm) {
	  gmt = time(0);
	  tm = localtime(&gmt);
	}
	sprintf(t, "%02d-%.3s-%02d",
		tm->tm_mday,
		monthnames + 3 * tm->tm_mon,
		tm->tm_year % 100);
	while (*t)
	  t++;
	break;
      case 'h':
	f++;
	strcpy(t, myhostname);
	while (*t)
	  t++;
	break;
      case 'n':
	f++;
	*t++ = '\n';
	break;
      case 't':
	f++;
	if (!tm) {
	  gmt = time(0);
	  tm = localtime(&gmt);
	}
	sprintf(t, "%02d:%02d", tm->tm_hour, tm->tm_min);
	while (*t)
	  t++;
	break;
      case 'u':
	f++;
	strcpy(t, user.name);
	while (*t)
	  t++;
	break;
      case 'w':
	f++;
	strcpy(t, current_group ?
	       current_group->name + NEWSGROUPSPREFIXLENGTH :
	       "NONE");
	while (*t)
	  t++;
	break;
      default:
	*t++ = *f++;
	break;
      }
    } else {
      *t++ = *f++;
    }
  }
  *t = 0;
  printf("%s", line);
}

/*---------------------------------------------------------------------------*/

static void bbs(void)
{

  char line[1024];
  FILE *fp;

  if (level != MBOX)
    printf("DK5SG-BBS  Revision: %s   Type HELP for help.\n", revision.number);

  change_alias("BYE", "QUIT");
  change_alias("CHECK", "DIR");
  change_alias("ERASE", "DELETE");
  change_alias("EXIT", "QUIT");
  change_alias("KILL", "DELETE");
  change_alias("PRINT", "READ");
  change_alias("RESPOND", "REPLY");
  change_alias("SB", "SEND");
  change_alias("SP", "SEND");
  change_alias("STATUS", "VERSION");
  change_alias("TYPE", "READ");
  change_alias(">", "WRITE");
  change_alias("?", "HELP");
  change_alias("!", "SHELL");
  change_alias("|", "PIPE");

  change_alias("MAIL", "SHELL mailx");
  change_alias("MORE", "PIPE more");
  change_alias("NEWNEWS", "RELOAD; DIR");
  change_alias("SKIPGROUP", "MARK *; LIST");

  sprintf(line, "%s/%s", user.dir, BBSRCFILE);
  if ((fp = fopen(line, "r"))) {
    while (fgets(line, sizeof(line), fp)) parse_command_line(line, 0, 0, 0);
    fclose(fp);
  }
  if (do_forward) {
    connect_bbs();
    wait_for_prompt();
  }
  if (level == MBOX) printf("[THEBOX-1.8-H$]\n");
  if (do_forward) wait_for_prompt();
  for (;;) {
    if (do_forward)
      strcpy(line, "f>");
    else {
      print_prompt();
      getstring(line);
    }
    parse_command_line(line, 0, 0, 0);
    do_forward = 0;
    if (stopped) {
      puts("\n*** Interrupt ***");
      stopped = 0;
    }
  }
}

/*---------------------------------------------------------------------------*/

static void doexport(void)
{

  char fname[1024];
  int fdlock;

  sprintf(fname, FWDLOCKFILE "%s", user.name);
  if ((fdlock = lock_file(fname, 1)) >= 0) {
    forward_news();
    close(fdlock);
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char buf[1024];
  char *cp;
  char *sysname = 0;
  FILE *fp;
  int c;
  int err_flag = 0;
  struct passwd *pw;

  if (!*CTLINND_PROG ||
      !*NEWS_DIR ||
      !*RNEWS_PROG ||
      !*SENDMAIL_PROG ||
      !*UUCP_DIR)
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

  if (!(fp = fopen(BBSCONFIGFILE, "r")))
    halt();
  while (fgets(buf, sizeof(buf), fp)) {
    get_header_value("Hostname:", 0, buf, myhostname);
    get_header_value("Domain:", 0, buf, mydomain);
  }
  fclose(fp);
  if (!*myhostname || !*mydomain)
    halt();

  while ((c = getopt(argc, argv, "ef:opw:")) != EOF)
    switch (c) {
    case 'e':
      if (getuid()) {
	puts("The 'e' option is for Store&Forward use only.");
	exit(1);
      }
      export = 1;
      break;
    case 'f':
      if (getuid()) {
	puts("The 'f' option is for Store&Forward use only.");
	exit(1);
      }
      sysname = optarg;
      do_forward = 1;
      break;
    case 'o':
      output_only = 1;
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
  if (export && !do_forward) err_flag = 1;
  if (optind < argc) err_flag = 1;
  if (err_flag) {
    puts("usage: bbs [-w seconds] [-f system [-e]] [-o] [-p]");
    exit(1);
  }

  mkdir(LOCKDIR, 0755);

  if (export) {
    if (!(user.name = strdup(sysname))) halt();
    level = MBOX;
  } else {
    pw = do_forward ? getpwnam(sysname) : getpwuid(getuid());
    if (!pw) halt();
    if (!(user.name = strdup(pw->pw_name))) halt();
    user.uid = (int) pw->pw_uid;
    user.gid = (int) pw->pw_gid;
    if (!(user.dir = strdup(pw->pw_dir))) halt();
    if (!pw->pw_shell || !*pw->pw_shell) pw->pw_shell = "/bin/sh";
    if (!(user.shell = strdup(pw->pw_shell))) halt();
    endpwent();
    if (!user.uid) level = ROOT;
    if (connect_addr(user.name, buf, buf)) level = MBOX;
  }

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

  if (export) {
    doexport();
  } else {
    bbs();
  }

  return 0;
}
