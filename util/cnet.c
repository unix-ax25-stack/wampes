static char  rcsid[] = "@(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/cnet.c,v 1.2 1989/01/16 20:44:49 dk5sg Exp $";

#include <sys/types.h>

#include <stdio.h>
#include <sys/socket.h>
#include <termio.h>
#include <time.h>

extern struct sockaddr *build_sockaddr();
extern void exit();
extern void perror();

main(argc, argv)
int  argc;
char  **argv;
{

  char  *server;
  char  buffer[1024];
  int  addrlen;
  int  i, mask, size;
  struct sockaddr *addr;
  struct termio termio, termio_save;

  /*****
  server = (argc < 2) ? "unix:/tcp/sockets/netkbd" : argv[1];
  *****/
  server = (argc < 2) ? "loopback:netkbd" : argv[1];
  if (!(addr = build_sockaddr(server, &addrlen))) {
    fprintf(stderr, "%s: Cannot build address from \"%s\"\n", argv[0], server);
    exit(1);
  }
  close(3);
  if (socket(addr->sa_family, SOCK_STREAM, 0) != 3) {
    perror(argv[0]);
    exit(1);
  }
  if (connect(3, addr, addrlen)) {
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
    select(4, &mask, (int *) 0, (int *) 0, (struct timeval *) 0);
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

