/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ttydriv.c,v 1.2 1990/02/14 16:17:44 deyke Exp $ */

/* TTY input driver */

#include <stdio.h>
#include <string.h>

#define LINESIZE     1024
#define RECALLSIZE   63

#define NEXT(i)      (((i) + 1) & RECALLSIZE)
#define PREV(i)      (((i) - 1) & RECALLSIZE)

static int  rawmode;

/*---------------------------------------------------------------------------*/

raw()
{
  rawmode = 1;
}

/*---------------------------------------------------------------------------*/

cooked()
{
  rawmode = 0;
}

/*---------------------------------------------------------------------------*/

/* Accept characters from the incoming tty, buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 * Returns the number of characters available for use; if non-zero,
 * also stashes a pointer to the character(s) in the "buf" argument.
 */

int  ttydriv(chr, buf)
char  chr, **buf;
{

  static char  linebuf[LINESIZE];
  static char  *end = linebuf;
  static char  *pos = linebuf;
  static char  *recall_buffer[RECALLSIZE+1];
  static int  esc;
  static int  rptr;
  static int  wptr;

  char  *p, *f, *t;
  int  cnt;

  if (rawmode) {
    *linebuf = chr;
    *buf = linebuf;
    return 1;
  }

  cnt = 0;
  if (!esc) {
    switch (chr) {

    case 0:
      break;

    case '\b': /* backspace */
      if (pos > linebuf) {
	for (p = pos; p < end; p++) p[-1] = *p;
	pos--;
	end--;
	putchar('\b');
	putchar('\033');
	putchar('P');
      }
      break;

    case '\n': /* linefeed */
    case '\r': /* return */
      *end = '\0';
      if (*linebuf && strcmp(linebuf, recall_buffer[PREV(wptr)])) {
	recall_buffer[wptr] = strcpy(malloc(end - linebuf + 1), linebuf);
	wptr = NEXT(wptr);
	if (recall_buffer[wptr]) {
	  free(recall_buffer[wptr]);
	  recall_buffer[wptr] = 0;
	}
      }
      rptr = wptr;
      *end++ = '\r';
      *end++ = '\n';
      puts("\033&s1A");     /* enable XmitFnctn, newline */
      cnt = end - linebuf;
      pos = end = linebuf;
      break;

    case 18: /* control R, redisplay line */
      putchar('\n');
      for (p = linebuf; p < end; p++) putchar(*p);
      while (p > pos) {
	p--;
	putchar('\b');
      }
      break;

    case 23: /* control W, delete word */
      p = pos;
      while (p > linebuf && p[-1] == ' ') p--;
      while (p > linebuf && p[-1] != ' ') p--;
      for (f = pos, t = p; f < end; ) *t++ = *f++;
      while (pos > p) {
	pos--;
	end--;
	putchar('\b');
	putchar('\033');
	putchar('P');
      }
      break;

    case 24: /* control X, delete line */
      if (end > linebuf) {
	while (pos > linebuf) {
	  pos--;
	  putchar('\b');
	}
	end = linebuf;
	putchar('\033');
	putchar('K');
      }
      break;

    case 27: /* escape */
      esc = 1;
      break;

    default:
      if (pos == end) {
	*pos++ = chr;
	end++;
	putchar(chr);
      } else {
	for (p = end - 1; p >= pos; p--) p[1] = *p;
	*pos++ = chr;
	end++;
	putchar('\033');
	putchar('Q');
	putchar(chr);
	putchar('\033');
	putchar('R');
      }
      break;

    }
  } else {
    switch (chr) {

    case 'C': /* cursor right */
      if (pos < end) {
	putchar(*pos++);
      } else {
	*pos++ = ' ';
	end++;
	putchar(' ');
      }
      break;

    case 'D': /* cursor left */
      if (pos > linebuf) {
	pos--;
	putchar('\b');
      }
      break;

    case 'F': /* cursor home down */
      while (pos < end) putchar(*pos++);
      break;

    case 'H': /* cursor home up */
    case 'h': /* cursor home up */
      while (pos > linebuf) {
	pos--;
	putchar('\b');
      }
      break;

    case 'J': /* clear display */
    case 'M': /* delete line */
      while (pos > linebuf) {
	pos--;
	putchar('\b');
      }
    case 'K': /* clear line */
      if (end > pos) {
	end = pos;
	putchar('\033');
	putchar('K');
      }
      break;

    case 'P': /* delete char */
      if (pos < end) {
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
	putchar('\033');
	putchar('P');
      }
      break;

    case 'U': /* next page (shift recall) */
      if (recall_buffer[rptr] && recall_buffer[NEXT(rptr)]) {
	rptr = NEXT(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	if (end > linebuf) {
	  while (pos > linebuf) {
	    pos--;
	    putchar('\b');
	  }
	  putchar('\033');
	  putchar('K');
	}
	for (pos = linebuf; *pos; pos++) putchar(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    case 'V': /* prev page (recall) */
      if (recall_buffer[PREV(rptr)]) {
	rptr = PREV(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	if (end > linebuf) {
	  while (pos > linebuf) {
	    pos--;
	    putchar('\b');
	  }
	  putchar('\033');
	  putchar('K');
	}
	for (pos = linebuf; *pos; pos++) putchar(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    default:
      break;

    }
    esc = 0;
  }
  fflush(stdout);
  if (cnt) *buf = linebuf;
  return cnt;
}

