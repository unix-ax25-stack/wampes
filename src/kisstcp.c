/* KISS-over-TCP - kisstcp, and under the name tcpkiss also from BPQ.
 *
 * THE FRAME: FEND, a KISS type byte, the escaped AX.25 frame, FEND.  The
 * first byte after the FEND is NOT an AX.25 byte but channel and command in
 * the upper and lower nibble.  Anyone who skips it shifts every field in the
 * frame by one and then gets a cryptic CRC message instead of the explanation
 * (Thomas).
 *
 * THE REAL QUESTION IS NOT THE ESCAPING BUT THE STREAM.  KISS is
 * self-synchronising, and that is its whole advantage: 0xc0 proves that a
 * frame has ended, no matter what the stream looked like before.  A read() is
 * not a frame boundary - half a read() is not half a frame, it is only half a
 * frame.  So collect everything that is there, and hand WAMPES only ever a
 * complete frame.
 */

#include "configure.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "cmdparse.h"
#include "iface.h"
#include "devparam.h"
#include "slip.h"			/* FR_END, FR_ESC, T_FR_END, T_FR_ESC */
#include "ax25.h"
#include "axip.h"
#include "crc.h"
#include "tcpsock.h"
#include "buildsaddr.h"		/* build_hostport(): the "host:port" rule */

#define KISSTCP_DEFAULT_PORT	8001

/* THE TYPE BYTE, and it is two fields and not one:
 *
 *      7 6 5 4  3 2 1 0
 *     [ channel ][ command ]
 *
 * The upper nibble is the KISS channel, the lower one the command - zero for
 * a data frame, 1..6 for setting a TNC parameter, 15 for leaving KISS.  Dire
 * Wolf writes it exactly so: stemp[0] = (chan << 4) | kiss_cmd (kissnet.c).
 *
 * AND THERE IS NO CRC BIT IN IT.  This file used to read 0x80 as "a 16 bit
 * CRC follows" and 0x20 as "an RMNC CRC follows", which is not KISS: both
 * bits are in the channel field, so 0x80 is channel 8 and 0x20 is channel 2.
 * No conformant peer ever sends them, this code can therefore never have seen
 * a CRC there against one - and against itself it read its own test frames as
 * a wrong channel.  The lower nibble is the command, and there is no room in
 * the type byte to say "a CRC follows": over TCP the frame boundary is given
 * by FEND anyway, and the systems that meet here (Dire Wolf, BPQ, catf) send
 * no CRC.  A CRC between FEND and the AX.25 frame would have to be announced
 * out of band, and nothing announces it (Thomas).
 */
#define KISS_CMD_DATA_FRAME	0

/* THE CHANNEL, AND WHY ZERO IS ENOUGH.  Dire Wolf's rule is "one TCP port per
 * channel".  That is not convenience: the channel number stands in the KISS
 * frame and not in the TCP connection, and a programmer holds it in his head.
 * We take channel zero and leave the rest lying instead of assigning it to a
 * channel it was not in (Thomas).
 */
#define KISS_CHAN		0

/* THE STATE, AND ONE VALUE PER PORT. */

/* What fits into one mbuf over TCP: the AX.25 frame without the KISS frame.
 */
#define MAX_TXFRAME		2048

/*---------------------------------------------------------------------------*/

/* THE INPUT.  What tcpsock.c has collected in tp->buf is split into frames
 * here.  At the end the rest that is not yet a complete frame is moved to the
 * front - it stays there until the rest of the stream arrives.
 */
static int
kisstcp_rx(
struct tcpsock *tp)
{
  int off = 0;
  int segstart;
  int esclen;
  int flen;
  int i;
  unsigned char raw[TCPSOCK_MAXFRAME];
  unsigned char frame[TCPSOCK_MAXFRAME];
  int type;

