#ifndef __lint
static const char rcsid[] = "@(#) $Id: netupds.c,v 1.51 1999/10/08 03:33:35 deyke Exp $";
#endif

/* Net Update Client/Server */

#define DEBUG   0

#include <sys/types.h>

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

extern char *optarg;
extern int optind;

#ifndef S_ISLNK
#define S_ISLNK(mode)   (((mode) & (S_IFMT)) == (S_IFLNK))
#endif

#include "buildsaddr.h"
#include "calc_crc.h"
#include "configure.h"
#include "lockfile.h"
#include "md5.h"
#include "strdup.h"
#include "strmatch.h"

#define DIGESTSIZE      16              /* MD5 digest size */
#define HASHSIZE        499             /* Hash table size */
#define TIMEOUT         (6*3600)        /* Client/Server timeout (6 hours) */
#define UMASK           022             /* Client/Server umask */
#define VERSION         1               /* Client version */

/* Flag bits */

#define USE_GZIP        0x00000002      /* Otherwise use compress */
#define VERSIONMASK     0x0000ff00      /* Reserved for version information */

enum e_scanmode {
  SCAN_CLIENT,
  SCAN_MASTER,
  SCAN_MIRROR
};

enum e_action {
  ACT_DELETE,
  ACT_CREATE,
  ACT_UPDATE
};

struct s_fileentry {
  unsigned short mode;          /* File mode */
  char digest[DIGESTSIZE];      /* MD5 digest */
};

struct s_file {
  struct s_file *next;          /* Linked list pointer */
  struct s_file *hashlink;      /* Hash list pointer */
  struct s_fileentry master;    /* Master file info */
  struct s_fileentry mirror;    /* Mirror file info */
  struct s_fileentry client;    /* Client file info */
  char name[1];                 /* File name, variable length, must be last in
				   struct !!! */
};

static const char *notifyuser = "root";
static const char *server = "db0sao";
static const char *sourcedir = "/tcp";
static const char *wampesdir = "/tcp";

static char *inpptr;
static char inpbuf[1024];
static char mirrordir[1024];
static char outbuf[1024];
static const char *client;
static int do_make = 1;
static int do_unix_networking;
static int fdinp;
static int fdlock;
static int fdout;
static int inpcnt;
static int outcnt;
static int received;
static int xmitted;
static struct s_file *Filehead;
static struct s_file *Hashtable[HASHSIZE];

/*---------------------------------------------------------------------------*/

static char *include_table[] =
{

  "*.R",
  "ChangeLog",
  "Makefile",
  "NeXT",
  "NeXT/*.[ch]",
  "NeXT/*.sh",
  "NeXT/Makefile",
  "NeXT/README.NeXT",
  "NeXT/uname-next",
  "README",
  "aos",
  "aos/*.[ch]",
  "aos/Makefile",
  "bbs",
  "bbs/*.[ch]",
  "bbs/Makefile",
  "bbs/bbs.help",
  "cc",
  "convers",
  "convers/*.[ch]",
  "convers/Makefile",
  "doc",
  "doc/?*.*",
  "domain.txt",
  "examples",
  "examples/?*.*",
  "hosts",
  "lib",
  "lib/*.[ch]",
  "lib/Makefile",
  "lib/configure",
  "src",
  "src/*.[ch]",
  "src/Makefile",
  "src/linux_include",
  "src/linux_include/netinet",
  "src/linux_include/netinet/*.h",
  "util",
  "util/*.[ch]",
  "util/Makefile",

  0
};

static char *dynamic_include_table[3];

static char *exclude_table[] =
{

  "lib/configure.h",

  0
};

/*---------------------------------------------------------------------------*/

#if DEBUG

static char *digest_to_hex(const char *digest)
{

  int i;
  static char buffer[2 * DIGESTSIZE + 1];

  for (i = 0; i < DIGESTSIZE; i++) {
    sprintf(buffer + 2 * i, "%x%x", (digest[i] >> 4) & 0xf, digest[i] & 0xf);
  }
  return buffer;
}

static void dump_file(const struct s_file *p)
{
  printf("File name:     %s\n", p->name);
  printf("Master mode:   %6o\n", p->master.mode);
  printf("Mirror mode:   %6o\n", p->mirror.mode);
  printf("Client mode:   %6o\n", p->client.mode);
  printf("Master digest: %s\n", digest_to_hex(p->master.digest));
  printf("Mirror digest: %s\n", digest_to_hex(p->mirror.digest));
  printf("Client digest: %s\n", digest_to_hex(p->client.digest));
  printf("\n");
}

#endif

/*---------------------------------------------------------------------------*/

