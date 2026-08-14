/* Getting a serial TNC into KISS mode, without transmitting anything.
 *
 * A TNC on a serial line can be in one of three states when we find it, and
 * the wrong move in any of them puts bytes on the air:
 *
 *   KISS        what it usually is, because KISS survives a power cycle -
 *               the Kantronics manual says so explicitly.  Text typed at it
 *               here becomes the contents of a frame.
 *   command     the harmless one.
 *   converse    or transparent, where every byte typed is transmitted.  This
 *               is how "KISS ON" and a bare newline end up on HF, which is
 *               what started this.
 *
 * The way out of all three, in this order:
 *
 *      C0 FF C0        leaves KISS - documented by Kantronics and by Kenwood
 *                      alike, so it may be relied on.  In command mode it is
 *                      junk and draws an error; in converse mode it lands in
 *                      the send buffer, where the Ctrl-C that follows clears
 *                      it again before any CR could send it.
 *      guard, ^C^C^C, guard
 *                      leaves converse and, with the guard times, transparent
 *                      as well - CMDTIME, one second by default.  In command
 *                      mode it does nothing.
 *      the commands    which differ per make, so they are configuration and
 *                      not code.
 *
 * The sequence is written in a small language of the same shape as a modem
 * chat script, because the same four things are needed: literal text, raw
 * bytes, waiting, and reading.  The two known makes are nothing but canned
 * strings in that language, so their differences never reach the code.
 */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "iface.h"
#include "asy.h"
#include "n8250.h"
#include "slip.h"

/* How long a ?"..." waits before giving up on it and going on. */
#define TNCINIT_EXPECT_MS       2000
#define TNCINIT_POLL_MS         50

static const struct {
  const char *name;
  const char *seq;
} Known[] = {
  /* TAPR TNC2 and everything that copied it, the Kenwood built-in TNCs
   * among them - hence the alias.  Kenwood knows KISS OFF too, which would be
   * a tidier way out than C0 FF C0; it is not used, because the sequence has
   * to work on a TNC already in KISS mode, where no text is read.
   */
  { "tapr",       "\\xC0\\xFF\\xC0 \\d ^C^C^C \\d - "
		  "\"KISS ON\\r\" \\p \"RESTART\\r\" \\p -" },
  { "kenwood",    "\\xC0\\xFF\\xC0 \\d ^C^C^C \\d - "
		  "\"KISS ON\\r\" \\p \"RESTART\\r\" \\p -" },
  /* Kantronics says it differently and, unlike the others, remembers KISS
   * across a power cycle.
   */
  { "kantronics", "\\xC0\\xFF\\xC0 \\d ^C^C^C \\d - "
		  "\"INTFACE KISS\\r\" \\p \"RESET\\r\" \\p -" },
  { NULL, NULL }
};

/*---------------------------------------------------------------------------*/

/* One token of the sequence.  Words are separated by whitespace; a quoted
 * word may contain them.  Returns a pointer past the token, or NULL at the
 * end.
 */

static const char *next_token(const char *p, char *tok, size_t toklen)
{
  size_t n = 0;
  int quote = 0;

  while (*p == ' ' || *p == '\t') p++;
  if (*p == '\0') return NULL;
  for (; *p != '\0'; p++) {
    if (*p == '"') {
      quote = !quote;
      if (n + 1 < toklen) tok[n++] = *p;
      continue;
    }
    if (!quote && (*p == ' ' || *p == '\t')) break;
    if (n + 1 < toklen) tok[n++] = *p;
  }
  tok[n] = '\0';
  return p;
}

/* The escapes, shared by quoted text and by bare words like \xC0\xFF\xC0. */

static int unescape(const char *in, uint8 *out, int outlen)
{
  int n = 0;

  while (*in != '\0' && n < outlen) {
    if (*in == '^' && in[1] != '\0') {           /* ^C, easier to read */
      out[n++] = (uint8) (in[1] & 0x1f);
      in += 2;
      continue;
    }
    if (*in != '\\') {
      out[n++] = (uint8) *in++;
      continue;
    }
    in++;
    switch (*in) {
    case 'r':  out[n++] = '\r'; in++; break;
    case 'n':  out[n++] = '\n'; in++; break;
    case 't':  out[n++] = '\t'; in++; break;
    case '\\': out[n++] = '\\'; in++; break;
    case '"':  out[n++] = '"';  in++; break;
    case 'x': case 'X':
      {
	int v = 0, d = 0;

	in++;
	while (d < 2 && isxdigit(*in & 0xff)) {
	  int c = *in++;

	  v = v * 16 + (c <= '9' ? c - '0' : (c | 0x20) - 'a' + 10);
	  d++;
	}
	if (d) out[n++] = (uint8) v;
      }
      break;
    default:
      if (*in != '\0') out[n++] = (uint8) *in++;
      break;
    }
  }
  return n;
}

/*---------------------------------------------------------------------------*/

static void tnc_write(struct slip *sp, const uint8 *buf, int cnt)
{
  struct mbuf *bp;

  if (cnt <= 0) return;
  if ((bp = qdata((char *) buf, (uint) cnt)) == NULL) return;
  /* Straight at the device: none of this is a frame, so none of it may be
   * wrapped in one.
   */
  (*sp->send)(sp->iface->dev, &bp);
}

