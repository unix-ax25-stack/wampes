/* Bulletin Board System */

static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/bbs.c,v 2.30 1991/12/18 05:54:11 deyke Exp $";

#define _HPUX_SOURCE

#include <sys/types.h>

#include <stdio.h>

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termio.h>
#include <time.h>
#include <unistd.h>

extern char *optarg;
extern char *sys_errlist[];
extern int optind;

#define __ARGS(x) x

#include "../src/buildsaddr.h"
#include "bbs.h"

#define SECONDS     (1L)
#define MINUTES     (60L*SECONDS)
#define HOURS       (60L*MINUTES)
#define DAYS        (24L*HOURS)

#define USER        0
#define MBOX        1
#define ROOT        2

#define BBS         0
#define RMAIL       1
#define RNEWS       2

struct revision {
  char number[16];
  char date[16];
  char time[16];
  char author[16];
  char state[16];
} revision;

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

struct utsname utsname;

static char *MYHOSTNAME;
static char *myhostname;
static char mydesc[80];
static char prompt[1024] = "bbs> ";
static int debug;
static int doforward;
static int errors;
static int fdindex;
static int fdlock = -1;
static int fdseq;
static int level;
static int mode = BBS;
static int packetcluster;
static struct user user;
static volatile int stopped;

/* In bbs.c: */
static void errorstop(int line);
static char *strupc(char *s);
static char *strlwc(char *s);
static int Strcasecmp(const char *s1, const char *s2);
static int Strncasecmp(const char *s1, const char *s2, int n);
static char *strtrim(char *s);
static const char *strcasepos(const char *str, const char *pat);
static char *getstring(char *s);
static char *timestr(long gmt);
static int callvalid(const char *call);
static void make_parent_directories(const char *filename);
static char *getfilename(int mesg);
static void get_seq(void);
static void put_seq(void);
static void wait_for_prompt(void);
static void lock(void);
static void unlock(void);
static int get_index(int n, struct index *index);
static int read_allowed(const struct index *index);
static char *get_user_from_path(char *path);
static char *get_host_from_path(char *path);
static int calc_crc(const char *str);
static int hash_bid(const char *bid, int store);
static int msg_uniq(const char *bid, const char *mid);
static void send_to_bbs(struct mail *mail);
static void send_to_mail(struct mail *mail);
static void send_to_news(struct mail *mail);
static void fix_address(char *addr);
static struct mail *alloc_mail(void);
static void free_mail(struct mail *mail);
static void route_mail(struct mail *mail);
static void append_line(struct mail *mail, char *line);
static void get_header_value(const char *name, char *line, char *value);
static char *get_host_from_header(char *line);
static int host_in_header(char *fname, char *host);
static int mail_pending(void);
static void delete_command(int argc, char **argv);
static void dir_print(struct dir_entry *p);
static void dir_command(int argc, char **argv);
static void disconnect_command(int argc, char **argv);
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
static char *connect_addr(char *host);
static void connect_bbs(void);
static void parse_command_line(char *line);
static void bbs(void);
static void interrupt_handler(int sig, int code, struct sigcontext *scp);
static void alarm_handler(int sig, int code, struct sigcontext *scp);
static void trap_signal(int sig, void (*handler )());
static void rmail(void);
static void rnews(void);
int main(int argc, char **argv);

/*---------------------------------------------------------------------------*/

static const struct cmdtable {
  char *name;
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

  (char *) 0,   unknown_command,        0,      USER
};

/*---------------------------------------------------------------------------*/

static void errorstop(int line)
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

