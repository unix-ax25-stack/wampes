/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ttydriv.c,v 1.28 1996/08/11 18:16:09 deyke Exp $ */

/* TTY input line editing
 */
#include <stdio.h>

#include "global.h"
#include "tty.h"

#define LINESIZE        1024
#define LINEMAX         (LINESIZE-10)
#define RECALLSIZE      63

enum e_ttytype {
  TT_UNKNOWN,
  TT_ANSI,
  TT_HP
};

enum e_keyaction {
  KA_NONE,              /* Use key literally */
  KA_FK1,               /* Function key 1 */
  KA_FK2,               /* Function key 2 */
  KA_FK3,               /* Function key 3 */
  KA_FK4,               /* Function key 4 */
  KA_FK5,               /* Function key 5 */
  KA_FK6,               /* Function key 6 */
  KA_FK7,               /* Function key 7 */
  KA_FK8,               /* Function key 8 */
  KA_FK9,               /* Function key 9 */
  KA_FK10,              /* Function key 10 */
  KA_DEL_CURR_CHAR,     /* Delete current character */
  KA_DEL_CURR_WORD,     /* Delete current word */
  KA_DEL_LINE,          /* Delete whole line */
  KA_DEL_PREV_CHAR,     /* Delete previous character */
  KA_DEL_PREV_WORD,     /* Delete previous word */
  KA_DEL_TO_EOL,        /* Delete to end of line */
  KA_ECHO,              /* Echo input to output */
  KA_IGNORE,            /* Ignore this key */
  KA_LEFT_CHAR,         /* Go left one character */
  KA_LEFT_MAX,          /* Go to start of line */
  KA_LEFT_WORD,         /* Go left one word */
  KA_NEXT,              /* Recall next line */
  KA_PREV,              /* Recall previous line */
  KA_QUOTE,             /* Quote next character */
  KA_REDISPLAY,         /* Redisplay line */
  KA_RETURN,            /* Return line to caller */
  KA_RIGHT_CHAR,        /* Go right one character */
  KA_RIGHT_MAX,         /* Go to end of line */
  KA_RIGHT_WORD,        /* Go right one word */
  KA_SEARCH,            /* Search line */
  KA_TRANSMIT           /* Return line to caller (without CR/LF) */
};

struct keytable {
	char str[8];
	enum e_ttytype ttytype;
	enum e_keyaction keyaction;
};

