static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/convers.c,v 1.5 1988/09/12 20:20:52 dk5sg Exp $";

#include <sys/types.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <termio.h>
#include <time.h>

extern char  *getenv();
extern char  *optarg;
extern int  optind;
extern void exit();
extern void perror();

#define PORT 3600

static struct termio prev_termio;
static struct utsname utsname;

/*---------------------------------------------------------------------------*/

/*
 * Executes read(2), cares about EINTR
 */

int  doread(fildes, buf, nbyte)
int  fildes;
char  *buf;
unsigned  nbyte;
{
  int  cnt;

  for (; ; ) {
    cnt = read(fildes, buf, nbyte);
    if (cnt >= 0) return cnt;
    if (errno != EINTR) return (-1);
  }
}

/*---------------------------------------------------------------------------*/

/*
 * Executes write(2), retries until all data is send, cares about EINTR
 *
 * WARNING: SIGPIPE should be ignored by the calling context!
 */

int  dowrite(fildes, buf, nbyte)
int  fildes;
char  *buf;
unsigned  nbyte;
{
  int  cnt, len;

  len = nbyte;
  while (nbyte) {
    cnt = write(fildes, buf, nbyte);
    if (cnt < 0) {
      if (errno != EINTR) return (-1);
      cnt = 0;
    }
    buf += cnt;
    nbyte -= cnt;
  }
  return len;
}

/*---------------------------------------------------------------------------*/

static int  sigpipe_handler(sig, code, scp)
int  sig;
int  code;
struct sigcontext *scp;
{
  scp->sc_syscall_action = SIG_RETURN;
}

/*---------------------------------------------------------------------------*/

static void stop(arg)
register char  *arg;
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

  char  *hostname;
  char  buffer[2048];
  char  c;
  char  inbuf[2048];
  char  outbuf[2048];
  int  ch;
  int  channel = 0;
  int  echo;
  int  errflag = 0;
  int  i;
  int  incnt = 0;
  int  mask;
  int  outcnt = 0;
  int  port = PORT;
  int  size;
  static struct sockaddr_in addr;
  struct hostent *hp;
  struct sigvec vec;
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

  if (uname(&utsname)) stop(*argv);
  hostname = utsname.nodename;

  while ((ch = getopt(argc, argv, "c:h:p:")) != EOF)
    switch (ch) {
    case 'c':
      channel = atoi(optarg);
      break;
    case 'h':
      hostname = optarg;
      break;
    case 'p':
      port = atoi(optarg);
      break;
    case '?':
      errflag = 1;
      break;
    }

  if (errflag || optind < argc) {
    fprintf(stderr, "usage: convers [-c channel] [-h host] [-p port]\n");
    stop(0);
  }

  addr.sin_family = AF_INET;
  if (!(hp = gethostbyname(hostname))) {
    fprintf(stderr, "%s: %s not found in /etc/hosts\n", *argv, hostname);
    stop(0);
  }
  addr.sin_addr.s_addr = ((struct in_addr *) (hp->h_addr))->s_addr;
  addr.sin_port = port;
  close(3);
  if (socket(AF_INET, SOCK_STREAM, 0) != 3) stop(*argv);
  if (connect(3, &addr, sizeof(struct sockaddr_in ))) stop(*argv);

  i = sprintf(inbuf, "/NAME %s %d\n", getenv("LOGNAME"), channel);
  if (dowrite(3, inbuf, (unsigned) i) < 0) stop(*argv);

  for (; ; ) {
    mask = 011;
    select(4, &mask, 0, 0, (struct timeval *) 0);
    if (mask & 1) {
      do {
	if ((size = doread(0, buffer, sizeof(buffer))) <= 0) stop(0);
	for (i = 0; i < size; i++) {
	  c = buffer[i];
	  if (c == '\r') c = '\n';
	  if (c == prev_termio.c_cc[VERASE]) {
	    if (incnt) {
	      incnt--;
	      if (echo && dowrite(1, "\b \b", 3) < 0) stop(*argv);
	    }
	  } else if (c == prev_termio.c_cc[VKILL]) {
	    for (; incnt; incnt--)
	      if (echo && dowrite(1, "\b \b", 3) < 0) stop(*argv);
	  } else if (echo && c == 18) {
	    if (dowrite(1, "^R\n", 3) < 0) stop(*argv);
	    if (dowrite(1, inbuf, (unsigned) incnt) < 0) stop(*argv);
	  } else {
	    inbuf[incnt++] = c;
	    if (echo && dowrite(1, &c, 1) < 0) stop(*argv);
	  }
	  if (c == '\n' || incnt == sizeof(inbuf) - 1) {
	    if (*inbuf == '!') {
	      inbuf[incnt] = '\0';
	      if (ioctl(0, TCSETA, &prev_termio)) stop(*argv);
	      system(inbuf + 1);
	      if (ioctl(0, TCSETA, &curr_termio)) stop(*argv);
	      if (dowrite(1, "!\n", 2) < 0) stop(*argv);
	    } else {
	      if (dowrite(3, inbuf, (unsigned) incnt) < 0) stop(*argv);
	    }
	    incnt = 0;
	  }
	}
      } while (incnt);
    } else {
      size = doread(3, buffer, sizeof(buffer));
      if (size <= 0) stop(0);
      for (i = 0; i < size; i++) {
	c = buffer[i];
	if (c != '\r') outbuf[outcnt++] = c;
	if (c == '\n' || outcnt == sizeof(outbuf)) {
	  if (dowrite(1, outbuf, (unsigned) outcnt) < 0) stop(*argv);
	  outcnt = 0;
	}
      }
    }
  }
}

