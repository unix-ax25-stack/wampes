static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/convers.c,v 1.3 1988/09/01 21:29:32 dk5sg Exp $";

#include <sys/types.h>

#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <termio.h>
#include <time.h>

extern char  *getenv();
extern void exit();
extern void perror();

#define PORT 3600

static struct termio prev_termio;
static struct utsname utsname;

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
  int  echo;
  int  i;
  int  incnt = 0;
  int  mask;
  int  outcnt = 0;
  int  size;
  static struct sockaddr_in addr;
  struct hostent *hp;
  struct termio curr_termio;

  if (ioctl(0, TCGETA, &prev_termio)) stop(*argv);
  if (ioctl(0, TCGETA, &curr_termio)) stop(*argv);
  echo = curr_termio.c_lflag & ECHO;
  curr_termio.c_lflag = 0;
  curr_termio.c_cc[VMIN] = 1;
  curr_termio.c_cc[VTIME] = 0;
  if (ioctl(0, TCSETA, &curr_termio)) stop(*argv);

  if (uname(&utsname)) stop(*argv);
  addr.sin_family = AF_INET;
  if (!(hp = gethostbyname(hostname = (argc >= 2) ? argv[1] : utsname.nodename))) {
    fprintf(stderr, "%s: %s not found in /etc/hosts\n", *argv, hostname);
    stop(0);
  }
  addr.sin_addr.s_addr = ((struct in_addr *) (hp->h_addr))->s_addr;
  addr.sin_port = (argc >= 3) ? atoi(argv[2]) : PORT;
  close(3);
  if (socket(AF_INET, SOCK_STREAM, 0) != 3) stop(*argv);
  if (connect(3, &addr, sizeof(struct sockaddr_in ))) stop(*argv);

  i = sprintf(inbuf, "/NAME %s\n", getenv("LOGNAME"));
  if (write(3, inbuf, (unsigned) i) != i) stop(*argv);

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
	      if (echo && write(1, "\b \b", 3) != 3) stop(*argv);
	    }
	  } else if (c == prev_termio.c_cc[VKILL]) {
	    for (; incnt; incnt--)
	      if (echo && write(1, "\b \b", 3) != 3) stop(*argv);
	  } else if (echo && c == 18) {
	    if (write(1, "^R\n", 3) != 3) stop(*argv);
	    if (write(1, inbuf, (unsigned) incnt) != incnt) stop(*argv);
	  } else {
	    inbuf[incnt++] = c;
	    if (echo && write(1, &c, 1) != 1) stop(*argv);
	  }
	  if (c == '\n' || incnt == sizeof(inbuf) - 1) {
	    if (*inbuf == '!') {
	      inbuf[incnt] = '\0';
	      if (ioctl(0, TCSETA, &prev_termio)) stop(*argv);
	      system(inbuf + 1);
	      if (ioctl(0, TCSETA, &curr_termio)) stop(*argv);
	      if (write(1, "!\n", 2) != 2) stop(*argv);
	    } else {
	      if (write(3, inbuf, (unsigned) incnt) != incnt) stop(*argv);
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
	  if (write(1, outbuf, (unsigned) outcnt) != outcnt) stop(*argv);
	  outcnt = 0;
	}
      }
    }
  }
}