static void quit(void)
{

  char *p;
  int n;

  printf("Received %d bytes, sent %d bytes\n", received, xmitted);
  p = outbuf;
  while (outcnt > 0) {
    n = write(fdout, p, outcnt);
    if (n <= 0) {
      break;
    }
    p += n;
    outcnt -= n;
  }
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void syscallerr_handler(const char *mesg, int line)
{
  char buf[1024];

  sprintf(buf, "Line %d: %s", line, mesg);
  perror(buf);
  quit();
}

#define syscallerr(mesg) \
	syscallerr_handler(mesg, __LINE__)

/*---------------------------------------------------------------------------*/

static void usererr_handler(const char *mesg, int line)
{
  fprintf(stderr, "Line %d: %s\n", line, mesg);
  quit();
}

#define usererr(mesg) \
	usererr_handler(mesg, __LINE__)

/*---------------------------------------------------------------------------*/

#define protoerr() \
	usererr_handler("Protocol violation", __LINE__)

/*---------------------------------------------------------------------------*/

#define FTYPE(x) \
	((x) & S_IFMT)

/*---------------------------------------------------------------------------*/

static void flushoutbuf(void)
{

  char *p;
  int n;

  p = outbuf;
  while (outcnt > 0) {
    n = write(fdout, p, outcnt);
    if (n < 0) {
      syscallerr("write");
    }
    if (!n) {
      usererr("write() returned 0");
    }
    p += n;
    outcnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

static void writechar(int value)
{
  outbuf[outcnt++] = value;
  xmitted++;
  if (outcnt >= sizeof(outbuf)) {
    flushoutbuf();
  }
}

/*---------------------------------------------------------------------------*/

static void writeint(int value)
{
  writechar(value >> 24);
  writechar(value >> 16);
  writechar(value >> 8);
  writechar(value);
}

/*---------------------------------------------------------------------------*/

static void writebuf(const char *buf, int cnt)
{
  int n;

  while (cnt > 0) {
    n = sizeof(outbuf) - outcnt;
    if (n > cnt) {
      n = cnt;
    }
    memcpy(outbuf + outcnt, buf, n);
    xmitted += n;
    outcnt += n;
    buf += n;
    cnt -= n;
    if (outcnt >= sizeof(outbuf)) {
      flushoutbuf();
    }
  }
}

/*---------------------------------------------------------------------------*/

static void fillinpbuf(void)
{
  flushoutbuf();
  inpcnt = read(fdinp, inpptr = inpbuf, sizeof(inpbuf));
  if (inpcnt < 0) {
    syscallerr("read");
  }
  if (!inpcnt) {
    usererr("read: End of file");
  }
}

/*---------------------------------------------------------------------------*/

static int readchar(void)
{
  if (inpcnt <= 0) {
    fillinpbuf();
  }
  inpcnt--;
  received++;
  return (*inpptr++ & 0xff);
}

/*---------------------------------------------------------------------------*/

static int readint(void)
{
  int value;

  value = readchar() << 24;
  value |= readchar() << 16;
  value |= readchar() << 8;
  value |= readchar();
  return value;
}

/*---------------------------------------------------------------------------*/

static void readbuf(char *buf, int cnt)
{
  int n;

  while (cnt > 0) {
    if (inpcnt <= 0) {
      fillinpbuf();
    }
    n = cnt < inpcnt ? cnt : inpcnt;
    memcpy(buf, inpptr, n);
    received += n;
    inpptr += n;
    buf += n;
    inpcnt -= n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

static void readclientname(void)
{

  char buf[1024];
  int i;

  for (i = 0;; i++) {
    if (i >= sizeof(buf)) {
      usererr("Client name too long");
    }
    if (!(buf[i] = readchar())) {
      break;
    }
    if (!isalnum(buf[i] & 0xff) && buf[i] != '-') {
      usererr("Bad character in client name");
    }
  }
  if (!*buf) {
    usererr("Null client name received");
  }
  client = strdup(buf);
}

/*---------------------------------------------------------------------------*/

static void generate_dynamic_include_table(void)
{

  char buf[1024];
  int n;

  n = 0;
  strcpy(buf, "net.rc.");
  strcat(buf, client);
  dynamic_include_table[n++] = strdup(buf);
}

/*---------------------------------------------------------------------------*/

static struct s_file *getfileptr(const char *filename)
{

  struct s_file **hp;
  struct s_file *p;

  hp = Hashtable + calc_crc_16(filename) % HASHSIZE;
  for (p = *hp; p; p = p->hashlink) {
    if (!strcmp(p->name, filename)) {
      return p;
    }
  }
  p = (struct s_file *) calloc(1, sizeof(struct s_file) + strlen(filename));
  if (!p) {
    syscallerr("malloc");
  }
  p->next = Filehead;
  Filehead = p;
  p->hashlink = *hp;
  *hp = p;
  strcpy(p->name, filename);
  return p;
}

/*---------------------------------------------------------------------------*/

static int filefilter(const char *filename)
{
  char **tp;

  for (tp = exclude_table; *tp; tp++) {
    if (stringmatch(filename, *tp)) {
      return 0;
    }
  }
  for (tp = include_table; *tp; tp++) {
    if (stringmatch(filename, *tp)) {
      return 1;
    }
  }
  for (tp = dynamic_include_table; *tp; tp++) {
    if (stringmatch(filename, *tp)) {
      return 1;
    }
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

static void getdigest(const char *filename, MD5_CTX * mdContext)
{

  char buf[8 * 1024];
  int fd;
  int len;

  if ((fd = open(filename, O_RDONLY, 0644)) < 0) {
    syscallerr(filename);
  }
  MD5Init(mdContext);
  while ((len = read(fd, buf, sizeof(buf))) > 0) {
    MD5Update(mdContext, (unsigned char *) buf, len);
  }
  MD5Final(mdContext);
  close(fd);
}

/*---------------------------------------------------------------------------*/

static void send_info(const char *filename, unsigned short filemode, const char *digest)
{

  char *cp;
  const char *fn;
  int flags;
  int lastpathlen;
  int len;
  static char lastpath[1024];

  flags = ((int) filemode >> 12) & 0xf;
  fn = filename;
  lastpathlen = strlen(lastpath);
  if (lastpathlen && !strncmp(fn, lastpath, lastpathlen)) {
    flags |= 0x40;
    fn += lastpathlen;
  }
  len = strlen(fn);
  if (len > 2) {
    if (!strcmp(fn + len - 2, ".c")) {
      flags |= 0x10;
      len -= 2;
    } else if (!strcmp(fn + len - 2, ".h")) {
      flags |= 0x20;
      len -= 2;
    }
  }
  if (len > 255) {
    usererr("File name too long");
  }
  writechar(flags);
  writechar(len);
  writebuf(fn, len);
  if (S_ISREG(filemode) || S_ISLNK(filemode)) {
    writebuf(digest, DIGESTSIZE);
  }
  strcpy(lastpath, filename);
  if ((cp = strrchr(lastpath, '/'))) {
    cp[1] = 0;
  } else {
    strcat(lastpath, "/");
  }
}

/*---------------------------------------------------------------------------*/

static void read_info(char *filename, unsigned short *filemodeptr, char *digest)
{

  char *cp;
  char *fn;
  int flags;
  int len;
  static char lastpath[1024];

  flags = readchar();
  if ((*filemodeptr = flags << 12)) {
    if (!(len = readchar())) {
      protoerr();
    }
    fn = filename;
    if (flags & 0x40) {
      for (cp = lastpath; *cp; cp++) {
	*fn++ = *cp;
      }
    }
    readbuf(fn, len);
    if (flags & 0x10) {
      strcpy(fn + len, ".c");
    } else if (flags & 0x20) {
      strcpy(fn + len, ".h");
    } else {
      fn[len] = 0;
    }
    switch (FTYPE(*filemodeptr)) {
    case S_IFDIR:
      break;
    case S_IFREG:
    case S_IFLNK:
      readbuf(digest, DIGESTSIZE);
      break;
    default:
      protoerr();
    }
    strcpy(lastpath, filename);
    if ((cp = strrchr(lastpath, '/'))) {
      cp[1] = 0;
    } else {
      strcat(lastpath, "/");
    }
  }
}

/*---------------------------------------------------------------------------*/

static void print_action(enum e_action action, unsigned short filemode, const char *filename)
{
  switch (action) {
  case ACT_DELETE:
    printf("Delete");
    break;
  case ACT_CREATE:
    printf("Create");
    break;
  case ACT_UPDATE:
    printf("Update");
    break;
  default:
    protoerr();
  }
  switch (FTYPE(filemode)) {
  case S_IFDIR:
    printf(" Directory");
    break;
  case S_IFREG:
    printf(" File");
    break;
  case S_IFLNK:
    printf(" Symlink");
    break;
  default:
    protoerr();
  }
  printf(" %s\n", filename);
}

/*---------------------------------------------------------------------------*/

static int doremove(const char *filename)
{

  char tempfilename[1024];
  int i;
  struct stat statbuf;

  if (lstat(filename, &statbuf)) {
    return 0;
  }
  if (!remove(filename)) {
    return 0;
  }
  strcpy(tempfilename, filename);
  for (i = 0; i < 16; i++) {
    strcat(tempfilename, "~");
    if (!rename(filename, tempfilename)) {
      return 0;
    }
  }
  return remove(filename);
}

/*---------------------------------------------------------------------------*/

static void update_mirror_from_master(struct s_file *p, const char *masterfilename, const char *mirrorfilename)
{

  char buf[8 * 1024];
  int fd1;
  int fd;
  int len;

  switch (FTYPE(p->master.mode)) {

  case 0:
    if (FTYPE(p->mirror.mode) && doremove(mirrorfilename)) {
      syscallerr(mirrorfilename);
    }
    break;

  case S_IFDIR:
    if (!S_ISDIR(p->mirror.mode)) {
      if (FTYPE(p->mirror.mode) && doremove(mirrorfilename)) {
	syscallerr(mirrorfilename);
      }
      if (mkdir(mirrorfilename, 0755)) {
	syscallerr(mirrorfilename);
      }
    }
    break;

  case S_IFREG:
    if (!S_ISREG(p->mirror.mode) ||
	memcmp(p->master.digest, p->mirror.digest, DIGESTSIZE)) {
      if (FTYPE(p->mirror.mode) && doremove(mirrorfilename)) {
	syscallerr(mirrorfilename);
      }
      if ((fd = open(masterfilename, O_RDONLY, 0644)) < 0) {
	syscallerr(masterfilename);
      }
      if ((fd1 = open(mirrorfilename, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
	syscallerr(mirrorfilename);
      }
      while ((len = read(fd, buf, sizeof(buf))) > 0) {
	if (write(fd1, buf, len) != len) {
	  syscallerr("write");
	}
      }
      if (close(fd)) {
	syscallerr("close");
      }
      if (close(fd1)) {
	syscallerr("close");
      }
    }
    break;

  case S_IFLNK:
    if (!S_ISLNK(p->mirror.mode) ||
	memcmp(p->master.digest, p->mirror.digest, DIGESTSIZE)) {
      if (FTYPE(p->mirror.mode) && doremove(mirrorfilename)) {
	syscallerr(mirrorfilename);
      }
      if ((len = readlink(masterfilename, buf, sizeof(buf))) < 0) {
	syscallerr(masterfilename);
      }
      buf[len] = 0;
      umask(0333);
      if (symlink(buf, mirrorfilename)) {
	syscallerr(mirrorfilename);
      }
      umask(UMASK);
    }
    break;

  default:
    protoerr();

  }
  p->mirror = p->master;
}

/*---------------------------------------------------------------------------*/

static void update_mirror_from_rcs(struct s_file *p, const char *master, const char *mirrorfilename)
{

  FILE *fp;
  MD5_CTX mdContext;
  char *cp;
  char buf[8 * 1024];
  char major[1024];
  char rcsfilename[1024];
  int len;
  int minor;

  strcpy(rcsfilename, master);
  strcat(rcsfilename, "/");
  strcat(rcsfilename, p->name);
  cp = strrchr(rcsfilename, '/') + 1;
  strcpy(buf, cp);
  strcpy(cp, "RCS/");
  strcpy(cp + 4, buf);
  strcat(cp, ",v");

  if (!(fp = fopen(rcsfilename, "r"))) {
    goto Fail;
  }
  len = fscanf(fp, "head %s", major);
  fclose(fp);
  if (len != 1) {
    goto Fail;
  }
  if (!(cp = strrchr(major, '.'))) {
    goto Fail;
  }
  minor = atoi(cp + 1);
  cp[1] = 0;
  if (!minor) {
    goto Fail;
  }
  for (; minor > 0; minor--) {
    sprintf(buf, "co -q -p%s%d %s > %s", major, minor, rcsfilename, mirrorfilename);
    system(buf);
    getdigest(mirrorfilename, &mdContext);
    if (!memcmp(p->client.digest, (char *) mdContext.digest, DIGESTSIZE)) {
      p->mirror = p->client;
      return;
    }
  }

Fail:
  doremove(mirrorfilename);
  memset((char *) &p->mirror, 0, sizeof(struct s_fileentry));
}

/*---------------------------------------------------------------------------*/

static void apply_diffs(const char *sourcename, const char *diffname)
{

  FILE *fpdiff;
  FILE *fpsource;
  FILE *fptemp;
  char *cp;
  char cmd;
  char line[2048];
  char tempname[1024];
  int chr;
  int linenum;
  int numlines;
  int startline;
  struct stat statbuf;

  if (!(fpsource = fopen(sourcename, "r"))) {
    syscallerr(sourcename);
  }
  if (!(fpdiff = fopen(diffname, "r"))) {
    syscallerr(diffname);
  }
  strcpy(tempname, sourcename);
  for (cp = tempname; *cp; cp++) ;
  cp--;
  for (chr = 'A';; chr++) {
    if (!chr) {
      usererr("Could not generate temp file name");
    }
    *cp = chr;
    if (stat(tempname, &statbuf)) {
      break;
    }
  }
  if (!(fptemp = fopen(tempname, "w"))) {
    syscallerr(tempname);
  }
  linenum = 0;
  while (fgets(line, sizeof(line), fpdiff)) {
    if (sscanf(line, "%c%d %d", &cmd, &startline, &numlines) != 3) {
      usererr("Bad edit command");
    }
    switch (cmd) {
    case 'a':
      while (linenum < startline) {
	if (!fgets(line, sizeof(line), fpsource)) {
	  usererr("Unexpected end-of-file");
	}
	linenum++;
	fputs(line, fptemp);
      }
      while (--numlines >= 0) {
	if (!fgets(line, sizeof(line), fpdiff)) {
	  usererr("Unexpected end-of-file");
	}
	fputs(line, fptemp);
      }
      break;
    case 'd':
      while (linenum + 1 < startline) {
	if (!fgets(line, sizeof(line), fpsource)) {
	  usererr("Unexpected end-of-file");
	}
	linenum++;
	fputs(line, fptemp);
      }
      while (--numlines >= 0) {
	if (!fgets(line, sizeof(line), fpsource)) {
	  usererr("Unexpected end-of-file");
	}
	linenum++;
      }
      break;
    default:
      usererr("Bad edit command");
    }
  }
  while (fgets(line, sizeof(line), fpsource)) {
    fputs(line, fptemp);
  }
  fclose(fpsource);
  fclose(fpdiff);
  fclose(fptemp);
  if (rename(tempname, sourcename)) {
    syscallerr(tempname);
  }
}

/*---------------------------------------------------------------------------*/

static void send_update(struct s_file *p, enum e_action action, int flags, const char *masterfilename, const char *mirrorfilename)
{

  char buf[8 * 1024];
  char tempfilename[1024];
  int fd;
  int filesize;
  int len;
  int opcode;
  struct stat statbuf;
  unsigned short filemode;

#if DEBUG
  dump_file(p);
#endif
  filemode = (action == ACT_DELETE) ? p->client.mode : p->master.mode;
  opcode = ((int) filemode >> 12) & 0xf;
  opcode |= action << 4;
  len = strlen(p->name);
  if (len > 255) {
    usererr("File name too long");
  }
  print_action(action, filemode, p->name);
  writechar(opcode);
  writechar(len);
  writebuf(p->name, len);

  if (action == ACT_DELETE) {
    update_mirror_from_master(p, masterfilename, mirrorfilename);
    return;
  }

  if (S_ISDIR(p->master.mode)) {
    update_mirror_from_master(p, masterfilename, mirrorfilename);
    return;
  }

  if (S_ISLNK(p->master.mode)) {
    if (action == ACT_UPDATE) {
      protoerr();
    }
    filesize = readlink(masterfilename, buf, sizeof(buf));
    if (filesize < 0) {
      syscallerr(masterfilename);
    }
    if (filesize > 255) {
      usererr("File name too long");
    }
    writechar(filesize);
    writebuf(buf, filesize);
    update_mirror_from_master(p, masterfilename, mirrorfilename);
    return;
  }

  if (!S_ISREG(p->master.mode)) {
    protoerr();
  }
  tmpnam(tempfilename);
  switch (action) {
  case ACT_CREATE:
    sprintf(buf,
	    "%s < %s > %s",
	    (flags & USE_GZIP) ? GZIP_PROG " -9" : "compress",
	    masterfilename,
	    tempfilename);
    break;
  case ACT_UPDATE:
    sprintf(buf,
	    "diff -n %s %s | %s > %s",
	    mirrorfilename,
	    masterfilename,
	    (flags & USE_GZIP) ? GZIP_PROG " -9" : "compress",
	    tempfilename);
    break;
  default:
    protoerr();
  }
  system(buf);
  if (lstat(tempfilename, &statbuf)) {
    syscallerr(tempfilename);
  }
  filesize = (int) statbuf.st_size;
  fd = open(tempfilename, O_RDONLY, 0644);
  if (fd < 0) {
    syscallerr(tempfilename);
  }
  writebuf(p->master.digest, DIGESTSIZE);
  writeint(filesize);
  while (filesize > 0) {
    len = filesize < sizeof(buf) ? filesize : sizeof(buf);
    if (read(fd, buf, len) != len) {
      usererr("read: End of file");
    }
    writebuf(buf, len);
    filesize -= len;
  }
  if (close(fd)) {
    syscallerr("close");
  }
  if (doremove(tempfilename)) {
    syscallerr(tempfilename);
  }
  switch (readchar()) {
  case 0:
    usererr("Client reports digest mismatch after update");
    break;
  case 1:
    update_mirror_from_master(p, masterfilename, mirrorfilename);
    break;
  default:
    protoerr();
  }
}

/*---------------------------------------------------------------------------*/

static int recv_update(int flags)
{

  MD5_CTX mdContext;
  char buf[8 * 1024];
  char digest[DIGESTSIZE];
  char filename[1024];
  char tempfilename1[1024];
  char tempfilename2[1024];
  enum e_action action;
  int fd;
  int filesize;
  int len;
  int opcode;
  unsigned short filemode;

  if (!(opcode = readchar())) {
    return 0;
  }

  filemode = opcode << 12;
  action = (enum e_action) ((opcode >> 4) & 0xf);

  if (!(len = readchar())) {
    protoerr();
  }
  readbuf(filename, len);
  filename[len] = 0;

  print_action(action, filemode, filename);

  if (action == ACT_DELETE) {
    if (doremove(filename)) {
      syscallerr(filename);
    }
    if (len > 2 && filename[len - 2] == '.' && filename[len - 1] == 'c') {
      filename[len - 1] = 'o';
      doremove(filename);
    }
    return 1;
  }

  if (S_ISDIR(filemode)) {
    doremove(filename);
    if (mkdir(filename, 0755)) {
      syscallerr(filename);
    }
    return 1;
  }

  if (S_ISLNK(filemode)) {
    if (action == ACT_UPDATE) {
      protoerr();
    }
    filesize = readchar();
    if (!filesize) {
      protoerr();
    }
    readbuf(buf, filesize);
    buf[filesize] = 0;
    doremove(filename);
    umask(0333);
    if (symlink(buf, filename)) {
      syscallerr(filename);
    }
    umask(UMASK);
    return 1;
  }

  if (!S_ISREG(filemode)) {
    protoerr();
  }
  readbuf(digest, DIGESTSIZE);
  filesize = readint();
  tmpnam(tempfilename1);
  fd = open(tempfilename1, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    syscallerr(tempfilename1);
  }
  while (filesize > 0) {
    len = filesize < sizeof(buf) ? filesize : sizeof(buf);
    readbuf(buf, len);
    if (write(fd, buf, len) != len) {
      syscallerr("write");
    }
    filesize -= len;
  }
  if (close(fd)) {
    syscallerr("close");
  }
  if (action == ACT_UPDATE) {
    tmpnam(tempfilename2);
    sprintf(buf,
	    "%s -d < %s > %s",
	    (flags & USE_GZIP) ? GZIP_PROG : "compress",
	    tempfilename1,
	    tempfilename2);
    system(buf);
    apply_diffs(filename, tempfilename2);
    if (doremove(tempfilename2)) {
      syscallerr(tempfilename2);
    }
  } else if (action == ACT_CREATE) {
    doremove(filename);
    sprintf(buf,
	    "%s -d < %s > %s",
	    (flags & USE_GZIP) ? GZIP_PROG : "compress",
	    tempfilename1,
	    filename);
    system(buf);
  } else {
    protoerr();
  }
  if (doremove(tempfilename1)) {
    syscallerr(tempfilename1);
  }
  getdigest(filename, &mdContext);
  if (memcmp((char *) mdContext.digest, digest, DIGESTSIZE)) {
    writechar(0);
    usererr("Digest mismatch after update");
  }
  writechar(1);
  return 1;
}

/*---------------------------------------------------------------------------*/

static void scandirectory(const char *dirname, enum e_scanmode scanmode)
{

  DIR *dirp;
  MD5_CTX mdContext;
  char *fnp;
  char buf[1024];
  char filename[1024];
  int n;
  struct dirent *dp;
  struct s_file *p;
  struct stat statbuf;

  if (!(dirp = opendir(dirname))) {
    syscallerr(dirname);
  }
  fnp = filename;
  if (*dirname != '.' || dirname[1]) {
    while (*dirname) {
      *fnp++ = *dirname++;
    }
    *fnp++ = '/';
  }
  while ((dp = readdir(dirp))) {
    if (*dp->d_name == '.') {
      if (!dp->d_name[1]) {
	continue;
      }
      if (dp->d_name[1] == '.' && !dp->d_name[2]) {
	continue;
      }
    }
    strcpy(fnp, dp->d_name);
    if (scanmode != SCAN_MIRROR && !filefilter(filename)) {
      continue;
    }
    if (lstat(filename, &statbuf)) {
      syscallerr(filename);
    }
    switch (FTYPE(statbuf.st_mode)) {
    case S_IFDIR:
      break;
    case S_IFREG:
      getdigest(filename, &mdContext);
      break;
    case S_IFLNK:
      if ((n = readlink(filename, buf, sizeof(buf))) < 0) {
	syscallerr(filename);
      }
      MD5Init(&mdContext);
      MD5Update(&mdContext, (unsigned char *) buf, n);
      MD5Final(&mdContext);
      break;
    default:
      continue;
    }
    switch (scanmode) {
    case SCAN_CLIENT:
      send_info(filename, statbuf.st_mode, (char *) mdContext.digest);
      break;
    case SCAN_MASTER:
      p = getfileptr(filename);
      p->master.mode = statbuf.st_mode;
      memcpy(p->master.digest, (char *) mdContext.digest, DIGESTSIZE);
      break;
    case SCAN_MIRROR:
      p = getfileptr(filename);
      p->mirror.mode = statbuf.st_mode;
      memcpy(p->mirror.digest, (char *) mdContext.digest, DIGESTSIZE);
      break;
    default:
      protoerr();
    }
    if (S_ISDIR(statbuf.st_mode)) {
      scandirectory(filename, scanmode);
    }
  }
  closedir(dirp);
}

/*---------------------------------------------------------------------------*/

static void server_version_1(int flags)
{

  char digest[DIGESTSIZE];
  char filename[1024];
  char masterfilename[1024];
  char mirror[1024];
  char mirrorfilename[1024];
  const char *master;
  struct s_file *newhead;
  struct s_file *p;
  unsigned short filemode;

  generate_dynamic_include_table();

  if (chdir(master = sourcedir)) {
    syscallerr(master);
  }
  scandirectory(".", SCAN_MASTER);

  strcpy(mirror, mirrordir);
  strcat(mirror, "/");
  strcat(mirror, client);
  if (chdir(mirror)) {
    syscallerr(mirror);
  }
  scandirectory(".", SCAN_MIRROR);

  for (;;) {
    read_info(filename, &filemode, digest);
    if (!filemode) {
      break;
    }
    p = getfileptr(filename);
    p->client.mode = filemode;
    memcpy(p->client.digest, digest, DIGESTSIZE);
  }

  newhead = 0;
  while ((p = Filehead)) {
    Filehead = p->next;
    strcpy(masterfilename, master);
    strcat(masterfilename, "/");
    strcat(masterfilename, p->name);
    strcpy(mirrorfilename, mirror);
    strcat(mirrorfilename, "/");
    strcat(mirrorfilename, p->name);
    if (!FTYPE(p->master.mode)) {
      if (FTYPE(p->client.mode)) {
	send_update(p, ACT_DELETE, flags, masterfilename, mirrorfilename);
      } else {
	update_mirror_from_master(p, masterfilename, mirrorfilename);
      }
      free(p);
    } else {
      if (FTYPE(p->mirror.mode) &&
	  (FTYPE(p->mirror.mode) != FTYPE(p->client.mode) ||
	   FTYPE(p->mirror.mode) != FTYPE(p->master.mode))) {
	if (doremove(mirrorfilename)) {
	  syscallerr(mirrorfilename);
	}
	memset((char *) &p->mirror, 0, sizeof(struct s_fileentry));
      }
      p->next = newhead;
      newhead = p;
    }
  }
  Filehead = newhead;

  for (p = Filehead; p; p = p->next) {

    strcpy(masterfilename, master);
    strcat(masterfilename, "/");
    strcat(masterfilename, p->name);
    strcpy(mirrorfilename, mirror);
    strcat(mirrorfilename, "/");
    strcat(mirrorfilename, p->name);

    if (FTYPE(p->master.mode) != FTYPE(p->client.mode)) {
      send_update(p, ACT_CREATE, flags, masterfilename, mirrorfilename);
      continue;
    }

    switch (FTYPE(p->master.mode)) {
    case S_IFDIR:
      update_mirror_from_master(p, masterfilename, mirrorfilename);
      break;
    case S_IFREG:
      if (!memcmp(p->master.digest, p->client.digest, DIGESTSIZE)) {
	update_mirror_from_master(p, masterfilename, mirrorfilename);
	continue;
      }
      if (memcmp(p->client.digest, p->mirror.digest, DIGESTSIZE)) {
	update_mirror_from_rcs(p, master, mirrorfilename);
      }
      if (memcmp(p->client.digest, p->mirror.digest, DIGESTSIZE) ||
	  stringmatch(p->name, "*.gif")) {
	send_update(p, ACT_CREATE, flags, masterfilename, mirrorfilename);
      } else {
	send_update(p, ACT_UPDATE, flags, masterfilename, mirrorfilename);
      }
      break;
    case S_IFLNK:
      if (memcmp(p->master.digest, p->client.digest, DIGESTSIZE)) {
	send_update(p, ACT_CREATE, flags, masterfilename, mirrorfilename);
      } else {
	update_mirror_from_master(p, masterfilename, mirrorfilename);
      }
      break;
    default:
#if DEBUG
      dump_file(p);
#endif
      protoerr();
    }

  }
  writechar(0);
  flushoutbuf();
}

/*---------------------------------------------------------------------------*/

static void doserver(void)
{

  char buf[1024];
  int fdpipe[2];
  int flags = 0;
  int i;
  int version;

  if (!*MAIL_PROG) {
    exit(1);
  }
  if ((fdinp = dup(0)) < 3) {
    exit(1);
  }
  if ((fdout = dup(1)) < 3) {
    exit(1);
  }
  if (pipe(fdpipe)) {
    exit(1);
  }
  switch (fork()) {
  case -1:
    exit(1);
  case 0:
    for (i = 0; i < FD_SETSIZE; i++) {
      if (i != fdpipe[0]) {
	close(i);
      }
    }
    dup(fdpipe[0]);
    open("/dev/null", O_RDWR, 0666);
    open("/dev/null", O_RDWR, 0666);
    close(fdpipe[0]);
    execl(MAIL_PROG, MAIL_PROG, "-s", "Net Update Log", notifyuser, 0);
    exit(1);
  default:
    for (i = 0; i < FD_SETSIZE; i++) {
      if (i != fdinp && i != fdout && i != fdlock && i != fdpipe[1]) {
	close(i);
      }
    }
    open("/dev/null", O_RDWR, 0666);
    dup(fdpipe[1]);
    dup(fdpipe[1]);
    close(fdpipe[1]);
  }
  readclientname();
  if (isdigit(*client & 0xff)) {
    flags = atoi(client);
    readclientname();
  }
  version = (flags >> 8) & 0xff;
  printf("Client=%s Version=%d Compressor=%s\n",
	 client,
	 version,
	 (flags & USE_GZIP) ? "GZIP" : "COMPRESS");
  if ((flags & USE_GZIP) && !*GZIP_PROG) {
    usererr("gzip not available - shutting down");
  }
  strcpy(buf, mirrordir);
  strcat(buf, "/");
  strcat(buf, client);
  if (chdir(buf)) {
    mkdir(sourcedir, 0755);
    mkdir(mirrordir, 0755);
    mkdir(buf, 0755);
    if (chdir(buf)) {
      syscallerr(buf);
    }
  }
  switch (version) {
  case 1:
    server_version_1(flags);
    break;
  default:
    protoerr();
  }
  quit();
}

/*---------------------------------------------------------------------------*/

static void doclient(void)
{

  char buf[1024];
  int addrlen;
  int flags;
  struct sockaddr *addr;
  struct stat statbuf;

  if (chdir(sourcedir)) {
    syscallerr(sourcedir);
  }

  flags = VERSION << 8;
  if (*GZIP_PROG) {
    flags |= USE_GZIP;
  }

  if (do_unix_networking) {
    strcpy(buf, server);
    if (!strchr(buf, ':')) {
      strcat(buf, ":4715");
    }
  } else {
    strcpy(buf, "unix:");
    strcat(buf, wampesdir);
    strcat(buf, "/.sockets/netcmd");
  }
  if (!(addr = build_sockaddr(buf, &addrlen))) {
    usererr("build_sockaddr: Failed");
  }
  fdinp = fdout = socket(addr->sa_family, SOCK_STREAM, 0);
  if (fdinp < 0) {
    syscallerr("socket");
  }
  if (connect(fdinp, addr, addrlen)) {
    syscallerr("connect");
  }
  if (!do_unix_networking) {
    sprintf(buf, "binary\nconnect tcp %s netupds\n", server);
    writebuf(buf, strlen(buf));
  }

  xmitted = 0;
  sprintf(buf, "%d", flags);
  writebuf(buf, strlen(buf) + 1);
  writebuf(client, strlen(client) + 1);
  flushoutbuf();

  generate_dynamic_include_table();
  scandirectory(".", SCAN_CLIENT);
  writechar(0);

  while (recv_update(flags)) ;

  strcpy(buf, "net.rc.");
  strcat(buf, client);
  if (!lstat(buf, &statbuf)) {
    doremove("net.rc");
    link(buf, "net.rc");
  }

  printf("Received %d bytes, sent %d bytes\n", received, xmitted);

  if (do_make) {
    system("make");
  }

}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char *cp;
  char *options;
  char *progname;
  char buf[1024];
  char lockdir[1024];
  char lockfile[1024];
  int chr;
  int errflag;
  int is_client;

  setbuf(stdout, 0);

  if (getuid()) {
    usererr("Permission denied");
  }

  alarm(TIMEOUT);
  umask(UMASK);

  putenv("HOME=/");
  putenv("LOGNAME=root");
  putenv("PATH="
	 "/opt/ansic/bin:"
	 "/opt/SUNWspro/bin:"
	 "/usr/bsd:"
	 "/usr/lang:"
	 "/bin/posix:"
	 "/bin:"
	 "/usr/bin:"
	 "/usr/ccs/bin:"
	 "/usr/ucb:"
	 "/usr/contrib/bin:"
	 "/usr/local/bin:"
	 "/usr/local/etc");
  putenv("LD_LIBRARY_PATH=/opt/SUNWspro/lib");
  putenv("SHELL=/bin/sh");
  if (!getenv("TZ")) {
    putenv("TZ=MEZ-1MESZ");
  }

  if (argc < 1) {
    usererr("Unknown program name");
  }
  if ((progname = strrchr(argv[0], '/'))) {
    progname++;
  } else {
    progname = argv[0];
  }
  if (!strcmp(progname, "netupdc")) {
    is_client = 1;
    options = "ms:uw:";
  } else if (!strcmp(progname, "netupds")) {
    is_client = 0;
    options = "n:s:";
  } else {
    is_client = 0;
    options = "";
    usererr("Unknown program name");
  }

  errflag = 0;
  while ((chr = getopt(argc, argv, options)) != EOF) {
    switch (chr) {
    case 'm':
      do_make = 0;
      break;
    case 'n':
      notifyuser = optarg;
      break;
    case 's':
      sourcedir = optarg;
      break;
    case 'u':
      do_unix_networking = 1;
      break;
    case 'w':
      wampesdir = optarg;
      break;
    case '?':
      errflag = 1;
      break;
    }
  }

  if (is_client) {
    if (optind < argc) {
      server = argv[optind++];
    }
    if (optind < argc) {
      client = argv[optind++];
    } else {
      if (gethostname(buf, sizeof(buf))) {
	syscallerr("gethostname");
      }
      if ((cp = strchr(buf, '.'))) {
	*cp = 0;
      }
      client = strdup(buf);
    }
  }

  if (errflag || optind < argc) {
    if (is_client) {
      fprintf(stderr, "Usage: netupdc [-s sourcedir] [-w wampesdir] [-u] [-m] [server [client] ]\n");
    } else {
      fprintf(stderr, "Usage: netupds [-s sourcedir] [-n user]\n");
    }
    exit(1);
  }

  strcpy(lockdir, sourcedir);
  strcat(lockdir, "/locks");

  strcpy(lockfile, lockdir);
  strcat(lockfile, "/netupd");

  strcpy(mirrordir, sourcedir);
  strcat(mirrordir, "/netupdmirrors");

  mkdir(lockdir, 0755);
  if ((fdlock = lock_file(lockfile, is_client, 0)) < 0) {
    syscallerr(lockfile);
  }

  if (is_client) {
    doclient();
  } else {
    doserver();
  }

  return 0;
}
