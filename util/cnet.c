#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <netdb.h>
#include <termio.h>
#include <time.h>
#include <sys/utsname.h>

extern void exit();
extern void perror();

static struct utsname utsname;

main(argc, argv)
int  argc;
char  **argv;
{

  char  *hostname, buffer[1024];
  int  i, mask, size;
  static struct sockaddr_in addr;
  struct hostent *hp;
  struct termio termio, termio_save;

  if (uname(&utsname)) {
    perror(argv[0]);
    exit(1);
  }
  addr.sin_family = AF_INET;
  if (!(hp = gethostbyname(hostname = argc >= 2 ? argv[1] : utsname.nodename))) {
    fprintf(stderr, "%s: %s not found in /etc/hosts\n", argv[0], hostname);
    exit(1);
  }
  addr.sin_addr.s_addr = ((struct in_addr *)(hp->h_addr))->s_addr;
  addr.sin_port = 4712;
  close(3);
  if (socket(AF_INET, SOCK_STREAM, 0) != 3) {
    perror(argv[0]);
    exit(1);
  }
  if (connect(3, &addr, sizeof(struct sockaddr_in )) == -1) {
    perror(argv[0]);
    exit(1);
  }
  ioctl(0, TCGETA, &termio);
  ioctl(0, TCGETA, &termio_save);
  termio.c_lflag = 0;
  termio.c_cc[VMIN] = 1;
  termio.c_cc[VTIME] = 0;
  ioctl(0, TCSETA, &termio);

  for (; ; ) {
    mask = 011;
    select(4, &mask, 0, 0, (struct timeval *) 0);
    if (mask & 1) {
      size = read(0, buffer, sizeof(buffer));
      if (size <= 0) break;
      if (write(3, buffer, (unsigned) size) != size) break;
    } else {
      size = read(3, buffer, sizeof(buffer));
      if (size <= 0) break;
      for (i = 0; i < size; i++)
	if (buffer[i] != '\r') putchar(buffer[i]);
      fflush(stdout);
    }
  }

  ioctl(0, TCSETA, &termio_save);
  return 0;
}