static char *strupc(char *s)
{
  char *p;

  for (p = s; *p = _toupper(uchar(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static char *strlwc(char *s)
{
  char *p;

  for (p = s; *p = _tolower(uchar(*p)); p++) ;
  return s;
}

/*---------------------------------------------------------------------------*/

static int Strcasecmp(const char *s1, const char *s2)
{
  while (_tolower(uchar(*s1)) == _tolower(uchar(*s2++)))
    if (!*s1++) return 0;
  return _tolower(uchar(*s1)) - _tolower(uchar(s2[-1]));
}

/*---------------------------------------------------------------------------*/

static int Strncasecmp(const char *s1, const char *s2, int n)
{
  while (--n >= 0 && _tolower(uchar(*s1)) == _tolower(uchar(*s2++)))
    if (!*s1++) return 0;
  return n < 0 ? 0 : _tolower(uchar(*s1)) - _tolower(uchar(s2[-1]));
}

/*---------------------------------------------------------------------------*/

static char *strtrim(char *s)
{
  char *p;

  for (p = s; *p; p++) ;
  while (--p >= s && isspace(uchar(*p))) ;
  p[1] = '\0';
  return s;
}

/*---------------------------------------------------------------------------*/

static const char *strcasepos(const char *str, const char *pat)
{
  const char *s, *p;

  for (; ; str++)
    for (s = str, p = pat; ; ) {
      if (!*p) return str;
      if (!*s) return 0;
      if (_tolower(uchar(*s++)) != _tolower(uchar(*p++))) break;
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
      return (p == s) ? 0 : s;
    case '\0':
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
	       "JanFebMarAprMayJunJulAugSepOctNovDec" + 3 * tm->tm_mon,
	       tm->tm_year % 100,
	       tm->tm_hour,
	       tm->tm_min);
  return buf;
}

/*---------------------------------------------------------------------------*/

static int callvalid(const char *call)
{
  int d, l;

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
  setresgid(user.gid, user.gid, 1);
  setresuid(user.uid, user.uid, 0);
  fdseq = open(fname, O_RDWR | O_CREAT, 0644);
  setresuid(0, 0, 0);
  setresgid(1, 1, 1);
  if (fdseq < 0) halt();
  if (lockf(fdseq, F_TLOCK, 0)) {
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
  if ((n = sprintf(buf, "%d\n", user.seq)) < 2) halt();
  if (lseek(fdseq, 0L, SEEK_SET)) halt();
  if (write(fdseq, buf, (unsigned) n) != n) halt();
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

static void lock(void)
{
  if (fdlock < 0) {
    if ((fdlock = open("lock", O_RDWR | O_CREAT, 0644)) < 0) halt();
  }
  if (lockf(fdlock, F_LOCK, 0)) halt();
}

/*---------------------------------------------------------------------------*/

static void unlock(void)
{
  if (lockf(fdlock, F_ULOCK, 0)) halt();
}

/*---------------------------------------------------------------------------*/

static int get_index(int n, struct index *index)
{

  int i1, i2, im;
  long pos;

  i1 = 0;
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (read(fdindex, (char *) index, sizeof(struct index )) != sizeof(struct index )) return 0;
  if (n == index->mesg) return 1;
  if (n < index->mesg) return 0;

  if ((pos = lseek(fdindex, (long) (-sizeof(struct index )), SEEK_END)) < 0) halt();
  i2 = (int) (pos / sizeof(struct index));
  if (read(fdindex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
  if (n == index->mesg) return 1;
  if (n > index->mesg) return 0;

  while (i1 + 1 < i2) {
    im = (i1 + i2) / 2;
    if (lseek(fdindex, (long) (im * sizeof(struct index )), SEEK_SET) < 0) halt();
    if (read(fdindex, (char *) index, sizeof(struct index )) != sizeof(struct index )) halt();
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
  *cp = '\0';
  cp = strrchr(tmp, '!');
  return cp ? (cp + 1) : tmp;
}

/*---------------------------------------------------------------------------*/

/* Calculate crc16 for a null terminated string (used for hashing) */

static int calc_crc(const char *str)
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

  int crc;

  crc = 0;
  while (*str)
    crc = ((crc >> 8) & 0xff) ^ crc_table[(crc ^ *str++) & 0xff];
  return crc;
}

/*---------------------------------------------------------------------------*/

static int hash_bid(const char *bid, int store)
{

#define HASH_SIZE 4001

  int hash_index;
  static struct strlist *hash_table[HASH_SIZE];
  struct strlist *p;

  hash_index = (calc_crc(bid) & 0x7fff) % HASH_SIZE;
  for (p = hash_table[hash_index]; p && strcmp(bid, p->str); p = p->next) ;
  if (!p && store) {
    p = malloc(sizeof(*p) + strlen(bid));
    strcpy(p->str, bid);
    p->next = hash_table[hash_index];
    hash_table[hash_index] = p;
  }
  return (p != 0);
}

/*---------------------------------------------------------------------------*/

static int msg_uniq(const char *bid, const char *mid)
{

  int n;
  long validdate;
  static long startpos;
  struct index *pi;
  struct index index_array[1000];

  validdate = time((long *) 0) - 90 * DAYS;
  if (lseek(fdindex, startpos, SEEK_SET) != startpos) halt();
  for (; ; ) {
    n = read(fdindex, index_array, sizeof(index_array)) / sizeof(struct index);
    if (n <= 0) break;
    for (pi = index_array; n; pi++, n--)
      if (pi->date >= validdate) hash_bid(pi->bid, 1);
  }
  startpos = lseek(fdindex, 0L, SEEK_CUR);
  return !hash_bid(bid, 0);
}

/*---------------------------------------------------------------------------*/

static void send_to_bbs(struct mail *mail)
{

  FILE *fp;
  struct index index;
  struct strlist *p;

  lock();
  if (msg_uniq(mail->bid, mail->mid)) {
    if (lseek(fdindex, (long) (-sizeof(struct index )), SEEK_END) < 0)
      index.mesg = 1;
    else {
      if (read(fdindex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
      index.mesg++;
    }
    index.date = mail->date;
    index.lifetime_h = (mail->lifetime + 1) >> 8;
    index.lifetime_l = (mail->lifetime + 1);
    strcpy(index.bid, mail->bid);
    strncpy(index.subject, mail->subject, LEN_SUBJECT);
    index.subject[LEN_SUBJECT] = '\0';
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
    if (lseek(fdindex, 0L, SEEK_END) < 0) halt();
    if (write(fdindex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
    if (index.mesg == user.seq + 1) {
      user.seq = index.mesg;
      put_seq();
    }
  }
  unlock();
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
    setresgid(1, 1, 1);
    setresuid(0, 0, 0);
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

static void send_to_news(struct mail *mail)
{

  FILE *fp;
  char *fromhost;
  int fd;
  int i;
  long mst;
  struct strlist *p;
  struct tm *tm;

  if ((fd = open("/usr/bin/rnews", O_RDONLY)) < 0) return;
  close(fd);
  switch (fork()) {
  case -1:
    halt();
  case 0:
    setresgid(1, 1, 1);
    setresuid(0, 0, 0);
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
      p = mail->head;
      while (p && p->str[0] == 'R' && p->str[1] == ':') p = p->next;
      if (p && p->str[0] == 'd' && p->str[1] == 'e' && p->str[2] == ' ')
	p = p->next;
      while (p && p->str[0] == '\0') p = p->next;
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

  char tmp[1024];
  char *p1, *p2;

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

static struct mail *alloc_mail(void)
{
  struct mail *mail;

  mail = (struct mail *) calloc(1, sizeof(struct mail ));
  mail->lifetime = -1;
  return mail;
}

/*---------------------------------------------------------------------------*/

static void free_mail(struct mail *mail)
{
  struct strlist *p;

  while (p = mail->head) {
    mail->head = p->next;
    free(p);
  }
  free(mail);
}

/*---------------------------------------------------------------------------*/

static void route_mail(struct mail *mail)
{

#define MidSuffix "@bbs.net"

  FILE *fp;
  int n;
  char *cp;
  char *s;
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

  /* Remove message delimiters */

  for (p = mail->head; p; p = p->next) {
    s = p->str;
    if (*s == '.' && !s[1]) *s = '\0';
    while (cp = strchr(s, '\004')) while (cp[0] = cp[1]) cp++;
    while (cp = strchr(s, '\032')) while (cp[0] = cp[1]) cp++;
    if (!Strncasecmp(s, "***end", 6)) *s = ' ';
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

static void append_line(struct mail *mail, char *line)
{
  struct strlist *p;

  p = malloc(sizeof(struct strlist) + strlen(line));
  p->next = 0;
  strcpy(p->str, line);
  if (!mail->head)
    mail->head = p;
  else
    mail->tail->next = p;
  mail->tail = p;
}

/*---------------------------------------------------------------------------*/

static void get_header_value(const char *name, char *line, char *value)
{
  char *p1, *p2;

  while (*name)
    if (_tolower(uchar(*name++)) != _tolower(uchar(*line++))) return;
  while (isspace(uchar(*line))) line++;
  while ((p1 = strchr(line, '<')) && (p2 = strrchr(p1, '>'))) {
    *p2 = '\0';
    line = p1 + 1;
  }
  strcpy(value, line);
}

/*---------------------------------------------------------------------------*/

static char *get_host_from_header(char *line)
{

  char *p, *q;
  static char buf[1024];

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

static int host_in_header(char *fname, char *host)
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

static int mail_pending(void)
{

  DIR *dirp;
  char dirname[1024];
  long curr_time;
  static int have_mail;
  static long next_time;
  struct dirent *dp;

  curr_time = time((long *) 0);
  if (next_time > curr_time) return have_mail;
  next_time = curr_time + 5 * MINUTES;
  have_mail = 0;
  sprintf(dirname, "/usr/spool/uucp/%s", user.name);
  if (dirp = opendir(dirname)) {
    for (dp = readdir(dirp); dp; dp = readdir(dirp))
      if (*dp->d_name == 'C') {
	have_mail = 1;
	break;
      }
    closedir(dirp);
  }
  return have_mail;
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
	if (lseek(fdindex, (long) (-sizeof(struct index )), SEEK_CUR) < 0) halt();
	if (write(fdindex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
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
    n = read(fdindex, (char *) (pi = index), 1000 * sizeof(struct index )) / sizeof(struct index );
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

static void disconnect_command(int argc, char **argv)
{
  puts("Disconnecting...");
  kill(0, 1);
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void f_command(int argc, char **argv)
{

  FILE *fp;
  char buf[1024];
  int c;
  int do_not_exit;
  int lifetime;
  struct index index;
  struct tm *tm;

  do_not_exit = doforward;
  if (!get_index(user.seq, &index))
    if (lseek(fdindex, 0L, SEEK_SET)) halt();
  while (read(fdindex, (char *) &index, sizeof(struct index )) == sizeof(struct index )) {
    if (!index.deleted                &&
	index.mesg > user.seq         &&
	!calleq(index.at, myhostname) &&
	!host_in_header(getfilename(index.mesg), user.name)) {
      if (mail_pending()) exit(0);
      do_not_exit = 1;
      lifetime = ((index.lifetime_h << 8) & 0xff00) + (index.lifetime_l & 0xff) - 1;
      if (lifetime != -1)
	printf("S %s%s%s < %s%s%s # %d\n",
	       index.to,
	       *index.at ? " @ " : "",
	       index.at,
	       index.from,
	       *index.bid ? " $" : "",
	       index.bid,
	       lifetime);
      else
	printf("S %s%s%s < %s%s%s\n",
	       index.to,
	       *index.at ? " @ " : "",
	       index.at,
	       index.from,
	       *index.bid ? " $" : "",
	       index.bid);
      if (!getstring(buf)) exit(1);
      switch (_tolower(uchar(*buf))) {
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
		 MYHOSTNAME,
		 mydesc);
	  if (!(fp = fopen(getfilename(index.mesg), "r"))) halt();
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
  if (lseek(fdindex, (long) (-sizeof(struct index )), SEEK_END) >= 0) {
    for (; ; ) {
      if (stopped) return;
      if (read(fdindex, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
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
      if (lseek(fdindex, -2L * sizeof(struct index ), SEEK_CUR) < 0) break;
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
    while (isspace(uchar(*p))) p++;
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
  int i;
  struct mail *mail;

  mail = alloc_mail();
  *at = *path = '\0';
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
  if (packetcluster || level != MBOX) puts("Enter message: (terminate with ^Z or ***END)");
  for (; ; ) {
    if (!getstring(line)) exit(1);
    if (stopped) {
      free_mail(mail);
      return;
    }
    if (*line == '\032') break;
    if (!Strncasecmp(line, "***end", 6)) break;
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

static void shell_command(int argc, char **argv)
{

  char command[2048];
  int i;
  int oldstopped;
  pid_t pid;
  int status;
  long oldmask;

  switch (pid = fork()) {
  case -1:
    puts("Sorry, cannot fork.");
    break;
  case 0:
    setresgid(user.gid, user.gid, user.gid);
    setresuid(user.uid, user.uid, user.uid);
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
    n = read(fdindex, (char *) (pi = index), 1000 * sizeof(struct index )) / sizeof(struct index );
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

  char *tempfile = "index.tmp";
  int f;
  int wflag;
  long validdate;
  struct index index;

  validdate = time((long *) 0) - 90 * DAYS;
  if ((f = open(tempfile, O_WRONLY | O_CREAT | O_EXCL, 0644)) < 0) halt();
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  wflag = 0;
  while (read(fdindex, (char *) & index, sizeof(struct index )) == sizeof(struct index )) {
    wflag = 1;
    if (!index.deleted || *index.bid && index.date >= validdate) {
      if (write(f, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
      wflag = 0;
    }
  }
  if (wflag)
    if (write(f, (char *) & index, sizeof(struct index )) != sizeof(struct index )) halt();
  if (close(f)) halt();
  if (close(fdindex)) halt();
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

static void xscreen_command(int argc, char **argv)
{

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
  struct termio curr_termio, prev_termio;
  unsigned int indexarraysize;

  if (fstat(fdindex, &statbuf)) halt();
  indexarraysize = statbuf.st_size;
  indexarrayentries = indexarraysize / sizeof(struct index );
  if (!indexarrayentries) return;
  if (!(indexarray = (struct index *) malloc(indexarraysize))) halt();
  if (lseek(fdindex, 0L, SEEK_SET)) halt();
  if (read(fdindex, (char *) indexarray, indexarraysize) != indexarraysize) halt();

  ioctl(0, TCGETA, &prev_termio);
  curr_termio = prev_termio;
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
      if (unlink(getfilename(pi->mesg))) halt();
      pi->deleted = 1;
      if (lseek(fdindex, (long) ((pi - indexarray) * sizeof(struct index )), SEEK_SET) < 0) halt();
      if (write(fdindex, (char *) pi, sizeof(struct index )) != sizeof(struct index )) halt();

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
      sprintf(buf, "vi %s", getfilename(pi->mesg));
      ioctl(0, TCSETA, &prev_termio);
      system(buf);
      ioctl(0, TCSETA, &curr_termio);
      cmd = 'r';
      break;

    case 'q':
      ioctl(0, TCSETA, &prev_termio);
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
}

#undef Invalid

/*---------------------------------------------------------------------------*/

static char *connect_addr(char *host)
{

  FILE *fp;
  char *addr;
  char *h;
  char *p;
  static char line[1024];

  addr = 0;
  if (fp = fopen(CONFIGFILE, "r")) {
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

  char *argv[256];
  char *delim = "<>@$#[";
  char *f;
  char *t;
  char buf[2048];
  int argc;
  int len;
  int quote;
  const struct cmdtable *cmdp;

  argc = 0;
  memset(argv, 0, sizeof(argv));
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
	level >= cmdp->level && !Strncasecmp(cmdp->name, *argv, len)) {
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
  if (fp = fopen(line, "r")) {
    while (fgets(line, sizeof(line), fp)) parse_command_line(line);
    fclose(fp);
  }
  if (doforward) {
    if (mail_pending()) exit(0);
    connect_bbs();
    wait_for_prompt();
  }
  if (level == MBOX) printf("[THEBOX-1.6-$]\n");
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

static void interrupt_handler(int sig, int code, struct sigcontext *scp)
{
  stopped = 1;
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

static void alarm_handler(int sig, int code, struct sigcontext *scp)
{
  puts("\n*** Timeout ***");
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void trap_signal(int sig, void (*handler)())
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

static void rmail(void)
{

  char line[1024];
  int state = 0;
  struct mail *mail;

  mail = alloc_mail();
  strcpy(mail->to, "alle");
  if (scanf("%*s%s", mail->from) != 1) halt();
  if (!getstring(line)) halt();
  while (getstring(line))
    switch (state) {
    case 0:
      get_header_value("Newsgroups:",  line, mail->to);
      get_header_value("Subject:",     line, mail->subject);
      get_header_value("Bulletin-ID:", line, mail->bid);
      get_header_value("Message-ID:",  line, mail->mid);
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

static void rnews(void)
{

  char line[1024];
  int n;
  int state = 0;
  struct mail *mail;

  while (fgets(line, sizeof(line), stdin)) {
    mail = alloc_mail();
    strcpy(mail->to, "alle@alle");
    if (strncmp(line, "#! rnews ", 9)) halt();
    n = atoi(line + 9);
    while (n > 0) {
      if (!fgets(line, (n < sizeof(line)) ? (n + 1) : (int) sizeof(line), stdin)) halt();
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

int main(int argc, char **argv)
{

  char *dir = WRKDIR;
  char *sysname;
  char buf[1024];
  int c;
  int err_flag = 0;
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

  while ((c = getopt(argc, argv, "df:mnpw:")) != EOF)
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
  MYHOSTNAME = strdup(myhostname);
  strupc(MYHOSTNAME);

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
  case RMAIL:
    rmail();
    break;
  case RNEWS:
    rnews();
    break;
  }
  return 0;
}