static const struct keytable Keytable[] = {

	{ "\001",       TT_UNKNOWN,     KA_LEFT_MAX },
	{ "\002",       TT_UNKNOWN,     KA_LEFT_CHAR },
	{ "\004",       TT_UNKNOWN,     KA_DEL_CURR_CHAR },
	{ "\005",       TT_UNKNOWN,     KA_RIGHT_MAX },
	{ "\006",       TT_UNKNOWN,     KA_RIGHT_CHAR },
	{ "\010",       TT_UNKNOWN,     KA_DEL_PREV_CHAR },
	{ "\012",       TT_UNKNOWN,     KA_RETURN },
	{ "\013",       TT_UNKNOWN,     KA_DEL_TO_EOL },
	{ "\014",       TT_UNKNOWN,     KA_REDISPLAY },
	{ "\015",       TT_UNKNOWN,     KA_RETURN },
	{ "\016",       TT_UNKNOWN,     KA_NEXT },
	{ "\020",       TT_UNKNOWN,     KA_PREV },
	{ "\022",       TT_UNKNOWN,     KA_SEARCH },
	{ "\024",       TT_UNKNOWN,     KA_TRANSMIT },
	{ "\025",       TT_UNKNOWN,     KA_DEL_LINE },
	{ "\026",       TT_UNKNOWN,     KA_QUOTE },
	{ "\027",       TT_UNKNOWN,     KA_DEL_PREV_WORD },
	{ "\030",       TT_UNKNOWN,     KA_DEL_LINE },
	{ "\033&r1L",   TT_HP,          KA_LEFT_MAX },
	{ "\033&r1R",   TT_HP,          KA_RIGHT_MAX },
	{ "\033A",      TT_HP,          KA_PREV },
	{ "\033B",      TT_HP,          KA_NEXT },
	{ "\033C",      TT_HP,          KA_RIGHT_CHAR },
	{ "\033D",      TT_HP,          KA_LEFT_CHAR },
	{ "\033F",      TT_HP,          KA_RIGHT_MAX },
	{ "\033G",      TT_HP,          KA_DEL_TO_EOL },
	{ "\033H",      TT_HP,          KA_LEFT_MAX },
	{ "\033J",      TT_HP,          KA_DEL_LINE },
	{ "\033K",      TT_HP,          KA_DEL_TO_EOL },
	{ "\033L",      TT_HP,          KA_IGNORE },
	{ "\033M",      TT_HP,          KA_DEL_LINE },
	{ "\033OA",     TT_ANSI,        KA_PREV },
	{ "\033OB",     TT_ANSI,        KA_NEXT },
	{ "\033OC",     TT_ANSI,        KA_RIGHT_CHAR },
	{ "\033OD",     TT_ANSI,        KA_LEFT_CHAR },
	{ "\033OP",     TT_ANSI,        KA_FK1 },
	{ "\033OQ",     TT_ANSI,        KA_FK2 },
	{ "\033OR",     TT_ANSI,        KA_FK3 },
	{ "\033OS",     TT_ANSI,        KA_FK4 },
	{ "\033OT",     TT_ANSI,        KA_FK5 },
	{ "\033OU",     TT_ANSI,        KA_FK6 },
	{ "\033OV",     TT_ANSI,        KA_FK7 },
	{ "\033OW",     TT_ANSI,        KA_FK8 },
	{ "\033OX",     TT_ANSI,        KA_FK9 },
	{ "\033OY",     TT_ANSI,        KA_FK10 },
	{ "\033Om",     TT_ANSI,        KA_FK8 },
	{ "\033Ot",     TT_ANSI,        KA_FK9 },
	{ "\033Ou",     TT_ANSI,        KA_FK10 },
	{ "\033Ow",     TT_ANSI,        KA_FK5 },
	{ "\033Ox",     TT_ANSI,        KA_FK6 },
	{ "\033Oy",     TT_ANSI,        KA_FK7 },
	{ "\033P",      TT_HP,          KA_DEL_CURR_CHAR },
	{ "\033Q",      TT_HP,          KA_IGNORE },
	{ "\033S",      TT_HP,          KA_ECHO },
	{ "\033T",      TT_HP,          KA_ECHO },
	{ "\033U",      TT_HP,          KA_NEXT },
	{ "\033V",      TT_HP,          KA_PREV },
	{ "\033Y",      TT_HP,          KA_RIGHT_MAX },
	{ "\033[11~",   TT_ANSI,        KA_FK1 },
	{ "\033[12~",   TT_ANSI,        KA_FK2 },
	{ "\033[13~",   TT_ANSI,        KA_FK3 },
	{ "\033[14~",   TT_ANSI,        KA_FK4 },
	{ "\033[15~",   TT_ANSI,        KA_FK5 },
	{ "\033[17~",   TT_ANSI,        KA_FK6 },
	{ "\033[18~",   TT_ANSI,        KA_FK7 },
	{ "\033[19~",   TT_ANSI,        KA_FK8 },
	{ "\033[5~",    TT_ANSI,        KA_PREV },
	{ "\033[6~",    TT_ANSI,        KA_NEXT },
	{ "\033[A",     TT_ANSI,        KA_PREV },
	{ "\033[B",     TT_ANSI,        KA_NEXT },
	{ "\033[C",     TT_ANSI,        KA_RIGHT_CHAR },
	{ "\033[D",     TT_ANSI,        KA_LEFT_CHAR },
	{ "\033[P",     TT_ANSI,        KA_FK1 },
	{ "\033[Q",     TT_ANSI,        KA_FK2 },
	{ "\033[R",     TT_ANSI,        KA_FK3 },
	{ "\033[S",     TT_ANSI,        KA_FK4 },
	{ "\033[T",     TT_ANSI,        KA_FK5 },
	{ "\033[U",     TT_ANSI,        KA_FK6 },
	{ "\033[V",     TT_ANSI,        KA_FK7 },
	{ "\033[W",     TT_ANSI,        KA_FK8 },
	{ "\033[X",     TT_ANSI,        KA_FK9 },
	{ "\033[Y",     TT_ANSI,        KA_FK10 },
	{ "\033[m",     TT_ANSI,        KA_FK8 },
	{ "\033[t",     TT_ANSI,        KA_FK9 },
	{ "\033[u",     TT_ANSI,        KA_FK10 },
	{ "\033[w",     TT_ANSI,        KA_FK5 },
	{ "\033[x",     TT_ANSI,        KA_FK6 },
	{ "\033[y",     TT_ANSI,        KA_FK7 },
	{ "\033\010",   TT_UNKNOWN,     KA_DEL_PREV_WORD },
	{ "\033\177",   TT_UNKNOWN,     KA_DEL_PREV_WORD },
	{ "\033b",      TT_UNKNOWN,     KA_LEFT_WORD },
	{ "\033d",      TT_UNKNOWN,     KA_DEL_CURR_WORD },
	{ "\033f",      TT_UNKNOWN,     KA_RIGHT_WORD },
	{ "\033h",      TT_HP,          KA_LEFT_MAX },
	{ "\033p",      TT_HP,          KA_FK1 },
	{ "\033q",      TT_HP,          KA_FK2 },
	{ "\033r",      TT_HP,          KA_FK3 },
	{ "\033s",      TT_HP,          KA_FK4 },
	{ "\033t",      TT_HP,          KA_FK5 },
	{ "\033u",      TT_HP,          KA_FK6 },
	{ "\033v",      TT_HP,          KA_FK7 },
	{ "\033w",      TT_HP,          KA_FK8 },
	{ "\177",       TT_UNKNOWN,     KA_DEL_PREV_CHAR },

	{ "",           TT_UNKNOWN,     KA_NONE }

};