  while (off < tp->len) {
    /* UP TO THE FIRST FEND.  Everything before it is rubbish or the rest of
     * an earlier frame - both is skipped.  A stream that begins with half a
     * frame after a reset is the normal case after an RST and no reason to
     * abort.
     */
    for (segstart = off; segstart < tp->len; segstart++)
      if (tp->buf[segstart] == FR_END)
	break;
    if (segstart >= tp->len)
      break;			/* no FEND yet: wait */

    /* TWO FENDS IN A ROW ARE AN EMPTY FRAME, and that is allowed in KISS.  It
     * is discarded rather than reported as an error, because it is not one:
     * it is what a partner sends when the channel is quiet, and a counter that
     * grows by twelve an hour on a perfectly healthy line has stopped being
     * read (Thomas).
     *
     * BUT IT IS COUNTED, separately and in its own column.  The reason is the
     * NAT box: a mapping that has been dropped is not reported to either end,
     * both believe the connection is up, and nothing is sent for ever.  A
     * partner that can send the empty frame is a partner whose connection can
     * be kept alive - and that is the one thing about a quiet port that cannot
     * be told apart from a port whose peer has vanished.  One count per run of
     * FENDs, however long it is: c0 c0 c0 is one partner being quiet, not three
     * of anything.
     */
    for (off = segstart + 1; off < tp->len && tp->buf[off] == FR_END; off++) ;
    if (off > segstart + 1)
      tp->idlesin++;
    if (off >= tp->len)
      break;

    /* THE RAW SECTION, collected unescaped.  We collect the whole thing first
     * and unpack afterwards.  An escape runs across the boundary of two
     * read()s, and anyone who meets half an escape while unpacking needs a
     * state across the call - that is the reason for the second buffer and
     * against a state machine with five states.
     */
    for (esclen = 0; off < tp->len; off++) {
      if (tp->buf[off] == FR_END)
	break;
      if (esclen >= (int) sizeof(raw)) {
	tp->overruns++;
	tp->ifp->crcerrors++;	/* the interface counts too - see axtcp.c */
	tcpsock_forget(tp);
	return -1;
      }
      raw[esclen++] = tp->buf[off];
    }
    if (off >= tp->len)
      break;			/* no closing FEND: wait */

    /* UNESCAPING.  FESC T_FR_END is a FEND, FESC T_FR_ESC a FESC, everything
     * else stands as it is.  A real FESC without a successor is a broken
     * peer - but the bytes after it are still the bytes they have to be, and
     * are not thrown away with it.
     */
    for (i = 0, flen = 0; i < esclen; i++) {
      if (flen >= (int) sizeof(frame)) {
	tp->overruns++;
	tp->ifp->crcerrors++;	/* the interface counts too - see axtcp.c */
	tcpsock_forget(tp);
	return -1;
      }
      if (raw[i] == FR_ESC && i + 1 < esclen) {
	i++;
	frame[flen++] = (raw[i] == T_FR_END) ? FR_END :
			(raw[i] == T_FR_ESC) ? FR_ESC : raw[i];
      } else {
	frame[flen++] = raw[i];
      }
    }
    if (flen < 1)
      continue;			/* only rubbish between two FEND */
    /* UNREACHABLE, and left that way on purpose rather than decorated with a
     * counter: the two loops above already skipped every FEND that follows
     * another one, so the first byte collected here is never a FEND and esclen
     * is at least 1; unescaping appends one byte per collected byte, so flen
     * is at least 1 too.  An "empty frame" in KISS is the c0 c0 above, and
     * that one is deliberately not an error - it is what a partner sends to
     * say the channel is quiet, and a peer that does it every minute must not
     * fill the counter (Thomas).
     */

    /* THE TYPE BYTE, and it is mandatory, not a matter of form: only from
     * here on is the rest the AX.25 frame.
     */
    type = frame[0];
    flen--;
    memmove(frame, frame + 1, (size_t) flen);

    /* THE UPPER NIBBLE IS THE CHANNEL.  We take zero and leave everything else
     * lying.  Channel 3 is a partner that wants to tell us something on
     * channel 3, and that is not our channel; reading it as channel zero would
     * be the error that shows up as "unknown callsigns" and not as "wrong
     * channel".  And it is counted, because a partner on one TCP port that
     * speaks for two channels was told to use two ports (Thomas).
     */
    if (((type >> 4) & 0x0f) != KISS_CHAN) {
      tp->crcerr++;
      tp->ifp->crcerrors++;
      continue;
    }

    /* THE LOWER NIBBLE IS THE COMMAND, and only 0 is a data frame.  What
     * stands in the rest is a TNC parameter being set for a TNC we are not -
     * a partner that sent us TXDELAY or, worse, command 15 to leave KISS.  It
     * is not AX.25 data and must not go to ax25() as if it were: a one byte
     * parameter frame that reaches ax_recv() comes back as a header complaint,
     * which names the symptom and not the cause.  So it is dropped here, and
     * counted, because over TCP nobody can set a parameter on a TNC that is
     * not there.
     */
    if ((type & 0x0f) != KISS_CMD_DATA_FRAME) {
      tp->crcerr++;
      tp->ifp->crcerrors++;
      continue;
    }
    /* AND NO CRC.  Nothing in the type byte announces one - see the comment
     * at KISS_CMD_DATA_FRAME - so what follows FEND is the AX.25 frame and
     * nothing else.  The frame carries its own FCS, which check_crc_16() in
     * ax25 does, and it must NOT be cut off here: ax25 appends one again when
     * it sends.
     */
    if (flen > 0)
      tcpsock_input(tp, frame, flen, KISS_CHAN);
  }

