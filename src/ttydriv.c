/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ttydriv.c,v 1.9 1991/05/29 12:02:40 deyke Exp $ */

/* TTY input line editing
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "tty.h"

#define LINESIZE     1024
#define LINEMAX      (LINESIZE-5)
#define RECALLSIZE   63

#define NEXT(i)      (((i) + 1) & RECALLSIZE)
#define PREV(i)      (((i) - 1) & RECALLSIZE)

static int  ansimode;
static int  rawmode;

static void printchr __ARGS((int chr));
static void backchr __ARGS((int chr));
static void delchr __ARGS((int chr));
static void inschr __ARGS((int chr));

/*---------------------------------------------------------------------------*/

int  raw()
{
  rawmode = 1;
  return 0;
}

/*---------------------------------------------------------------------------*/

int  cooked()
{
  rawmode = 0;
  return 0;
}

/*---------------------------------------------------------------------------*/

static void printchr(chr)
int  chr;
{
  chr &= 0xff;
  if (chr < 32) {
    putchar('^');
    putchar(chr + 64);
  } else if (chr < 127) {
    putchar(chr);
  } else if (chr < 128) {
    putchar('^');
    putchar('?');
  } else if (chr < 159) {
    putchar('^');
    putchar(chr - 32);
  } else if (chr < 160) {
    putchar('^');
    putchar('>');
  } else if (chr < 255) {
    putchar(chr);
  } else {
    putchar('^');
    putchar('/');
  }
}

/*---------------------------------------------------------------------------*/

static void backchr(chr)
int  chr;
{
  putchar('\b');
  chr &= 0x7f;
  if (chr < 32 || chr == 127)
    putchar('\b');
}

/*---------------------------------------------------------------------------*/

static void delchr(chr)
int  chr;
{
  putchar('\033');
  if (ansimode) putchar('[');
  putchar('P');
  chr &= 0x7f;
  if (chr < 32 || chr == 127) {
    putchar('\033');
    if (ansimode) putchar('[');
    putchar('P');
  }
}

/*---------------------------------------------------------------------------*/

static void inschr(chr)
int  chr;
{
  int  c;

  if (ansimode) {
    c = chr & 0x7f;
    printf("\033[%d@", (c < 32 || c == 127) ? 2 : 1);
    printchr(chr);
  } else {
    putchar('\033');
    putchar('Q');
    printchr(chr);
    putchar('\033');
    putchar('R');
  }
}

/*---------------------------------------------------------------------------*/

/* Accept characters from the incoming tty, buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 * Returns the number of characters available for use; if non-zero,
 * also stashes a pointer to the character(s) in the "buf" argument.
 */

int  ttydriv(chr, buf)
int  chr;
char  **buf;
{

  static char  linebuf[LINESIZE];
  static char  *end = linebuf;
  static char  *pos = linebuf;
  static char  *recall_buffer[RECALLSIZE+1];
  static int  esc, quote;
  static int  rptr, wptr;

  char  *p, *f, *t;
  int  cnt;

  if (rawmode) {
    *linebuf = chr;
    *buf = linebuf;
    return 1;
  }

  if (!chr) return 0;
  cnt = 0;
  if (quote) {
    delchr(quote);
    quote = 0;
    if (pos == end) {
      *end++ = chr;
      printchr(*pos++);
    } else {
      for (p = end - 1; p >= pos; p--) p[1] = *p;
      *pos++ = chr;
      end++;
      inschr(chr);
    }
  } else if (!esc) {
    switch (chr) {

    case '\b': /* backspace */
      if (pos > linebuf) {
	backchr(*--pos);
	delchr(*pos);
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
      }
      break;

    case '\n': /* linefeed */
    case '\r': /* return */
    case 20:   /* control T, transmit line */
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
      if (chr != 20) {
	*end++ = '\r';
	*end++ = '\n';
	putchar('\n');
      }
      cnt = end - linebuf;
      pos = end = linebuf;
      break;

    case 18: /* control R, redisplay line */
      putchar('\n');
      for (p = linebuf; p < end; p++) printchr(*p);
      while (p > pos) backchr(*--p);
      break;

    case 21: /* control U, delete line */
    case 24: /* control X, delete line */
      if (end > linebuf) {
	while (pos > linebuf) backchr(*--pos);
	end = linebuf;
	putchar('\033');
	if (ansimode) putchar('[');
	putchar('K');
      }
      break;

    case 22: /* control V, quote next character */
      if (end - linebuf >= LINEMAX)
	putchar(7);
      else {
	quote = '^';
	if (pos == end)
	  printchr(quote);
	else
	  inschr(quote);
	backchr(quote);
      }
      break;

    case 23: /* control W, delete word */
      p = pos;
      while (p > linebuf && p[-1] == ' ') { backchr(*--p); delchr(*p); }
      while (p > linebuf && p[-1] != ' ') { backchr(*--p); delchr(*p); }
      for (f = pos, t = p; f < end; ) *t++ = *f++;
      pos = p;
      end = t;
      break;

    case 27: /* escape */
      esc = 1;
      ansimode = 0;
      break;

    case 127: /* DEL, delete char */
      if (pos < end) {
	delchr(*pos);
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
      }
      break;

    default:
      if (end - linebuf >= LINEMAX)
	putchar(7);
      else if (pos == end) {
	*end++ = chr;
	printchr(*pos++);
      } else {
	for (p = end - 1; p >= pos; p--) p[1] = *p;
	*pos++ = chr;
	end++;
	inschr(chr);
      }
      break;

    }
  } else {
    esc = 0;
    switch (chr) {

    case 'A': /* up arrow */
    case 'V': /* prev page */
      if (recall_buffer[PREV(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  putchar('\033');
	  if (ansimode) putchar('[');
	  putchar('K');
	}
	rptr = PREV(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	for (pos = linebuf; *pos; pos++) printchr(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    case 'B': /* down arrow */
    case 'U': /* next page */
      if (recall_buffer[rptr] && recall_buffer[NEXT(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  putchar('\033');
	  if (ansimode) putchar('[');
	  putchar('K');
	}
	rptr = NEXT(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	for (pos = linebuf; *pos; pos++) printchr(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    case 'C': /* cursor right */
      if (pos - linebuf >= LINEMAX)
	putchar(7);
      else {
	if (pos == end) *end++ = ' ';
	printchr(*pos++);
      }
      break;

    case 'D': /* cursor left */
      if (pos > linebuf) backchr(*--pos);
      break;

    case 'F': /* cursor home down */
    case 'Y': /* end */
      while (pos < end) printchr(*pos++);
      break;

    case 'H': /* cursor home up */
    case 'h': /* cursor home up */
      while (pos > linebuf) backchr(*--pos);
      break;

    case 'J': /* clear display */
    case 'M': /* delete line */
      while (pos > linebuf) backchr(*--pos);
    case 'K': /* clear line */
      if (end > pos) {
	end = pos;
	putchar('\033');
	if (ansimode) putchar('[');
	putchar('K');
      }
      break;

    case 'P': /* delete char */
      if (pos < end) {
	delchr(*pos);
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
      }
      break;

    case '[': /* ansi escape sequence */
      esc = 1;
      ansimode = 1;
      break;

    default:
      putchar('\033');
      if (ansimode) putchar('[');
      putchar(chr);
      break;

    }
  }
  fflush(stdout);
  if (cnt) *buf = linebuf;
  return cnt;
}

