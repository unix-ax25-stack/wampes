#ifndef __lint
static const char rcsid[] = "@(#) $Id: ms2mm.c,v 1.5 1996/08/12 18:52:58 deyke Exp $";
#endif

#include <stdio.h>
#include <string.h>

/*---------------------------------------------------------------------------*/

/* What gets() should always have been: a read that knows how big the buffer
 * is.  gets() was removed from C11 and current glibc does not declare it any
 * more, so this is a build error and not a matter of taste - and it was a
 * buffer overrun waiting for a long line either way.
 *
 * Not a plain fgets(): gets() dropped the newline and fgets() keeps it, and
 * the callers below compare whole lines.  A long line is truncated here where
 * it used to overrun; the remainder turns up as the next line.
 */

static char *read_line(char *buf, size_t size)
{
  char *cp;

  if (!fgets(buf, (int) size, stdin))
    return 0;
  if ((cp = strchr(buf, '\n')))
    *cp = '\0';
  return buf;
}

static void print_header(int n)
{

  char *cp;
  char line[1024];

  printf(".H %d \"", n);
  if (!read_line(line, sizeof(line)))
    line[0] = '\0';
  cp = strpbrk(line, "<[\\");
  if (cp > line) {
    cp[-1] = 0;
    printf("%s\" \" %s\"\n", line, cp);
  } else {
    printf("%s\"\n", line);
  }
}

/*---------------------------------------------------------------------------*/

int main(void)
{
  char line[1024];

  while (read_line(line, sizeof(line))) {
    if (!strcmp(line, ".NH 1")) {
      print_header(1);
    } else if (!strcmp(line, ".NH 2")) {
      print_header(2);
    } else if (!strcmp(line, ".NH 3")) {
      print_header(3);
    } else if (!strcmp(line, ".NH 4")) {
      print_header(4);
    } else if (!strcmp(line, ".LP")) {
      puts(".P");
    } else {
      puts(line);
    }
  }
  return 0;
}