  /* THE REST STAYS.  Everything that was not a complete frame is moved to the
   * front and waits for the rest of the stream - that is the whole reason for
   * this buffer.
   */
  if (off > 0) {
    memmove(tp->buf, tp->buf + off, (size_t) (tp->len - off));
    tp->len -= off;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE OUTPUT.  FEND, type byte 0x00, the escaped frame, FEND.  We do not do
 * the CRC: over TCP the frame boundary is given by FEND anyway, and the
 * systems that meet here do not send one.  Whoever needs one should add it on
 * the KISS side - that is where the decision belongs and not into the middle.
 */
static int
kisstcp_tx(
struct tcpsock *tp,
unsigned char *out,
int max,
struct mbuf **bpp)
{
  int l;
  int i;
  int n = 0;
  unsigned char buf[MAX_TXFRAME];

  if (max < 3)
    return -1;
  if ((l = pullup(bpp, buf, sizeof(buf))) <= 0 || *bpp)
    return -1;

  out[n++] = FR_END;
  out[n++] = PARAM_DATA;	/* channel 0, data */
  for (i = 0; i < l; i++) {
    if (n + 3 >= max)
      return -1;
    if (buf[i] == FR_END) {
      out[n++] = FR_ESC;
      out[n++] = T_FR_END;
    } else if (buf[i] == FR_ESC) {
      out[n++] = FR_ESC;
      out[n++] = T_FR_ESC;
    } else {
      out[n++] = buf[i];
    }
  }
  out[n++] = FR_END;
  return n;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE OF THIS PROTOCOL: FEND FEND, two bytes and nothing else.
 *
 * THAT IS ONE FRAME OF NO CONTENT between two FENDs, and it is the KISS way of
 * saying "the channel is quiet" - which is exactly what we mean, and it is why
 * no byte has to be invented for the purpose.  TODO.txt asks here for "ein
 * Keepalive/Sync-Byte 0xC0", and one lone FEND would not do: it leaves the
 * receiver's decoder standing in the middle of a frame with no end in sight,
 * and a decoder waiting for an end is a decoder that cannot synchronise itself
 * out of it.  A FEND pair closes what it opens, which is the whole property
 * that makes KISS findable again in the stream.
 */
static int
kisstcp_keepalive(
struct tcpsock *tp,
unsigned char *out,
int max)
{
  (void) tp;
  if (max < 2)
    return -1;
  out[0] = FR_END;
  out[1] = FR_END;
  return 2;
}

/*---------------------------------------------------------------------------*/

/* THE FIRST BYTE DECIDES.  0xc0 is FEND and therefore KISS; anything else can
 * only be the upper half of an AXTCP length field, because 0xc0 as a length
 * would be 49152 bytes (XRouter against BPQ).
 *
 * ONLY ONCE, and that is the reason for the state TCPAD_DETECT: a protocol
 * change in mid-operation would be something one negotiates with a protocol,
 * and not something one guesses at from one byte.
 */
static int
kisstcp_detect(
struct tcpsock *tp)
{
  if (tp->len < 1)
    return 0;			/* nothing there yet: keep waiting */
  if (tp->buf[0] != FR_END)
    return -1;			/* not KISS */
  tp->proto = TCPAD_KISSTCP;
  return 1;
}

/*---------------------------------------------------------------------------*/

/* WHAT COUNTS ON THIS PORT, and nothing more.  The sysop said "kisstcp", so
 * this is KISS - an AXTCP neighbour that announces itself on port 8001 is not
 * accepted, and is not told why either.
 */
/* attach kisstcp [<label> [listen [<port>] | client <host>[:<port>]]]
 *
 * THE VALUES.  argv[0] is the word after "attach", argv[1] the label -
 * exactly as in axip_attach(), and that is not a subtlety here: an attach
 * that reads its arguments one place too early takes the word "listen" for
 * the label and then fails on the port specification it never read (Thomas).
 *
 * bind= may stand anywhere and is filtered out, as in axip: a word that may
 * only be written in one particular place is an extra rule that you think
 * about while copying it out.
 */
int
kisstcp_attach(
int argc,
char *argv[],
void *p)
{
  struct iface *ifp;
  struct tcpsock *tp;
  char ifname[64];
  const char *bindword = NULL;
  char *av[12];
  char *host = NULL;
  char hostbuf[1024];
  char servbuf[32];
  struct tcpsock_bindlist *binds = NULL;
  int family = AF_UNSPEC;
  long tmp;
  /* TWO PORT NUMBERS, and not one - see axtcp_attach().
   */
  int lport = KISSTCP_DEFAULT_PORT;		/* the port this port ANSWERS on */
  int dport = KISSTCP_DEFAULT_PORT;		/* the port this port CALLS on */
  int client = 0;
  int listen = 0;
  int ac = 0;
  int i;
  int keepalive = -1;		/* -1 = not written, so the default applies */
  char key[AXALEN];		/* the shared code, if one was written */

  (void) p;
  /* "bind=" AND "keepalive" ARE THE TWO WORDS THAT MAY STAND ANYWHERE, and both
   * are taken out here, before anything reads a position.  A word that may only
   * be written in one particular place is an extra rule to think about while
   * copying a line out.
   *
   * "bind=" IS A LIST, and the list is parsed ONCE and then belongs to the port,
   * so that "listen" can make a socket per entry and "client" can leave by the
   * entry of the peer's family out of the same words.
   */
  for (i = 0; i < argc; i++) {
    if (!strncmp(argv[i], "bind=", 5)) {
      if (bindword != NULL) {
	printf("\"bind=\" is written twice - it takes a list, so one word: "
	       "\"bind=%s,%s\"\n", bindword, argv[i] + 5);
	return 1;
      }
      bindword = argv[i] + 5;
      continue;
    }
    if (ac >= (int) (sizeof(av) / sizeof(av[0]))) {
      printf("Usage: attach kisstcp [<label> [listen [<port>]]]"
	     " [client <host>[:<port>]] [keepalive <seconds>]"
	     " [shared-key <code>]\n");
      printf("... and too many words before it.\n");
      return 1;
    }
    av[ac++] = argv[i];
  }
  argc = ac;
  argv = av;
  /* THE KEEPALIVE COMES OUT HERE, next to "bind=" and before anything reads a
   * position: from here on the line is positional, and a word in a position it
   * knows nothing about is an error the sysop would not understand.
   */
  if (tcpsock_take_keepalive(&argc, argv, &keepalive))
    return 1;
  /* AND THE SHARED CODE NEXT TO IT, for the same reason: a word that may only
   * stand in one particular place is an extra rule to think about while
   * copying a line out.
   */
  if (tcpsock_take_sharedkey(&argc, argv, key))
    return 1;

  /* "stat" IS NOT A LABEL.  The word in second place decides whether a port is
   * built at all - otherwise "attach kisstcp stat" would create a port named
   * "stat" and say nothing (Thomas).
   */
  if (argc >= 2 && !strcmp(argv[1], "stat")) {
    struct iface *statif = NULL;

    if (argc >= 3) {
      if ((statif = if_lookup(argv[2])) == NULL) {
	printf("Interface \"%s\" unknown\n", argv[2]);
	return 1;
      }
      if (statif->raw != tcpsock_raw) {
	printf("Interface \"%s\" is not a TCP transport\n", argv[2]);
	return 1;
      }
    }
    return tcpsock_stat(statif, TCPAD_KISSTCP, argc, argv, p);
  }

  /* "listen" AND "client" ARE NOT LABELS - see the same question in
   * axtcp_attach(): the name stands first, and a first word that is one of
   * them is taken as the name (Thomas).
   */
  if (argc >= 2 &&
      (!strcmp(argv[1], "listen") || !strcmp(argv[1], "client"))) {
    printf("\"%s\" is taken as the interface NAME here - the name stands\n"
	   "first and the option after it: \"attach kisstcp <name> %s ...\"\n",
	   argv[1], argv[1]);
    return 1;
  }
  if (argc >= 2) {
    strncpy(ifname, argv[1], sizeof(ifname) - 1);
    ifname[sizeof(ifname) - 1] = 0;
  } else {
    strncpy(ifname, "kisstcp", sizeof(ifname) - 1);
    ifname[sizeof(ifname) - 1] = 0;
  }
  if (if_lookup(ifname) != NULL) {
    printf("Interface %s already exists\n", ifname);
    return 1;
  }

  /* THE LIST IS TAKEN APART HERE, before the interface exists: a list that
   * cannot be read is a mistake in the command line, and building the interface
   * first and then refusing it would leave a name that is already half made.
   */
  if ((binds = tcpsock_bind_list(bindword)) == NULL && bindword != NULL)
    return 1;

  /* "listen" AND "client" ARE TWO WORDS, NOT TWO MODES, and both may stand: a
   * node that answers a KISS-TCP partner AND holds a session to an XRouter is
   * both of those at once, and it is one interface - one name for "ifconfig",
   * one "keepalive", one thing to trace.  Making them modes meant writing the
   * second word decided everything and the first was ignored, so such a node
   * had to become two interfaces over one port number, and two interfaces
   * cannot hold one port number between them.
   *
   * Either word may come first, and the line is walked word by word until
   * neither is there any more - the same shape as the "bind=" filtering above.
   *
   * NEITHER WORD AT ALL IS A LISTENER, which is the more frequent form and the
   * one where you need not decide on a word.
   */
  listen = 0;
  for (i = 2; i < argc; ) {
    if (!strcmp(argv[i], "listen")) {
      if (listen++)
	goto Usage;
      i++;
      if (i < argc && argv[i][0] != '\0') {
	if (cmd_getnum(argv[i], &tmp) || tmp <= 0 || tmp > 65535) {
	  printf("Port \"%s\" is not a port number\n", argv[i]);
	  return 1;
	}
	lport = (int) tmp;
	i++;
      }
    } else if (!strcmp(argv[i], "client")) {
      if (client++)
	goto Usage;
      if (++i >= argc) {
	printf("\"client\" wants a host, as in \"client 127.0.0.1:8000\"\n");
	printf("Usage: attach kisstcp [<label> [listen [<port>]]] "
	       "[client <host>[:<port>]] [keepalive <seconds>]\n");
	return 1;
      }
      /* THE PORT STANDS IN THE HOST, and that is right here: "client" has
       * exactly one target, and a second word for it would be a syllable
       * without content.
       *
       * build_hostport() is what splits it, so an IPv6 literal can be written
       * the way build_sockaddr() wants it - "[::1]:8000" - and the brackets are
       * gone by the time the name reaches getaddrinfo(), which would not take
       * them.  A bare "::1:8000" it refuses instead of guessing: there is no
       * telling the port from the address, and the wrong guess is the one that
       * costs the afternoon.
       */
      if (build_hostport(argv[i], hostbuf, sizeof(hostbuf), servbuf,
		 sizeof(servbuf), &family)) {
	printf("\"%s\" is not a host or a host:port - an IPv6 literal goes in "
	       "brackets, as in [::1]:8000\n", argv[i]);
	return 1;
      }
      if (*servbuf) {
	if (cmd_getnum(servbuf, &tmp) || tmp <= 0 || tmp > 65535) {
	  printf("Port \"%s\" is not a port number\n", servbuf);
	  return 1;
	}
	dport = (int) tmp;
      }
      if ((host = strdup(hostbuf)) == NULL)
	return 1;
      client = 1;
      i++;
    } else {
    Usage:
      /* A WORD TOO MANY IS REFUSED, not left unread.  The line is walked by
       * position, and a word nobody read is a word the sysop believes was
       * honoured: "attach kisstcp kl listen 8000 8001" bound 8000 and said
       * nothing at all about 8001, which is not a shorter answer than this
       * one.
       */
      printf("Usage: attach kisstcp [<label> [listen [<port>]]]"
	     " [client <host>[:<port>]] [keepalive <seconds>]"
	     " [shared-key <code>]\n");
      if (i < argc)
	printf("... and \"%s\" is one word too many.\n", argv[i]);
      return 1;
    }
  }
  /* NEITHER WORD IS A LISTENER, on the default port.
   */
  if (!client)
    listen = 1;

  if ((ifp = (struct iface *) calloc(1, sizeof(struct iface))) == NULL) {
    if (host)
      free(host);
    return 1;
  }
  if ((ifp->name = strdup(ifname)) == NULL) {
    free(ifp);
    if (host)
      free(host);
    return 1;
  }
  ifp->addr = Ip_addr;
  ifp->mtu = 1500;		/* an AX.25 frame fits comfortably */
  ifp->trace = 0;
  ifp->broadcast = 0xffffffffUL;
  ifp->netmask = 0xffffffffUL;
  /* OUR OWN HARDWARE ADDRESS.  ax_answers_to() compares every incoming
   * destination field with iface->hwaddr, and without that field the first
   * frame that is delivered to us crashes.  The port is ours, so our own
   * callsign goes here (Thomas).
   */
  ifp->hwaddr = (uint8 *) mallocw(AXALEN);
  addrcp(ifp->hwaddr, Mycall);
  /* THE IFTYPE, AND WITHOUT IT THE NODE CRASHES.  After every incoming
   * packet network() fetches ifp->iftype->rcvf; a NULL there is a crash on
   * the first good frame.  "AX25UI" is the same route as on a serial line:
   * after the KISS cap the bare AX.25 frame arrives, and ax_recv() takes it
   * in - kiss_recv() would look for a second cap here.
   */
  setencap(ifp, "AX25UI");
  /* THE WAY OUT, and it is a hook as in axip - the framing stays with the tx
   * hook, because AXTCP must count the CRC into its length field and KISS does
   * not send one along.
   */
  ifp->raw = tcpsock_raw;
  /* NO AX.25 FCS, AND crccontrol IS DELIBERATELY LEFT ALONE.  The comment
   * that used to stand here claimed that ax25 appends one when it sends and
   * that these two bits therefore have to come off again.  Neither half is
   * true, and the line was a no-op besides:
   *
   *   - Nothing in the HDLC/LAPB stack computes a frame check sequence.
   *     ax25_apply_iface_limits() (lapb.c:1916) only RESERVES two bytes for
   *     it in the length budget.  append_crc_ccitt() is called from exactly
   *     two places in the whole tree, axip.c:604 and axtcp.c:163, and those
   *     are encapsulations with no TNC in the path - on the air the TNC adds
   *     the FCS itself, which is why there is nothing here to do.
   *
   *   - Nothing takes such a bit off either.  check_crc_ccitt() is likewise
   *     called from those two encapsulations only (axip.c:774, axtcp.c:102).
   *     A KISS-TCP peer that did send an FCS would have those two bytes stay
   *     in the UI payload, because ntohax25() (ax25hdr.c:102) consumes the
   *     addresses and nothing else.  That is the same position the serial
   *     KISS port is in, and it is the position the KISS dialects are
   *     specified for.
   *
   *   - And crccontrol is read in only two places: kiss_raw() (kiss.c:85),
   *     which this port does not use - its tx hook is tcpsock_raw() - and the
   *     "ifconfig <if> verbose" switch (iface.c:1536).  So the line could
   *     never have done anything except print "crc-ccitt enabled" for a
   *     procedure that was not running.
   *
   * Leaving it at CRC_OFF, which is what the iface initialiser puts there,
   * makes that line say "crc off" - which is the truth about this port.
   */
  /* THE KEEPALIVE, and the default is on: see TCP_KEEPALIVE_DEFAULT for why a
   * line that is quiet needs a packet on it.  The written value wins, and 0 is
   * a written value - "keepalive 0" means off and not "use the default".
   */
  ifp->keepalive = keepalive < 0 ? TCP_KEEPALIVE_DEFAULT : keepalive;
  if (key[0] != '\0')
    memcpy(ifp->sharedkey, key, sizeof(ifp->sharedkey));

  /* ONE PORT WITH BOTH ROLES, and the two hooks are one set of four - the same
   * bytes either way, because a KISS-TCP port does not know until the first
   * byte which of the two dialects it is speaking.  Only the flags say which
   * half of it is wanted, and the session list is what "attach kisstcp stat"
   * walks.
   *
   * "bind=" BELONGS TO THE PORT AND NOT TO A ROLE, and that is what makes both
   * words work with one list: a listener makes a socket per entry, and the
   * session the client holds leaves by the entry of the peer's family.  A
   * listener and a client on one interface that want different local addresses
   * would need two interfaces - and two interfaces cannot hold one port number
   * between them, which is why they are one interface (Thomas).
   */
  if ((tp = tcpsock_new(ifp, (client ? TCF_CLIENT : 0) | (listen ? TCF_LISTEN : 0),
			TCPAD_KISSTCP)) == NULL) {
    if (host)
      free(host);
    tcpsock_bind_free(binds);
    if_detach(ifp);
    return 1;
  }
  tp->rx = kisstcp_rx;
  tp->tx = kisstcp_tx;
  tp->detect = kisstcp_detect;
  tp->keepalive = kisstcp_keepalive;
  /* THE LIST IS THE PORT'S, and not the call's: the rebuild needs it, and a
   * session that rings again has to leave by the address it was told to leave
   * by.  tcpsock_forget() takes it away with the port.
   */
  tp->binds = binds;

  if (listen) {
    if (tcpsock_listen(tp, binds, lport)) {
      /* tcpsock_listen() has said which address and why.  A second sentence here
       * would only repeat the port number at somebody who has just been told
       * the address.
       */
      tcpsock_forget(tp);
      if (host)
	free(host);
      if_detach(ifp);
      return 1;
    }
  }
  if (client) {
    /* WHAT THE BRACKETS SAID, remembered before the first attempt and not
     * only for it: AF_INET6 tells tcpsock_connect() to stop after the v6
     * answers, and a later retry would otherwise be free to walk into an A
     * record for a name that was written with "[...]" on purpose.  Zero means
     * "either family", which is what an unbracketed spelling asks for.
     *
     * AND WITH A LISTENER ALONGSIDE, THE BRACKETS ARE THE PORT'S TOO: they say
     * which family the peer is, and the "bind=" entry of that family is the
     * address the session leaves by.  So a bracketed peer plus "bind=[::]" is
     * a port that listens on [::] and calls out of [::], which is what the two
     * words together say.
     */
    tp->family = family;
    /* A RING THAT FAILS IS NO REASON TO LOSE THE PORT.  tcpsock_connect() says
     * why, once, and the attach carries on; the ticker tries again after the
     * deadline - otherwise after every outage of the peer one would have to
     * type the command once more (Thomas).
     */
    tcpsock_connect(tp, host, dport);
    free(host);
  }
  /* THE TICK, and it has to be started HERE, at the attach.  There is no
   * other place for it: the init call that was meant to do it is gone,
   * because nothing called it.  The consequence of a missing start would
   * be invisible at the attach - the port listens and answers - and only
   * show later: nothing in tcpsock_timer_task() runs, so a client never
   * reconnects, a connection that is not identified never hits its five
   * minutes, and the keepalive never ticks.
   *
   * CALLED FROM BOTH MODULES it is harmless: the first call starts the tick
   * and every further one sees it running (see tcpsock_init()).
   */
  tcpsock_init();
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}