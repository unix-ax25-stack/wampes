/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ttydriv.c,v 1.13 1992/08/24 10:09:46 deyke Exp $ */

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

char *Fkey_table[NUM_FKEY];
char *Fkey_ptr;

static int  ansimode;
static int  rawmode;

static void printchr __ARGS((int chr));
static void backchr __ARGS((int chr));
static void delchr __ARGS((int chr));
static void inschr __ARGS((int chr));
static void clreol __ARGS((void));

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

static void clreol()
{
  putchar('\033');
  if (ansimode) putchar('[');
  putchar('K');
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
  static int  Escape_save;
  static int  ansikey;
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
    Escape = Escape_save;
    if (pos == end) {
      *end++ = chr;
      printchr(*pos++);
    } else {
      for (p = end - 1; p >= pos; p--) p[1] = *p;
      *pos++ = chr;
      end++;
      inschr(chr);
    }
  } else if (ansikey) {
    ansikey = 0;
    switch (chr) {

    case 'A': /* up arrow */
      if (recall_buffer[PREV(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  clreol();
	}
	rptr = PREV(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	for (pos = linebuf; *pos; pos++) printchr(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    case 'B': /* down arrow */
      if (recall_buffer[rptr] && recall_buffer[NEXT(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  clreol();
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

    case 'P': /* ansi function key 1 */
      Fkey_ptr = Fkey_table[0];
      break;

    case 'Q': /* ansi function key 2 */
      Fkey_ptr = Fkey_table[1];
      break;

    case 'R': /* ansi function key 3 */
      Fkey_ptr = Fkey_table[2];
      break;

    case 'S': /* ansi function key 4 */
      Fkey_ptr = Fkey_table[3];
      break;

    case 'T': /* ansi function key 5 */
    case 'w': /* ansi function key 5 */
      Fkey_ptr = Fkey_table[4];
      break;

    case 'U': /* ansi function key 6 */
    case 'x': /* ansi function key 6 */
      Fkey_ptr = Fkey_table[5];
      break;

    case 'V': /* ansi function key 7 */
    case 'y': /* ansi function key 7 */
      Fkey_ptr = Fkey_table[6];
      break;

    case 'W': /* ansi function key 8 */
    case 'm': /* ansi function key 8 */
      Fkey_ptr = Fkey_table[7];
      break;

    case 'X': /* ansi function key 9 */
    case 't': /* ansi function key 9 */
      Fkey_ptr = Fkey_table[8];
      break;

    case 'Y': /* ansi function key 10 */
    case 'u': /* ansi function key 10 */
      Fkey_ptr = Fkey_table[9];
      break;

    }
  } else if (!esc) {
    switch (chr) {

#ifdef LINUX
    case 127: /* delete */
#else
    case '\b': /* backspace */
#endif
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
	*end   = '\0';
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
	clreol();
      }
      break;

    case 22: /* control V, quote next character */
      if (end - linebuf >= LINEMAX)
	putchar(7);
      else {
	Escape_save = Escape;
	Escape = 0;
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

#ifndef LINUX
    case 127: /* DEL, delete char */
      if (pos < end) {
	delchr(*pos);
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
      }
      break;
#endif

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
	  clreol();
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
	  clreol();
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
	clreol();
      }
      break;

    case 'O': /* ansi arrow or function key */
      ansimode = ansikey = 1;
      break;

    case 'P': /* delete char */
      if (pos < end) {
	delchr(*pos);
	for (p = pos + 1; p < end; p++) p[-1] = *p;
	end--;
      }
      break;

    case '[': /* ansi escape sequence */
      esc = ansimode = 1;
      break;

    case 'p': /* hp function key 1 */
    case 'q': /* hp function key 2 */
    case 'r': /* hp function key 3 */
    case 's': /* hp function key 4 */
    case 't': /* hp function key 5 */
    case 'u': /* hp function key 6 */
    case 'v': /* hp function key 7 */
    case 'w': /* hp function key 8 */
      Fkey_ptr = Fkey_table[chr - 'p'];
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

