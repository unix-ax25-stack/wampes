#ifndef __lint
static char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/convers.c,v 1.10 1993/03/30 17:23:33 deyke Exp $";
#endif

#define _HPUX_SOURCE

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <unistd.h>

#ifdef __STDC__
#define __ARGS(x)       x
#else
#define __ARGS(x)       ()
#define const
#endif

#ifdef __hpux
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

#include "buildsaddr.h"

extern char *optarg;
extern int optind;

static struct termios prev_termios;

static void stop __ARGS((char *arg));

/*---------------------------------------------------------------------------*/

static void stop(arg)
char *arg;
{
  if (*arg) perror(arg);
  tcsetattr(0, TCSANOW, &prev_termios);
  exit(0);
}

/*---------------------------------------------------------------------------*/

int main(argc, argv)
int argc;
char **argv;
{

#if 1
  char *server = "unix:/tcp/sockets/convers";
#else
  char *server = "*:3600";
#endif

  char buffer[2048];
  char c;
  char inbuf[2048];
  char outbuf[2048];
  int addrlen;
  int ch;
  int channel = 0;
  int echo;
  int errflag = 0;
  int i;
  int incnt = 0;
  int outcnt = 0;
  int size;
  struct fd_set actread;
  struct fd_set chkread;
  struct sockaddr *addr;
  struct termios curr_termios;

  signal(SIGPIPE, SIG_IGN);

  if (tcgetattr(0, &prev_termios)) stop(*argv);
  curr_termios = prev_termios;
  echo = curr_termios.c_lflag & ECHO;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 1;
  curr_termios.c_cc[VTIME] = 0;
  if (tcsetattr(0, TCSANOW, &curr_termios)) stop(*argv);

  while ((ch = getopt(argc, argv, "c:s:")) != EOF)
    switch (ch) {
    case 'c':
      channel = atoi(optarg);
      break;
    case 's':
      server = optarg;
      break;
    case '?':
      errflag = 1;
      break;
    }

  if (errflag || optind < argc || !(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "usage: convers [-c channel] [-s host:service]\n");
    stop("");
  }

  close(3);
  if (socket(addr->sa_family, SOCK_STREAM, 0) != 3) stop(*argv);
  if (connect(3, addr, addrlen)) stop(*argv);

  sprintf(inbuf, "/NAME %s %d\n", getenv("LOGNAME"), channel);
  if (write(3, inbuf, strlen(inbuf)) < 0) stop(*argv);

  FD_ZERO(&chkread);
  FD_SET(0, &chkread);
  FD_SET(3, &chkread);

  for (; ; ) {
    actread = chkread;
    select(4, SEL_ARG(&actread), SEL_ARG(0), SEL_ARG(0), (struct timeval *) 0);
    if (FD_ISSET(0, &actread)) {
      do {
	if ((size = read(0, buffer, sizeof(buffer))) <= 0) stop("");
	for (i = 0; i < size; i++) {
	  c = buffer[i];
	  if (c == '\r') c = '\n';
	  if (c == prev_termios.c_cc[VERASE]) {
	    if (incnt) {
	      incnt--;
	      if (echo && write(1, "\b \b", 3) < 0) stop(*argv);
	    }
	  } else if (c == prev_termios.c_cc[VKILL]) {
	    for (; incnt; incnt--)
	      if (echo && write(1, "\b \b", 3) < 0) stop(*argv);
	  } else if (echo && c == 18) {
	    if (write(1, "^R\n", 3) < 0) stop(*argv);
	    if (write(1, inbuf, incnt) < 0) stop(*argv);
	  } else {
	    inbuf[incnt++] = c;
	    if (echo && write(1, &c, 1) < 0) stop(*argv);
	  }
	  if (c == '\n' || incnt == sizeof(inbuf) - 1) {
	    if (*inbuf == '!') {
	      inbuf[incnt] = '\0';
	      if (tcsetattr(0, TCSANOW, &prev_termios)) stop(*argv);
	      system(inbuf + 1);
	      if (tcsetattr(0, TCSANOW, &curr_termios)) stop(*argv);
	      if (write(1, "!\n", 2) < 0) stop(*argv);
	    } else {
	      if (write(3, inbuf, incnt) < 0) stop(*argv);
	    }
	    incnt = 0;
	  }
	}
      } while (incnt);
    } else {
      size = read(3, buffer, sizeof(buffer));
      if (size <= 0) stop("");
      for (i = 0; i < size; i++) {
	c = buffer[i];
	if (c != '\r') outbuf[outcnt++] = c;
	if (c == '\n' || outcnt == sizeof(outbuf)) {
	  if (write(1, outbuf, outcnt) < 0) stop(*argv);
	  outcnt = 0;
	}
      }
    }
  }
}

