#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Attic/netupds.c,v 1.18 1994/10/30 21:27:09 deyke Exp $";
#endif

/* Net Update Client/Server */

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <netinet/in.h>
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

#include "buildsaddr.h"
#include "configure.h"
#include "strdup.h"

/* Flags */

#define USE_PATCH       0x0001  /* Else use ex */
#define USE_GZIP        0x0002  /* Else use compress */

/*---------------------------------------------------------------------------*/

static void pexit(const char *s)
{
  perror(s);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void doread(int fd, char *buf, int cnt)
{

  char *p = buf;
  int n;

  while (cnt) {
    n = read(fd, p, cnt);
    if (n < 0)
      pexit("read");
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

  for (i = 0;; i++) {
    if (i >= bufsize) {
      printf("String too long\n");
      exit(1);
    }
    doread(fd, buf + i, 1);
    if (!buf[i])
      break;
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

static void dowrite(int fd, const char *buf, int cnt)
{

  const char *p = buf;
  int n;

  while (cnt) {
    n = write(fd, p, cnt);
    if (n <= 0)
      pexit("write");
    p += n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

static int main_server(int argc, char **argv)
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

  if (!*MAIL_PROG)
    exit(1);
  if ((fdsocket = dup(0)) < 3)
    exit(1);
  if (pipe(fdpipe))
    exit(1);
  switch (fork()) {
  case -1:
    exit(1);
  case 0:
    for (i = 0; i < FD_SETSIZE; i++)
      if (i != fdpipe[0])
	close(i);
    dup(fdpipe[0]);
    open("/dev/null", O_RDWR, 0666);
    open("/dev/null", O_RDWR, 0666);
    close(fdpipe[0]);
    execl(MAIL_PROG, MAIL_PROG, "-s", "netupds log", "root", (char *) 0);
    exit(1);
  default:
    for (i = 0; i < FD_SETSIZE; i++)
      if (i != fdpipe[1] && i != fdsocket)
	close(i);
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
  printf((flags & USE_GZIP) ? " GZIP" : " COMPRESS");
  printf("\n");
#ifdef linux
  if (!(flags & USE_PATCH)) {
    printf("Attempt to use \"ex\" - shutting down\n");
    exit(1);
  }
#endif
  if ((flags & USE_PATCH) && !*PATCH_PROG) {
    printf("patch not available - shutting down\n");
    exit(1);
  }
  if ((flags & USE_GZIP) && !*GZIP_PROG) {
    printf("gzip not available - shutting down\n");
    exit(1);
  }
  sprintf(buf, "/tcp/netupdmirrors/%s", client);
  if (chdir(buf)) {
    mkdir("/tcp", 0755);
    mkdir("/tcp/netupdmirrors", 0755);
    mkdir(buf, 0755);
    if (chdir(buf))
      pexit(buf);
  }
  tmpnam(filename);
  sprintf(buf,
	  "/tcp/util/genupd %s %s | %s > %s",
	  client,
	  (flags & USE_PATCH) ? "patch" : "ex",
	  (flags & USE_GZIP) ? GZIP_PROG " -9" : "compress",
	  filename);
  system(buf);
  if (stat(filename, &statbuf))
    pexit(filename);
  filesize = (int) statbuf.st_size;
  printf("File size = %i\n", filesize);
  fflush(stdout);
  net_filesize = htonl(filesize);
  dowrite(fdsocket, (char *) &net_filesize, 4);
  fdfile = open(filename, O_RDONLY, 0600);
  if (fdfile < 0)
    pexit(filename);
  while (filesize > 0) {
    i = filesize < sizeof(buf) ? filesize : sizeof(buf);
    doread(fdfile, buf, i);
    dowrite(fdsocket, buf, i);
    filesize -= i;
  }
  if (close(fdfile))
    pexit("close");
  doread(fdsocket, (char *) &net_i, 4);
  i = ntohl(net_i);
  printf("Response = %i\n", i);
  fflush(stdout);
  if (!i) {
    sprintf(buf,
	    "%s -d < %s | sh",
	    (flags & USE_GZIP) ? GZIP_PROG : "compress",
	    filename);
    system(buf);
  }
  if (unlink(filename))
    pexit(filename);
  return 0;
}

/*---------------------------------------------------------------------------*/

static int main_client(int argc, char **argv)
{

  char *client;
  char *cp;
  char *server;
  char buf[1024];
  char filename[1024];
  int addrlen;
  int fdfile;
  int fdsocket;
  int filesize;
  int flags = 0;
  int i;
  int net_filesize;
  int net_i;
  struct sockaddr *addr;

  server = (argc < 2) ? "db0sao" : argv[1];
  if (argc < 3) {
    if (gethostname(buf, sizeof(buf)))
      pexit("gethostname");
    if ((cp = strchr(buf, '.')))
      *cp = 0;
    client = strdup(buf);
  } else
    client = argv[2];
  if (chdir("/tcp"))
    pexit("/tcp");
  if (*PATCH_PROG)
    flags |= USE_PATCH;
#ifdef linux
  if (!(flags & USE_PATCH)) {
    printf("patch not available - shutting down\n");
    exit(1);
  }
#endif
  if (*GZIP_PROG)
    flags |= USE_GZIP;
  if (!(addr = build_sockaddr("unix:/tcp/.sockets/netcmd", &addrlen))) {
    printf("build_sockaddr(): Failed\n");
    exit(1);
  }
  fdsocket = socket(addr->sa_family, SOCK_STREAM, 0);
  if (fdsocket < 0)
    pexit("socket");
  if (connect(fdsocket, addr, addrlen))
    pexit("connect");
  strcpy(buf, "binary\n");
  dowrite(fdsocket, buf, strlen(buf));
  sprintf(buf, "connect tcp %s netupds\n", server);
  dowrite(fdsocket, buf, strlen(buf));
  if (flags) {
    sprintf(buf, "%d", flags);
    dowrite(fdsocket, buf, strlen(buf) + 1);
  }
  dowrite(fdsocket, client, strlen(client) + 1);
  doread(fdsocket, (char *) &net_filesize, 4);
  filesize = ntohl(net_filesize);
  tmpnam(filename);
  fdfile = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fdfile < 0)
    pexit(filename);
  while (filesize > 0) {
    i = filesize < sizeof(buf) ? filesize : sizeof(buf);
    doread(fdsocket, buf, i);
    dowrite(fdfile, buf, i);
    filesize -= i;
  }
  if (close(fdfile))
    pexit("close");
  sprintf(buf,
	  "%s -d < %s | sh",
	  (flags & USE_GZIP) ? GZIP_PROG : "compress",
	  filename);
  i = system(buf);
  net_i = htonl(i);
  dowrite(fdsocket, (char *) &net_i, 4);
  if (unlink(filename))
    pexit(filename);
  if (!i)
    system("exec make");
  return i;
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{
  char *progname;

  if (getuid()) {
    printf("%s: Permission denied\n", *argv);
    exit(1);
  }
  alarm(6 * 3600);
  umask(022);
#ifdef linux
  if (!getenv("HOME"))
    putenv("HOME=/");
#else
  if (!getenv("HOME"))
    putenv("HOME=" HOME_DIR "/root");
#endif
  if (!getenv("LOGNAME"))
    putenv("LOGNAME=root");
  if (!getenv("PATH"))
    putenv("PATH=/bin:/usr/bin:/usr/local/bin:/usr/contrib/bin");
  if (!getenv("SHELL"))
    putenv("SHELL=/bin/sh");
  if (!getenv("TZ"))
    putenv("TZ=MEZ-1MESZ");
  if (argc > 0) {
    if ((progname = strrchr(argv[0], '/')))
      progname++;
    else
      progname = argv[0];
    if (!strcmp(progname, "netupds"))
      return main_server(argc, argv);
    if (!strcmp(progname, "netupdc"))
      return main_client(argc, argv);
  }
  printf("Unknown program name\n");
  return 1;
}