#define NEXT(i)         (((i) + 1) & RECALLSIZE)
#define PREV(i)         (((i) - 1) & RECALLSIZE)

char *Fkey_ptr;
char *Fkey_table[NUM_FKEY];

static enum e_ttytype Ttytype = TT_ANSI;
static int Rawmode;

/*---------------------------------------------------------------------------*/

int raw(void)
{
	Rawmode = 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

int cooked(void)
{
	Rawmode = 0;
	return 0;
}

/*---------------------------------------------------------------------------*/

static void printchr(int chr)
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

static void backchr(int chr)
{
	putchar('\b');
	chr &= 0x7f;
	if (chr < 32 || chr == 127)
		putchar('\b');
}

/*---------------------------------------------------------------------------*/

static void delchr(int chr)
{
#ifdef ibm6153
	putchar(' ');
	putchar('\b');
#else
	putchar('\033');
	if (Ttytype == TT_ANSI) putchar('[');
	putchar('P');
	chr &= 0x7f;
	if (chr < 32 || chr == 127) {
		putchar('\033');
		if (Ttytype == TT_ANSI) putchar('[');
		putchar('P');
	}
#endif
}

/*---------------------------------------------------------------------------*/

static void inschr(int chr)
{
	int c;

	if (Ttytype == TT_ANSI) {
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

static void clreol(void)
{
	putchar('\033');
	if (Ttytype == TT_ANSI) putchar('[');
	putchar('K');
}

/*---------------------------------------------------------------------------*/

/* Accept characters from the incoming tty, buffer and process them
 * (if in cooked mode) or just pass them directly (if in raw mode).
 * Returns the number of characters available for use; if non-zero,
 * also stashes a pointer to the character(s) in the "buf" argument.
 */

int ttydriv(int chr, char **buf)
{

	static char linebuf[LINESIZE];
	static char *end = linebuf;
	static char *pos = linebuf;
	static char *recall_buffer[RECALLSIZE+1];
	static char keybuf[8];
	static int Escape_save;
	static int keycnt;
	static int quote;
	static int rptr, wptr;

	char *p;
	enum e_keyaction keyaction;
	int cnt;
	int i;

	if (Rawmode) {
		*linebuf = chr;
		*buf = linebuf;
		return 1;
	}

	if (!chr) return 0;

	cnt = 0;
	keybuf[keycnt++] = chr;
	keyaction = KA_NONE;
	if (!quote) {
		for (i = 0; Keytable[i].str[0]; i++) {
			if (Keytable[i].str[0] == *keybuf &&
			    !strncmp(Keytable[i].str, keybuf, keycnt)) {
				if (strlen(Keytable[i].str) > keycnt)
					return 0;
				if (Keytable[i].ttytype != TT_UNKNOWN)
					Ttytype = Keytable[i].ttytype;
				keyaction = Keytable[i].keyaction;
				break;
			}
		}
	}

	if (quote) {
		delchr(quote);
		quote = 0;
		Escape = Escape_save;
	}

	switch (keyaction) {

	case KA_NONE:
		for (i = 0; i < keycnt; i++) {
			if (end - linebuf >= LINEMAX)
				putchar(7);
			else if (pos == end) {
				*end++ = keybuf[i];
				printchr(*pos++);
			} else {
				for (p = end - 1; p >= pos; p--) p[1] = *p;
				*pos++ = keybuf[i];
				end++;
				inschr(keybuf[i]);
			}
		}
		break;

	case KA_FK1:
	case KA_FK2:
	case KA_FK3:
	case KA_FK4:
	case KA_FK5:
	case KA_FK6:
	case KA_FK7:
	case KA_FK8:
	case KA_FK9:
	case KA_FK10:
		Fkey_ptr = Fkey_table[keyaction-KA_FK1];
		break;

	case KA_DEL_CURR_CHAR:
		if (pos < end) {
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_DEL_CURR_WORD:
		while (pos < end && *pos == ' ') {
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		while (pos < end && *pos != ' ') {
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_DEL_LINE:
		while (pos > linebuf) backchr(*--pos);
	case KA_DEL_TO_EOL:
		if (pos < end) {
			clreol();
			end = pos;
		}
		break;

	case KA_DEL_PREV_CHAR:
		if (pos > linebuf) {
			backchr(*--pos);
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_DEL_PREV_WORD:
		while (pos > linebuf && pos[-1] == ' ') {
			backchr(*--pos);
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		while (pos > linebuf && pos[-1] != ' ') {
			backchr(*--pos);
			delchr(*pos);
			for (p = pos + 1; p < end; p++) p[-1] = *p;
			end--;
		}
		break;

	case KA_ECHO:
		for (i = 0; i < keycnt; i++) putchar(keybuf[i]);
		break;

	case KA_IGNORE:
		break;

	case KA_LEFT_CHAR:
		if (pos > linebuf) backchr(*--pos);
		break;

	case KA_LEFT_MAX:
		while (pos > linebuf) backchr(*--pos);
		break;

	case KA_LEFT_WORD:
		while (pos > linebuf && pos[-1] == ' ') backchr(*--pos);
		while (pos > linebuf && pos[-1] != ' ') backchr(*--pos);
		break;

	case KA_NEXT:
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

	case KA_PREV:
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

	case KA_QUOTE:
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

	case KA_REDISPLAY:
		putchar('\n');
		for (p = linebuf; p < end; p++) printchr(*p);
		while (p > pos) backchr(*--p);
		break;

	case KA_RETURN:
	case KA_TRANSMIT:
		*end = 0;
		if (*linebuf == '\022') {
			rptr = wptr;
			while (recall_buffer[PREV(rptr)] &&
			       !strstr(recall_buffer[PREV(rptr)], linebuf + 1))
				rptr = PREV(rptr);
			if (recall_buffer[PREV(rptr)]) {
				while (pos > linebuf) backchr(*--pos);
				clreol();
				rptr = PREV(rptr);
				strcpy(linebuf, recall_buffer[rptr]);
				for (pos = linebuf; *pos; pos++) printchr(*pos);
				end = pos;
			} else {
				putchar(7);
				rptr = wptr;
			}
		} else {
			if (*linebuf &&
			    (!recall_buffer[PREV(wptr)] ||
			     strcmp(linebuf, recall_buffer[PREV(wptr)]))) {
				recall_buffer[wptr] = strdup(linebuf);
				wptr = NEXT(wptr);
				if (recall_buffer[wptr]) {
					free(recall_buffer[wptr]);
					recall_buffer[wptr] = 0;
				}
			}
			rptr = wptr;
			if (keyaction != KA_TRANSMIT) {
				*end++ = '\r';
				*end++ = '\n';
				*end   = 0;
				putchar('\n');
			}
			cnt = end - linebuf;
			pos = end = linebuf;
		}
		break;

	case KA_RIGHT_CHAR:
		if (pos < end) printchr(*pos++);
		break;

	case KA_RIGHT_MAX:
		while (pos < end) printchr(*pos++);
		break;

	case KA_RIGHT_WORD:
		while (pos < end && *pos == ' ') printchr(*pos++);
		while (pos < end && *pos != ' ') printchr(*pos++);
		break;

	case KA_SEARCH:
		while (pos > linebuf) backchr(*--pos);
		if (pos < end) {
			clreol();
			end = pos;
		}
		*end++ = '\022';
		printchr(*pos++);
		break;

	}

	keycnt = 0;
	fflush(stdout);
	if (cnt) *buf = linebuf;
	return cnt;
}

