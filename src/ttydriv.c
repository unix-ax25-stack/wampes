/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ttydriv.c,v 1.4 1990/03/19 12:33:51 deyke Exp $ */

/* TTY input driver */

#include <stdio.h>
#include <string.h>

#define LINESIZE     1024
#define LINEMAX      (LINESIZE-5)
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
  chr &= 0xff;
  if (chr < 32 || chr >= 127 && chr <= 159 || chr == 255) putchar('\b');
}

/*---------------------------------------------------------------------------*/

static void delchr(chr)
int  chr;
{
  putchar('\033');
  putchar('P');
  chr &= 0xff;
  if (chr < 32 || chr >= 127 && chr <= 159 || chr == 255) {
    putchar('\033');
    putchar('P');
  }
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
      putchar('\033');
      putchar('Q');
      printchr(chr);
      putchar('\033');
      putchar('R');
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

    case 22: /* control V, quote next character */
      if (end - linebuf >= LINEMAX)
	putchar(7);
      else {
	quote = '^';
	if (pos == end)
	  printchr(quote);
	else {
	  putchar('\033');
	  putchar('Q');
	  printchr(quote);
	  putchar('\033');
	  putchar('R');
	}
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

    case 24: /* control X, delete line */
      if (end > linebuf) {
	while (pos > linebuf) backchr(*--pos);
	end = linebuf;
	putchar('\033');
	putchar('K');
      }
      break;

    case 27: /* escape */
      esc = 1;
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
	putchar('\033');
	putchar('Q');
	printchr(chr);
	putchar('\033');
	putchar('R');
      }
      break;

    }
  } else {
    switch (chr) {

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

    case 'U': /* next page (shift recall) */
      if (recall_buffer[rptr] && recall_buffer[NEXT(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  putchar('\033');
	  putchar('K');
	}
	rptr = NEXT(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	for (pos = linebuf; *pos; pos++) printchr(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    case 'V': /* prev page (recall) */
      if (recall_buffer[PREV(rptr)]) {
	if (end > linebuf) {
	  while (pos > linebuf) backchr(*--pos);
	  putchar('\033');
	  putchar('K');
	}
	rptr = PREV(rptr);
	strcpy(linebuf, recall_buffer[rptr]);
	for (pos = linebuf; *pos; pos++) printchr(*pos);
	end = pos;
      } else
	putchar(7);
      break;

    default:
      putchar('\033');
      putchar(chr);
      break;

    }
    esc = 0;
  }
  fflush(stdout);
  if (cnt) *buf = linebuf;
  return cnt;
}

