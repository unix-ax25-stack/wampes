#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Attic/netupds.c,v 1.15 1993/10/13 22:31:23 deyke Exp $";
#endif

/* Net Update Server */

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#include "configure.h"
#include "netupd.h"

static void pexit(const char *s);
static void doread(int fd, char *buf, size_t cnt);
static void read_string(int fd, char *buf, int bufsize);
static void dowrite(int fd, const char *buf, size_t cnt);

/*---------------------------------------------------------------------------*/

static void pexit(const char *s)
{
  perror(s);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void doread(int fd, char *buf, size_t cnt)
{

  char *p = buf;
  int n;

  while (cnt) {
    n = read(fd, p, cnt);
    if (n < 0) pexit("read");
    if (!n) {
      printf("read(): End of file\n");
      exit(1);
    }
    p += n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

static void read_string(int fd, char *buf, int bufsize)
{
  int i;

  for (i = 0; ; i++) {
    if (i >= bufsize) {
      printf("String too long\n");
      exit(1);
    }
    doread(fd, buf + i, 1);
    if (!buf[i]) break;
    if (!isalnum(buf[i] & 0xff) && buf[i] != '-') {
      printf("Bad character in string\n");
      exit(1);
    }
  }
  if (!*buf) {
    printf("Null string received\n");
    exit(1);
  }
}

/*---------------------------------------------------------------------------*/

static void dowrite(int fd, const char *buf, size_t cnt)
{

  const char * p = buf;
  int n;

  while (cnt) {
    n = write(fd, p, cnt);
    if (n <= 0) pexit("write");
    p += n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

int main(void)
{

  char buf[1024];
  char client[1024];
  char filename[1024];
  int fdfile;
  int fdpipe[2];
  int fdsocket;
  int filesize;
  int flags = 0;
  int i;
  int net_filesize;
  int net_i;
  struct stat statbuf;

  if (!*MAIL_PROG || !*GZIP_PROG) exit(1);

  alarm(6 * 3600);

  umask(022);
  putenv("HOME=/users/root");
  putenv("LOGNAME=root");
  putenv("PATH=/bin:/usr/bin:/usr/local/bin:/usr/contrib/bin");
  putenv("SHELL=/bin/sh");
  putenv("TZ=MEZ-1MESZ");

  if ((fdsocket = dup(0)) < 3) exit(1);
  if (pipe(fdpipe)) exit(1);
  switch (fork()) {
  case -1:
    exit(1);
  case 0:
    for (i = 0; i < FD_SETSIZE; i++)
      if (i != fdpipe[0]) close(i);
    dup(fdpipe[0]);
    open("/dev/null", O_RDWR, 0666);
    open("/dev/null", O_RDWR, 0666);
    close(fdpipe[0]);
    execl(MAIL_PROG, MAIL_PROG, "-s", "netupds log", "root", (char *) 0);
    exit(1);
  default:
    for (i = 0; i < FD_SETSIZE; i++)
      if (i != fdpipe[1] && i != fdsocket) close(i);
    open("/dev/null", O_RDWR, 0666);
    dup(fdpipe[1]);
    dup(fdpipe[1]);
    close(fdpipe[1]);
  }

  read_string(fdsocket, client, sizeof(client));
  if (isdigit(*client & 0xff)) {
    flags = atoi(client);
    read_string(fdsocket, client, sizeof(client));
  }
  printf("Client = %s\n", client);
  printf("Flags =");
  printf((flags & USE_PATCH) ? " PATCH" : " EX");
  printf((flags & USE_GZIP)  ? " GZIP"  : " COMPRESS");
  printf("\n");

  sprintf(buf, "/users/funk/dk5sg/tcp.%s", client);
  if (chdir(buf)) pexit(buf);

  tmpnam(filename);
  sprintf(buf,
	  "/users/funk/dk5sg/tcp/util/genupd %s %s | %s > %s",
	  client,
	  (flags & USE_PATCH) ? "patch"   : "ex",
	  (flags & USE_GZIP)  ? GZIP_PROG " -9" : "compress",
	  filename);
  system(buf);

  if (stat(filename, &statbuf)) pexit(filename);
  filesize = statbuf.st_size;

  printf("File size = %i\n", filesize);
  fflush(stdout);

  net_filesize = htonl(filesize);
  dowrite(fdsocket, (char *) &net_filesize, 4);

  fdfile = open(filename, O_RDONLY, 0600);
  if (fdfile < 0) pexit(filename);
  while (filesize > 0) {
    i = filesize < sizeof(buf) ? filesize : sizeof(buf);
    doread(fdfile, buf, i);
    dowrite(fdsocket, buf, i);
    filesize -= i;
  }
  if (close(fdfile)) pexit("close");

  doread(fdsocket, (char *) &net_i, 4);
  i = ntohl(net_i);

  printf("Response = %i\n", i);
  fflush(stdout);

  if (!i) {
    sprintf(buf,
	    "%s < %s | sh",
	    (flags & USE_GZIP) ? GZIP_PROG " -d" : "uncompress",
	    filename);
    system(buf);
  }

  if (unlink(filename)) pexit(filename);

  return 0;
}

