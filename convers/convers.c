static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/convers.c,v 1.6 1989/01/15 19:55:46 dk5sg Exp $";

#include <sys/types.h>

#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <termio.h>
#include <time.h>

extern char  *getenv();
extern char  *optarg;
extern int  optind;
extern struct sockaddr *build_sockaddr();
extern void exit();
extern void perror();

static struct termio prev_termio;

/*---------------------------------------------------------------------------*/

static int  sigpipe_handler(sig, code, scp)
int  sig, code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

static void stop(arg)
char  *arg;
{
  if (arg) perror(arg);
  ioctl(0, TCSETA, &prev_termio);
  exit(0);
}

/*---------------------------------------------------------------------------*/

main(argc, argv)
int  argc;
char  **argv;
{

  char  *server = "unix:/tcp/sockets/convers";
  char  buffer[2048];
  char  c;
  char  inbuf[2048];
  char  outbuf[2048];
  int  addrlen;
  int  ch;
  int  channel = 0;
  int  echo;
  int  errflag = 0;
  int  i;
  int  incnt = 0;
  int  mask;
  int  outcnt = 0;
  int  size;
  struct sigvec vec;
  struct sockaddr *addr;
  struct termio curr_termio;

  vec.sv_mask = vec.sv_flags = 0;
  vec.sv_handler = sigpipe_handler;
  sigvector(SIGPIPE, &vec, (struct sigvec *) 0);

  if (ioctl(0, TCGETA, &prev_termio)) stop(*argv);
  if (ioctl(0, TCGETA, &curr_termio)) stop(*argv);
  echo = curr_termio.c_lflag & ECHO;
  curr_termio.c_lflag = 0;
  curr_termio.c_cc[VMIN] = 1;
  curr_termio.c_cc[VTIME] = 0;
  if (ioctl(0, TCSETA, &curr_termio)) stop(*argv);

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
    stop(0);
  }

  close(3);
  if (socket(addr->sa_family, SOCK_STREAM, 0) != 3) stop(*argv);
  if (connect(3, addr, addrlen)) stop(*argv);

  sprintf(inbuf, "/NAME %s %d\n", getenv("LOGNAME"), channel);
  if (write(3, inbuf, (unsigned) strlen(inbuf)) < 0) stop(*argv);

  for (; ; ) {
    mask = 011;
    select(4, &mask, 0, 0, (struct timeval *) 0);
    if (mask & 1) {
      do {
	if ((size = read(0, buffer, sizeof(buffer))) <= 0) stop(0);
	for (i = 0; i < size; i++) {
	  c = buffer[i];
	  if (c == '\r') c = '\n';
	  if (c == prev_termio.c_cc[VERASE]) {
	    if (incnt) {
	      incnt--;
	      if (echo && write(1, "\b \b", 3) < 0) stop(*argv);
	    }
	  } else if (c == prev_termio.c_cc[VKILL]) {
	    for (; incnt; incnt--)
	      if (echo && write(1, "\b \b", 3) < 0) stop(*argv);
	  } else if (echo && c == 18) {
	    if (write(1, "^R\n", 3) < 0) stop(*argv);
	    if (write(1, inbuf, (unsigned) incnt) < 0) stop(*argv);
	  } else {
	    inbuf[incnt++] = c;
	    if (echo && write(1, &c, 1) < 0) stop(*argv);
	  }
	  if (c == '\n' || incnt == sizeof(inbuf) - 1) {
	    if (*inbuf == '!') {
	      inbuf[incnt] = '\0';
	      if (ioctl(0, TCSETA, &prev_termio)) stop(*argv);
	      system(inbuf + 1);
	      if (ioctl(0, TCSETA, &curr_termio)) stop(*argv);
	      if (write(1, "!\n", 2) < 0) stop(*argv);
	    } else {
	      if (write(3, inbuf, (unsigned) incnt) < 0) stop(*argv);
	    }
	    incnt = 0;
	  }
	}
      } while (incnt);
    } else {
      size = read(3, buffer, sizeof(buffer));
      if (size <= 0) stop(0);
      for (i = 0; i < size; i++) {
	c = buffer[i];
	if (c != '\r') outbuf[outcnt++] = c;
	if (c == '\n' || outcnt == sizeof(outbuf)) {
	  if (write(1, outbuf, (unsigned) outcnt) < 0) stop(*argv);
	  outcnt = 0;
	}
      }
    }
  }
}

