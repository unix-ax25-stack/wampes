/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Attic/netupds.c,v 1.2 1990/03/02 09:27:18 deyke Exp $ */

/* Net Update Server */

#include <sys/types.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>

extern void exit();
extern void perror();

/*---------------------------------------------------------------------------*/

static void pexit(s)
char  *s;
{
  perror(s);
  exit(1);
}

/*---------------------------------------------------------------------------*/

static void doread(fd, buf, cnt)
int  fd;
char  *buf;
unsigned int  cnt;
{

  char  *p = buf;
  int  n;

  while (cnt) {
    n = read(fd, p, cnt);
    if (n < 0) pexit("read()");
    if (!n) {
      printf("read(): End of file\n");
      exit(1);
    }
    p += n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

static void dowrite(fd, buf, cnt)
int  fd;
char  *buf;
unsigned int  cnt;
{

  char  *p = buf;
  int  n;

  while (cnt) {
    n = write(fd, p, cnt);
    if (n <= 0) pexit("write()");
    p += n;
    cnt -= n;
  }
}

/*---------------------------------------------------------------------------*/

int  main()
{

  char  buf[1024];
  char  client[1024];
  char  filename[1024];
  int  fdfile;
  int  fdpipe[2];
  int  fdsocket;
  int  filesize;
  int  i;
  struct stat statbuf;

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
    for (i = 0; i < _NFILE; i++)
      if (i != fdpipe[0]) close(i);
    dup(fdpipe[0]);
    open("/dev/null", O_RDWR, 0666);
    open("/dev/null", O_RDWR, 0666);
    close(fdpipe[0]);
    execl("/usr/bin/mailx", "mailx", "-s", "netupds log", "root", (char *) 0);
    exit(1);
  default:
    for (i = 0; i < _NFILE; i++)
      if (i != fdpipe[1] && i != fdsocket) close(i);
    open("/dev/null", O_RDWR, 0666);
    dup(fdpipe[1]);
    dup(fdpipe[1]);
    close(fdpipe[1]);
  }

  for (i = 0; ; i++) {
    if (i >= sizeof(client)) {
      printf("Client name too long\n");
      exit(1);
    }
    doread(fdsocket, client + i, 1);
    if (!client[i]) break;
    if (!isalnum(client[i] & 0xff)) {
      printf("Bad char in client name\n");
      exit(1);
    }
  }
  if (!*client) {
    printf("Null client name\n");
    exit(1);
  }

  printf("Client = %s\n", client);
  fflush(stdout);

  sprintf(buf, "/users/funk/dk5sg/tcp.%s", client);
  if (chdir(buf)) pexit(buf);

  tmpnam(filename);
  sprintf(buf, "/users/funk/dk5sg/tcp/util/genupd %s | compress > %s", client, filename);
  system(buf);

  if (stat(filename, &statbuf)) pexit(filename);
  filesize = statbuf.st_size;

  printf("File size = %i\n", filesize);
  fflush(stdout);

  dowrite(fdsocket, (char *) & filesize, 4);

  fdfile = open(filename, O_RDONLY, 0600);
  if (fdfile < 0) pexit(filename);
  while (filesize > 0) {
    i = filesize < sizeof(buf) ? filesize : sizeof(buf);
    doread(fdfile, buf, (unsigned) i);
    dowrite(fdsocket, buf, (unsigned) i);
    filesize -= i;
  }

  doread(fdsocket, (char *) & i, 4);

  printf("Response = %i\n", i);
  fflush(stdout);

  if (!i) {
    sprintf(buf, "uncompress < %s | sh", filename);
    system(buf);
  }

  if (unlink(filename)) pexit(filename);

  return 0;
}

