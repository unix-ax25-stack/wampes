#ifndef __lint
static const char rcsid[] = "@(#) $Id: convers.c,v 1.23 1996/10/28 22:46:37 deyke Exp $";
#endif

#include <sys/types.h>

#include <stdio.h>      /* must be before pwd.h */

#include <ctype.h>
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

enum e_what {
  ONE_TOKEN,
  REST_OF_LINE
};

enum e_case {
  KEEP_CASE,
  LOWER_CASE
};

static const char *progname;

static int fdin = 0;
static int fdout = 1;
static int fdsock = -1;

static int echo;
static int erase_char;
static int kill_char;
static int restore_tty;

static int beep_flag;

#ifdef ibm032
static struct sgttyb prev_sgttyb;
static struct sgttyb curr_sgttyb;
#else
static struct termios prev_termios;
static struct termios curr_termios;
#endif

/*---------------------------------------------------------------------------*/

static void prev_tty(void)
{
  if (restore_tty) {
#ifdef ibm032
    ioctl(fdin, TIOCSETP, &prev_sgttyb);
#else
    tcsetattr(fdin, TCSANOW, &prev_termios);
#endif
  }
  restore_tty = 0;
}

/*---------------------------------------------------------------------------*/

static void stop(const char *arg)
{
  if (*arg) {
    perror(arg);
  }
  prev_tty();
  exit(0);
}

/*---------------------------------------------------------------------------*/

static void curr_tty(void)
{
#ifdef ibm032
  if (ioctl(fdin, TIOCSETP, &curr_sgttyb)) {
    stop(progname);
  }
#else
  if (tcsetattr(fdin, TCSANOW, &curr_termios)) {
    stop(progname);
  }
#endif
  restore_tty = 1;
}

/*---------------------------------------------------------------------------*/

static void set_tty(void)
{
#ifdef ibm032
  if (ioctl(fdin, TIOCGETP, &prev_sgttyb)) {
    stop(progname);
  }
  echo = prev_sgttyb.sg_flags & ECHO;
  erase_char = prev_sgttyb.sg_erase;
  kill_char = prev_sgttyb.sg_kill;
  curr_sgttyb = prev_sgttyb;
  curr_sgttyb.sg_flags = CRMOD | CBREAK;
#else
  if (tcgetattr(fdin, &prev_termios)) {
    stop(progname);
  }
  echo = prev_termios.c_lflag & ECHO;
  erase_char = prev_termios.c_cc[VERASE];
  kill_char = prev_termios.c_cc[VKILL];
  curr_termios = prev_termios;
  curr_termios.c_lflag = 0;
  curr_termios.c_cc[VMIN] = 1;
  curr_termios.c_cc[VTIME] = 0;
#endif
  curr_tty();
}

/*---------------------------------------------------------------------------*/

static int doread(int fd, char *buffer, int numbytes)
{
  int n;

  n = read(fd, buffer, numbytes);
  if (n < 0) {
    stop(progname);
  }
  if (!n) {
    stop("");
  }
  return n;
}

/*---------------------------------------------------------------------------*/

static void dowrite(int fd, const char *buffer, int numbytes)
{
  if (write(fd, buffer, numbytes) < 0) {
    stop(progname);
  }
}

/*---------------------------------------------------------------------------*/

static void dologin(int channel, const char *note)
{

  char buffer[2048];
  char *cp;
  char *name;
  struct passwd *pw;

  name = getenv("LOGNAME");
  if (!note && (pw = getpwnam(name))) {
    note = pw->pw_gecos;
    if ((cp = strchr(note, ','))) {
      *cp = 0;
    }
  }
  if (!note || !*note) {
    note = "@";
  }
  sprintf(buffer, "/NAME %s %d %s\n", name, channel, note);
  dowrite(fdsock, buffer, strlen(buffer));
}

/*---------------------------------------------------------------------------*/

static char *getarg(char *line, enum e_what what, enum e_case how)
{

  char *arg;
  char *cp;
  static char *next_arg;

  if (line) {
    for (next_arg = cp = line; *cp; cp++) ;
    while (--cp >= line && isspace(*cp & 0xff)) ;
    cp[1] = 0;
  }
  while (isspace(*next_arg & 0xff))
    next_arg++;
  arg = next_arg;
  if (what == ONE_TOKEN) {
    while (*next_arg && !isspace(*next_arg & 0xff))
      next_arg++;
    if (*next_arg)
      *next_arg++ = 0;
  } else {
    next_arg = "";
  }
  if (how == LOWER_CASE) {
    for (cp = arg; *cp; cp++) {
      if (*cp >= 'A' && *cp <= 'Z')
	*cp = tolower(*cp);
    }
  }
  return arg;
}

/*---------------------------------------------------------------------------*/

static void beep_command(void)
{

  char buffer[2048];
  char *arg;

  arg = getarg(0, ONE_TOKEN, LOWER_CASE);
  if (!strcmp(arg, "on")) {
    beep_flag = 1;
  } else if (!strcmp(arg, "off")) {
    beep_flag = 0;
  }
  sprintf(buffer, "*** BEEP is %s\n", beep_flag ? "ON" : "OFF");
  dowrite(fdout, buffer, strlen(buffer));
}

/*---------------------------------------------------------------------------*/

