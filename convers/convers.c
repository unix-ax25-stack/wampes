#ifndef __lint
static const char rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/convers.c,v 1.21 1996/08/11 18:16:05 deyke Exp $";
#endif

#include <sys/types.h>

#include <stdio.h>      /* must be before pwd.h */

#include <pwd.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef _AIX
#include <sys/select.h>
#endif

#ifdef ibm032
#include <sgtty.h>
#else
#include <termios.h>
#endif

#if defined __hpux && !defined _FD_SET
#define SEL_ARG(x) ((int *) (x))
#else
#define SEL_ARG(x) (x)
#endif

#include "buildsaddr.h"

extern char *optarg;
extern int optind;

#ifdef ibm032
static struct sgttyb prev_sgttyb;
#else
static struct termios prev_termios;
#endif

/*---------------------------------------------------------------------------*/

static void stop(const char *arg)
{
  if (*arg) perror(arg);
#ifdef ibm032
  ioctl(0, TIOCSETP, &prev_sgttyb);
#else
  tcsetattr(0, TCSANOW, &prev_termios);
#endif
  exit(0);
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char buffer[2048];
  char c;
  char inbuf[2048];
  char outbuf[2048];
  char *cp;
  char *name;
  char *note = 0;
  const char *server = "unix:/tcp/sockets/convers";
  int addrlen;
  int channel = 0;
  int ch;
  int dohosts = 0;
  int dokick = 0;
  int dolinks = 0;
  int dopeers = 0;
  int dousers = 0;
  int dowho = 0;
  int echo;
  int erase_char;
  int errflag = 0;
  int incnt = 0;
  int i;
  int kill_char;
  int outcnt = 0;
  int size;
  struct fd_set actread;
  struct fd_set chkread;
  struct passwd *pw;
  struct sockaddr *addr = 0;

#ifdef ibm032
  struct sgttyb curr_sgttyb;
#else
  struct termios curr_termios;
#endif

  signal(SIGPIPE, SIG_IGN);

#ifdef ibm032
  if (ioctl(0, TIOCGETP, &prev_sgttyb)) stop(*argv);
  echo = prev_sgttyb.sg_flags & ECHO;
  erase_char = prev_sgttyb.sg_erase;
  kill_char = prev_sgttyb.sg_kill;
  curr_sgttyb = prev_sgttyb;
  curr_sgttyb.sg_flags = CRMOD | CBREAK;
  if (ioctl(0, TIOCSETP, &curr_sgttyb)) stop(*argv);
#else
  if (tcgetattr(0, &prev_termios)) stop(*argv);
  echo = prev_termios.c_lflag & ECHO;
  erase_char = prev_termios.c_cc[VERASE];
  kill_char = prev_termios.c_cc[VKILL];
  curr_termios = prev_termios;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 1;
  curr_termios.c_cc[VTIME] = 0;
  if (tcsetattr(0, TCSANOW, &curr_termios)) stop(*argv);
#endif

  while ((ch = getopt(argc, argv, "c:hkln:ps:uw")) != EOF)
    switch (ch) {
    case 'c':
      channel = atoi(optarg);
      break;
    case 'h':
      dohosts = 1;
      break;
    case 'k':
      dokick = 1;
      break;
    case 'l':
      dolinks = 1;
      break;
    case 'n':
      note = optarg;
      break;
    case 'p':
      dopeers = 1;
      break;
    case 's':
      server = optarg;
      break;
    case 'u':
      dousers = 1;
      break;
    case 'w':
      dowho = 1;
      break;
    case '?':
      errflag = 1;
      break;
    }

  if (errflag || optind < argc || !(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "usage: convers"
	    " [-c channel]"
	    " [-h]"
	    " [-k]"
	    " [-l]"
	    " [-n note]"
	    " [-p]"
	    " [-s host:service]"
	    " [-u]"
	    " [-w]"
	    "\n");
    stop("");
  }

  close(3);
  if (socket(addr->sa_family, SOCK_STREAM, 0) != 3) stop(*argv);
  if (connect(3, addr, addrlen)) stop(*argv);

  if (dohosts || dokick || dolinks || dopeers || dousers || dowho) {
    *buffer = 0;
    if (dopeers)
      strcat(buffer, "/PEERS\n");
    if (dolinks)
      strcat(buffer, "/LINKS\n");
    if (dohosts)
      strcat(buffer, "/HOSTS\n");
    if (dousers)
      strcat(buffer, "/USERS\n");
    if (dowho)
      strcat(buffer, "/WHO\n");
    if (dokick)
      strcat(buffer, "/KICK\n");
    strcat(buffer, "/QUIT\n");
    if (write(3, buffer, strlen(buffer)) < 0)
      stop(*argv);
    for (;;) {
      size = read(3, buffer, sizeof(buffer));
      if (size < 0)
	stop(*argv);
      if (size == 0)
	stop("");
      if (write(1, buffer, size) < 0)
	stop(*argv);
    }
  }

  name = getenv("LOGNAME");
  if (!note && (pw = getpwnam(name))) {
    note = pw->pw_gecos;
    if ((cp = strchr(note, ','))) *cp = 0;
  }
  if (!note || !*note) note = "@";

  sprintf(inbuf, "/NAME %s %d %s\n", name, channel, note);
  if (write(3, inbuf, strlen(inbuf)) < 0) stop(*argv);

  FD_ZERO(&chkread);
  FD_SET(0, &chkread);
  FD_SET(3, &chkread);

  for (; ; ) {
    actread = chkread;
    select(4, SEL_ARG(&actread), 0, 0, 0);
    if (FD_ISSET(0, &actread)) {
      do {
	if ((size = read(0, buffer, sizeof(buffer))) <= 0) stop("");
	for (i = 0; i < size; i++) {
	  c = buffer[i];
	  if (c == '\r') c = '\n';
	  if (c == erase_char) {
	    if (incnt) {
	      incnt--;
	      if (echo && write(1, "\b \b", 3) < 0) stop(*argv);
	    }
	  } else if (c == kill_char) {
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
#ifdef ibm032
	      if (ioctl(0, TIOCSETP, &prev_sgttyb)) stop(*argv);
	      system(inbuf + 1);
	      if (ioctl(0, TIOCSETP, &curr_sgttyb)) stop(*argv);
#else
	      if (tcsetattr(0, TCSANOW, &prev_termios)) stop(*argv);
	      system(inbuf + 1);
	      if (tcsetattr(0, TCSANOW, &curr_termios)) stop(*argv);
#endif
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