/* Has what we are waiting for arrived?  The buffer holds whatever came in
 * while the sequence was running - see slip_recv().
 */

static int tnc_expect(struct slip *sp, const char *want, int32 ms)
{
  int32 waited = 0;

  sp->initrx[sp->initrxcnt] = '\0';
  while (strstr((char *) sp->initrx, want) == NULL) {
    if (waited >= ms) return 0;
    if (ppause(TNCINIT_POLL_MS) == -1) return 0;
    waited += TNCINIT_POLL_MS;
    sp->initrx[sp->initrxcnt] = '\0';
  }
  return 1;
}

/*---------------------------------------------------------------------------*/

/* The sequence itself, as its own process: it waits seconds at a time and the
 * scheduler here is cooperative, so it must not run inside anything else.
 */

static void tncinit_proc(int xdev, void *arg1, void *unused)
{
  char tok[256];
  const char *p;
  struct slip *sp = &Slip[xdev];
  uint8 buf[256];

  (void) arg1;
  (void) unused;

  sp->initialising = 1;
  sp->initrxcnt = 0;

  for (p = sp->initspec; (p = next_token(p, tok, sizeof(tok))) != NULL; ) {
    if (!strcmp(tok, "-")) {                     /* discard what arrived */
      sp->initrxcnt = 0;
      continue;
    }
    if (!strcmp(tok, "\\d")) { ppause(1000); continue; }
    if (!strcmp(tok, "\\p")) { ppause(100);  continue; }
    if (!strncmp(tok, "\\w", 2)) {
      ppause((int32) atol(tok + 2));
      continue;
    }
    if (tok[0] == '?') {                         /* wait for a string */
      char want[128];
      const char *q = tok + 1;
      int n;

      if (*q == '"') q++;
      strncpy(want, q, sizeof(want) - 1);
      want[sizeof(want) - 1] = '\0';
      if ((n = (int) strlen(want)) > 0 && want[n - 1] == '"') want[n - 1] = '\0';
      if (!tnc_expect(sp, want, TNCINIT_EXPECT_MS))
	/* Not a failure.  A port that does not come up because a TNC did
	 * not say what was expected is worse than a port whose TNC may not
	 * be in KISS mode - and the healing pass will come round again.
	 */
	if (sp->iface->trace)
	  printf("%s: tncinit: no \"%s\", carrying on\n",
		 sp->iface->name, want);
      continue;
    }
    {                                            /* text and raw bytes */
      char *unq = tok;
      int n;

      if (*unq == '"') {
	unq++;
	if ((n = (int) strlen(unq)) > 0 && unq[n - 1] == '"') unq[n - 1] = '\0';
      }
      n = unescape(unq, buf, (int) sizeof(buf));
      tnc_write(sp, buf, n);
    }
  }

  sp->initialising = 0;
  sp->initrxcnt = 0;

  /* "Just initialised" is its own state, and this is what it is for: the
   * device queue may hold frames from before the TNC went away.  Sending
   * hours-old I-frames now would earn a FRMR at the far end of a link that
   * has long since forgotten us.
   */
  {
    struct asy *ap = &Asy[sp->iface->dev];

    if (ap->sndq != NULL) {
      free_q(&ap->sndq);
      if (sp->iface->trace)
	printf("%s: tncinit: dropped what was still queued for the TNC\n",
	       sp->iface->name);
    }
  }
  sp->initdone++;
}

/*---------------------------------------------------------------------------*/

/* Run the configured sequence, if there is one and none is running. */

void tncinit_run(struct iface *ifp)
{
  struct slip *sp;

  if (ifp == NULL || ifp->xdev < 0 || ifp->xdev >= SLIP_MAX) return;
  sp = &Slip[ifp->xdev];
  if (sp->initspec == NULL || sp->initialising) return;
  newproc("tncinit", 2048, tncinit_proc, ifp->xdev, NULL, NULL, 0);
}

/* ifconfig <iface> tncinit tapr | kenwood | kantronics | "<sequence>"
 *
 * Setting it also runs it: the sysop said what the TNC is, and the reason to
 * say so is to have it done.
 */

int iftncinit(int argc, char *argv[], void *p)
{
  int i;
  struct iface *ifp = (struct iface *) p;
  struct slip *sp;

  if (ifp->xdev < 0 || ifp->xdev >= SLIP_MAX) {
    printf("%s is not a serial interface\n", ifp->name);
    return 1;
  }
  sp = &Slip[ifp->xdev];

  if (argc < 2) {
    printf("%s: tncinit %s\n", ifp->name,
	   sp->initspec ? sp->initspec : "(none)");
    return 0;
  }
  if (!strcmp(argv[1], "none") || !strcmp(argv[1], "off")) {
    free(sp->initspec);
    sp->initspec = NULL;
    return 0;
  }
  for (i = 0; Known[i].name != NULL; i++)
    if (!stricmp(argv[1], (char *) Known[i].name)) {
      free(sp->initspec);
      sp->initspec = strdup(Known[i].seq);
      tncinit_run(ifp);
      return 0;
    }
  free(sp->initspec);
  sp->initspec = strdup(argv[1]);
  tncinit_run(ifp);
  return 0;
}
