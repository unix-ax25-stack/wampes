/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/aos/setsid.c,v 1.2 1994/11/28 10:40:05 deyke Exp $ */

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
