/* TTY input driver */

#include <stdio.h>

#include "global.h"

#define DEL_CHAR        8
#define DEL_WORD        23
#define DEL_LINE        24
#define LINESIZE        256
#define REDISPLAY       18
#define TTY_COOKED      0
#define TTY_RAW         1

static int  ttymode;

/*---------------------------------------------------------------------------*/

raw()
{
  ttymode = TTY_RAW;
}

/*---------------------------------------------------------------------------*/

cooked()
{
  ttymode = TTY_COOKED;
}

/*---------------------------------------------------------------------------*/

/* Accept characters from the incoming tty buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 * Returns the number of characters available for use; if non-zero,
 * also stashes a pointer to the character(s) in the "buf" argument.
 */

int  ttydriv(c, buf)
char  c;
char  **buf;
{

  char  *q;
  int  cnt = 0;
  static char  linebuf[LINESIZE];
  static char  *cp = linebuf;

  if (!buf) return 0;
  switch (ttymode) {
  case TTY_RAW:
    *cp++ = c;
    cnt = cp - linebuf;
    cp = linebuf;
    break;
  case TTY_COOKED:
    switch (uchar(c)) {
    case '\r':
    case '\n':
      *cp++ = '\r';
      *cp++ = '\n';
      putchar('\n');
      cnt = cp - linebuf;
      cp = linebuf;
      break;
    case DEL_CHAR:
      if (cp > linebuf) {
	cp--;
	putchar('\b');
	putchar(' ');
	putchar('\b');
      }
      break;
    case DEL_WORD:
      while (cp > linebuf && cp[-1] == ' ') {
	cp--;
	putchar('\b');
	putchar(' ');
	putchar('\b');
      }
      while (cp > linebuf && cp[-1] != ' ') {
	cp--;
	putchar('\b');
	putchar(' ');
	putchar('\b');
      }
      break;
    case DEL_LINE:
      while (cp > linebuf) {
	cp--;
	putchar('\b');
	putchar(' ');
	putchar('\b');
      }
      break;
    case REDISPLAY:
      putchar('^');
      putchar('R');
      putchar('\n');
      for (q = linebuf; q < cp; q++) putchar(*q);
      break;
    default:
      *cp++ = c;
      putchar(c);
      if (cp >= linebuf + LINESIZE - 1) {
	cnt = cp - linebuf;
	cp = linebuf;
      }
      break;
    }
  }
  fflush(stdout);
  *buf = cnt ? linebuf : NULLCHAR;
  return cnt;
}