static void help_command(void)
{
  const char *help_text =
  "==================================== LOCAL ====================================\n"
  "Commands may be abbreviated.  Commands are:\n"

  "/?                   Show help information\n"
  "/HELP                Show help information\n"

  "/BEEP [on|off]       Display/set BEEP flag\n"

  "==================================== REMOTE ===================================\n";

  char commandbuf[2048];

  dowrite(fdout, help_text, strlen(help_text));
  sprintf(commandbuf, "/HELP %s\n", getarg(0, REST_OF_LINE, KEEP_CASE));
  dowrite(fdsock, commandbuf, strlen(commandbuf));
}

/*---------------------------------------------------------------------------*/

static void process_line(const char *line, int numbytes)
{

  struct command {
    const char *c_name;
    void (*c_fnc) (void);
  };

  static const struct command commands[] =
  {

    {"?", help_command},

    {"beep", beep_command},
    {"help", help_command},

    {0, 0}
  };

  char commandbuf[2048];
  char *arg;
  const struct command *cp;
  int arglen;

  if (*line == '!') {
    prev_tty();
    system(line + 1);
    curr_tty();
    dowrite(fdout, "!\n", 2);
  } else if (*line == '/') {
    strcpy(commandbuf, line + 1);
    arg = getarg(commandbuf, ONE_TOKEN, LOWER_CASE);
    arglen = strlen(arg);
    for (cp = commands; cp->c_name; cp++) {
      if (!strncmp(cp->c_name, arg, arglen)) {
	cp->c_fnc();
	return;
      }
    }
    dowrite(fdsock, line, numbytes);
  } else if (numbytes > 1) {
    dowrite(fdsock, line, numbytes);
  }
}

/*---------------------------------------------------------------------------*/

static void doinput(void)
{

  char buffer[2048];
  char chr;
  int i;
  int size;
  static char inbuf[2048];
  static int incnt;

  do {
    size = doread(fdin, buffer, sizeof(buffer));
    for (i = 0; i < size; i++) {
      chr = buffer[i];
      if (chr == '\r') {
	chr = '\n';
      }
      if (chr == erase_char) {
	if (incnt) {
	  incnt--;
	  if (echo) {
	    dowrite(fdout, "\b \b", 3);
	  }
	}
      } else if (chr == kill_char) {
	for (; incnt; incnt--) {
	  if (echo) {
	    dowrite(fdout, "\b \b", 3);
	  }
	}
      } else if (echo && chr == 18) {
	dowrite(fdout, "^R\n", 3);
	dowrite(fdout, inbuf, incnt);
      } else {
	inbuf[incnt++] = chr;
	if (echo) {
	  dowrite(fdout, &chr, 1);
	}
      }
      if (chr == '\n' || incnt >= sizeof(inbuf) - 1) {
	inbuf[incnt] = 0;
	process_line(inbuf, incnt);
	incnt = 0;
      }
    }
  } while (incnt);
}

/*---------------------------------------------------------------------------*/

static void dooutput(void)
{

  char buffer[2048];
  char chr;
  int i;
  int size;
  static char outbuf[2048];
  static int outcnt;

  size = doread(fdsock, buffer, sizeof(buffer));
  for (i = 0; i < size; i++) {
    chr = buffer[i];
    if (chr != '\r') {
      outbuf[outcnt++] = chr;
    }
    if (chr == '\n' || outcnt >= sizeof(outbuf)) {
      dowrite(fdout, outbuf, outcnt);
      outcnt = 0;
      if (beep_flag) {
	dowrite(fdout, "\007", 1);
      }
    }
  }
}

/*---------------------------------------------------------------------------*/

int main(int argc, char **argv)
{

  char buffer[2048];
  char *note = 0;
  const char *server = "unix:/tcp/sockets/convers";
  int addrlen;
  int channel = 0;
  int chr;
  int dohosts = 0;
  int dokick = 0;
  int dolinks = 0;
  int dopeers = 0;
  int dousers = 0;
  int dowho = 0;
  int errflag = 0;
  struct fd_set rmask;
  struct sockaddr *addr = 0;

  progname = *argv;

  signal(SIGPIPE, SIG_IGN);

  while ((chr = getopt(argc, argv, "c:hkln:ps:uw")) != EOF) {
    switch (chr) {
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

  fdsock = socket(addr->sa_family, SOCK_STREAM, 0);
  if (fdsock < 0 || connect(fdsock, addr, addrlen)) {
    stop(progname);
  }

  if (dohosts || dokick || dolinks || dopeers || dousers || dowho) {
    *buffer = 0;
    if (dopeers) {
      strcat(buffer, "/PEERS\n");
    }
    if (dolinks) {
      strcat(buffer, "/LINKS\n");
    }
    if (dohosts) {
      strcat(buffer, "/HOSTS\n");
    }
    if (dousers) {
      strcat(buffer, "/USERS\n");
    }
    if (dowho) {
      strcat(buffer, "/WHO\n");
    }
    if (dokick) {
      strcat(buffer, "/KICK\n");
    }
    strcat(buffer, "/QUIT\n");
    dowrite(fdsock, buffer, strlen(buffer));
    for (;;) {
      dowrite(fdout, buffer, doread(fdsock, buffer, sizeof(buffer)));
    }
  }

  dologin(channel, note);

  set_tty();

  for (;;) {
    FD_ZERO(&rmask);
    FD_SET(fdin, &rmask);
    FD_SET(fdsock, &rmask);
    select(fdsock + 1, SEL_ARG(&rmask), 0, 0, 0);
    if (FD_ISSET(fdin, &rmask)) {
      doinput();
    } else if (FD_ISSET(fdsock, &rmask)) {
      dooutput();
    }
  }
}
