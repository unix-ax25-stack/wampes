/* @(#) $Id: setsid.c,v 1.4 1996/08/12 18:53:54 deyke Exp $ */

/* setsid.c
 * remove tty association
 * 17/01/94 J.Jaeger, PA3EFU.
 */

#include <fcntl.h>
#include <sys/ioctl.h>

setsid()
{
	int fd;

	fd = open("/dev/tty",O_RDWR);

	ioctl(fd, TIOCNOTTY, 0);

	close(fd);

	close(0);
	close(1);
	close(2);

}
