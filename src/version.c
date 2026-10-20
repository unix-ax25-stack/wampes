/* @(#) $Id: version.c,v 1.394 2016/03/13 14:44:58 dl9sau Exp $ */

/* The version part of the banner comes from version_git.h, which the Makefile
 * generates and does not track - this file is tracked and must stay unchanged
 * by a build, or the tree would report itself modified for ever.  Without git,
 * as in a source archive, that header falls back to the string this banner
 * carried from the 5th of March 2000 until 2026.
 *
 * The rest names what is compiled in and is still maintained by hand.
 */
#include "version_git.h"

static char id[] = "@(#)" WAMPES_VERSION
	"-DL9SAU-VJC-VCompat-KRNLIF-TUNTAP-DIGIARP-GDBM";

/* version control information */
char *Version = id + 4;

#include <time.h>

int
doversion(
int argc,
char *argv[],
void *p)
{
	char buf[64];
	time_t now = time(NULL);
	struct tm *tm = localtime(&now);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm);
	printf("Version: %s\n", Version);
	printf("       compiled %s\n", buf);
	printf("      Source code at https://github.com/unix-ax25-stack/wampes\n");
	return 0;
}
