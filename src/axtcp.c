/* AXTCP - AX.25 in TCP with a length field, the way XRouter does it.  Port
 * 9393.
 *
 * THE FRAME: two bytes of length in big-endian order, then the complete
 * AX.25 frame WITH CRC.  The length counts the AX.25 frame and not itself,
 * and the CRC sits at the end of the length area, not above it - it is the
 * transcript, not the check on the transmission.  Anyone who counts it into
 * the length shifts every field by two bytes and gets a CRC message without
 * any content (Thomas).
 *
 * KISS IS THE MORE DANGEROUS CASE, and precisely because of the MISSING FEND.
 * KISS can re-enter the stream exactly right after a byte was lost, because
 * 0xc0 proves the start.  AXTCP cannot: one byte too many, and every
 * following length field is rubbish.  That is why the CRC here is not
 * decoration but the only way to find the stream again - it is correct exactly
 * when the frame was complete and in the right place, and otherwise it is a
 * matter of luck that one should not ask about.
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
#include "slip.h"			/* only FR_END: 0xc0 */
#include "ax25.h"
#include "axip.h"
#include "crc.h"
#include "tcpsock.h"
#include "buildsaddr.h"		/* build_hostport(): the "host:port" rule */

#define AXTCP_DEFAULT_PORT	9393

/* WHAT A LENGTH FIELD MAY CLAIM AT ALL.  It is 16 bits, and the peer will use
 * them - but an AX.25 frame is at most a few hundred bytes, and anything above
 * that is either an error or someone who wants to fill the buffer.  Without
 * this ceiling the buffer reserves 64 KB on the strength of a claim (Thomas).
 */
#define AXTCP_MAXLEN		2048

/* THE CHANNEL IS ALWAYS ZERO HERE.  AXTCP knows no channels - that is the
 * difference to KISS and the reason why the same axip table can serve both
 * and two tables are not needed.
 */
#define AXTCP_CHAN		0

/* THE STATE, AND ONE VALUE PER PORT.  It is one of three and is compared in
 * the header in exactly one place - a shared value would be right here and
 * wrong in the next place.
 */

/*---------------------------------------------------------------------------*/

/* THE INPUT.  As long as the length field is not complete there is nothing to
 * say; as long as the announced number of bytes is not complete, neither.
 * And when both are complete, the CRC decides whether this was a frame at all.
 */
static int
axtcp_rx(
struct tcpsock *tp)
{
  int len;
  int off = 0;

  while (tp->len - off >= 2) {
    len = (tp->buf[off] << 8) | tp->buf[off + 1];
    /* A LENGTH FIELD THAT CANNOT BE.  Unlike KISS there is no way here to
     * find the stream again: every byte that arrives now only changes WHERE
     * the absurdity begins.  So we disconnect, and the port waits for the
     * next attempt.
     */
    if (len > AXTCP_MAXLEN) {
      tp->overruns++;
      /* BOTH COUNTERS, and not only the one that "attach ... stat" shows.  A
       * wrong frame is a wrong frame whether one asks the session or the
       * interface: ifp->crcerrors is what ifconfig prints and what axip.c and
       * kiss.c have always counted, and a port that reports its errors only
       * in one of the two places is half as good as axip (Thomas).
       */
      tp->ifp->crcerrors++;
      tcpsock_forget(tp);
      return -1;
    }
    if (tp->len - off < 2 + len)
      break;			/* not complete yet: wait */

    /* THE CRC IS THE TRANSCRIPT, and it is the only proof that the frame was
     * complete and in the right place.  Without it everything that is not
     * just accidentally two bytes long would pass here.
     */
    if (len < 2 || !check_crc_ccitt((char *) tp->buf + off + 2, len)) {
      tp->crcerr++;
      tp->ifp->crcerrors++;	/* the interface counts too - see above */
      tcpsock_forget(tp);
      return -1;
    }
    /* THE KEEPALIVE, and it is recognisable without guessing: a length of 2 is
     * a frame of no bytes with the two CRC bytes over nothing, and the CRC of
     * nothing is 0x0000.  check_crc_ccitt() above has just confirmed that the
     * two bytes behind the length field are exactly that, so there is nothing
     * left to decide.
     *
     * IT IS NOT AN ERROR AND NOT A FRAME, and both matter.  Passed on to
     * tcpsock_input() it would come back as an overrun - that one counts a
     * length below two addresses (tcpsock.c:181) - and a port that sends one
     * of these every five minutes would then show an overrun that climbs by
     * twelve an hour for ever, which is the one thing a counter must not do.
     * And it must not reach ax25() as a zero byte frame, because ax25 would
     * file it as a heard station.
     */
    if (len == 2) {
      tp->idlesin++;
      off += 2 + len;
      continue;
    }
    /* WITHOUT THE TWO CRC BYTES, because ax25 appends its own again.
     */
    tcpsock_input(tp, tp->buf + off + 2, len - 2, AXTCP_CHAN);
    off += 2 + len;
  }
  /* SEVERAL FRAMES IN ONE read() ARE THE NORMAL CASE, not the exception: a
   * TCP MTU of 1460 and a frame of 60 bytes mean twenty-four frames at once.
   * Anyone who reads only one loses the rest in silence, and the loss only
   * shows when the peer goes quiet (Thomas).
   */
  if (off > 0) {
    memmove(tp->buf, tp->buf + off, (size_t) (tp->len - off));
    tp->len -= off;
  }
  return 0;
}

/*---------------------------------------------------------------------------*/

/* THE OUTPUT.  Length, then the frame, and the CRC belongs inside the length
 * area - it is part of what the length numbers.
 */
static int
axtcp_tx(
struct tcpsock *tp,
unsigned char *out,
int max,
struct mbuf **bpp)
{
  int l;

  if (max < 3)
    return -1;
  /* THE CRC BEFORE THE PULLUP: pullup() hands over exactly the area that goes
   * out, and exactly that is what the length says.
   */
  append_crc_ccitt(*bpp);
  if ((l = pullup(bpp, out + 2, (uint) (max - 2))) <= 0 || *bpp)
    return -1;
  out[0] = (unsigned char) ((l >> 8) & 0xff);
  out[1] = (unsigned char) (l & 0xff);
  return l + 2;
}

/*---------------------------------------------------------------------------*/

/* THE KEEPALIVE OF THIS PROTOCOL: 00 02 00 00.
 *
 * A LENGTH OF 2, because the length counts the frame together with its CRC
 * (TODO.txt: "Laengenfeld Big-Endian, umfasst Payload", and the payload is
 * "vollstaendiger AX.25-Frame einschl. FCS").  Two bytes of payload is
 * therefore an AX.25 frame of no bytes and the CRC of nothing, and the CRC of
 * nothing is 0x0000 - which is why the packet is three constants and not a
 * computed one.
 *
 * A LENGTH OF 0 WOULD BE THE OBVIOUS WRONG ANSWER, and it is worth writing
 * down why it is wrong: axtcp_rx() rejects a length below 2 in the same breath
 * in which it checks the CRC, so 00 00 00 00 is read as a frame that cannot
 * be and tears the session down.  A keepalive that killed the connection it
 * was meant to hold would be found out in production and not in a test.
 *
 * ZERO BYTES OF TCP WRITE ARE NOT AN OPTION: a write of length 0 says nothing
 * to the kernel, produces no segment, and never reaches the NAT box that is
 * the whole point of the exercise.
 */
static int
axtcp_keepalive(
struct tcpsock *tp,
unsigned char *out,
int max)
{
  (void) tp;
  if (max < 4)
    return -1;
  out[0] = 0x00;
  out[1] = 0x02;		/* two bytes of payload: 0 frame + 2 FCS */
  out[2] = 0x00;		/* the CRC of nothing */
  out[3] = 0x00;
  return 4;
}

/*---------------------------------------------------------------------------*/

/* THE FIRST BYTE DECIDES.  0xc0 is FEND and therefore KISS - and because 0xc0
 * as a length would be 49152, it cannot be anything else here.  Everything
 * else is an AXTCP candidate, and what confirms it is not the first byte but
 * the first CRC that comes out right.
 */
static int
axtcp_detect(
struct tcpsock *tp)
{
  if (tp->len < 1)
    return 0;			/* nothing there yet: keep waiting */
  if (tp->buf[0] == FR_END)
    return -1;			/* that is KISS */
  tp->proto = TCPAD_AXTCP;
  return 1;
}

/*---------------------------------------------------------------------------*/

static int
axtcp_init(
void)
{
  return tcpsock_init();
}

/* attach axtcp [<label> [listen [<port>] | client <host>[:<port>]]]
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
axtcp_attach(
int argc,
char *argv[],
void *p)
{
  struct iface *ifp;
  struct tcpsock *tp;
  char ifname[64];
  const char *bindword = NULL;
  char *av[10];
  char *host = NULL;
  char hostbuf[1024];
  char servbuf[32];
  struct tcpsock_bindlist *binds = NULL;
  int family = AF_UNSPEC;
  long tmp;
  /* TWO PORT NUMBERS, and not one.  "listen 8000 client host:8001" is one
   * interface that answers on 8000 and calls on 8001, and a single
   * variable would make whichever word came last the answer to both
   * questions - so "listen 20102 client 127.0.0.1:20103" listened on
   * 20103 and said nothing (Thomas).
   */
  int lport = AXTCP_DEFAULT_PORT;		/* the port this port ANSWERS on */
  int dport = AXTCP_DEFAULT_PORT;		/* the port this port CALLS on */
  int client = 0;
  int listen = 0;
  int ac = 0;
  int i;
  int keepalive = -1;		/* -1 = not written, so the default applies */

  (void) p;
  /* "bind=" AND "keepalive" ARE THE TWO WORDS THAT MAY STAND ANYWHERE, and
   * both are taken out here, before anything reads a position.  A word that may
   * only be written in one particular place is an extra rule to think about
   * while copying a line out.
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
      printf("Usage: attach axtcp [<label> [listen [<port>]]]"
	     " [client <host>[:<port>]] [keepalive <seconds>]\n");
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

  /* "stat" IS NOT A LABEL.  The word in second place decides whether a port is
   * built at all - otherwise "attach axtcp stat" would create a port named
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
    return tcpsock_stat(statif, TCPAD_AXTCP, argc, argv, p);
  }

  /* "listen" AND "client" ARE NOT LABELS, and a first word that is one of
   * them is the classic mistype: the name comes first, the option second, so
   * "attach axtcp listen 999" builds a port NAMED "listen" - and the next
   * attempt says "Interface listen already exists", a message about a port
   * nobody meant to make (the "stat" case above has the same shape, Thomas).
   */
  if (argc >= 2 &&
      (!strcmp(argv[1], "listen") || !strcmp(argv[1], "client"))) {
    printf("\"%s\" is taken as the interface NAME here - the name stands\n"
	   "first and the option after it: \"attach axtcp <name> %s ...\"\n",
	   argv[1], argv[1]);
    return 1;
  }
  if (argc >= 2) {
    strncpy(ifname, argv[1], sizeof(ifname) - 1);
    ifname[sizeof(ifname) - 1] = 0;
  } else {
    strncpy(ifname, "axtcp", sizeof(ifname) - 1);
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

  /* "listen" AND "client" ARE TWO WORDS, NOT TWO MODES, and both may stand:
   * a node that answers an XRouter AND holds a session to a partner is both of
   * those at once, and it is one interface - one name for "ifconfig", one
   * "keepalive", one thing to trace.  Making them modes meant writing the
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
	printf("\"client\" wants a host, as in \"client db0sao.ampr.org:8000\"\n");
	printf("Usage: attach axtcp [<label> [listen [<port>]]] "
	       "[client <host>[:<port>]]\n");
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
       * honoured: "attach axtcp al listen 19966 19967" bound 19966 and said
       * nothing at all about 19967, which is not a shorter answer than this
       * one.
       */
      printf("Usage: attach axtcp [<label> [listen [<port>]]]"
	     " [client <host>[:<port>]] [keepalive <seconds>]\n");
      if (i < argc)
	printf("... and \"%s\" is one word too many.\n", argv[i]);
      return 1;
    }
  }
  /* NEITHER WORD IS A LISTENER, on the default port: that is the more frequent
   * form and the one where you need not decide on a word.
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
   * the frame arrives without a KISS cap and ax_recv() is the receiver both
   * protocols need.
   */
  setencap(ifp, "AX25UI");
  /* THE WAY OUT, and it is a hook as in axip - the framing stays with the tx
   * hook, because AXTCP must count the CRC into its length field and KISS does
   * not send one along.
   */
  ifp->raw = tcpsock_raw;
  /* THE LINK ERROR CHECK ON.  The KISS frame does not carry it over TCP -
   * XRouter sends none - but ax25 appends one when it sends, and these bits
   * have to come off again, or nobody sends anything.
   */
  ifp->crccontrol = CRC_CCITT;
  /* THE KEEPALIVE, and the default is on: see TCP_KEEPALIVE_DEFAULT for why a
   * line that is quiet needs a packet on it.  The written value wins, and 0 is
   * a written value - "keepalive 0" means off and not "use the default".
   */
  ifp->keepalive = keepalive < 0 ? TCP_KEEPALIVE_DEFAULT : keepalive;

  /* ONE PORT WITH BOTH ROLES, and the two hooks are one set of four - the same
   * bytes either way.  Only the flags say which half of it is wanted, and the
   * session list is what "attach axtcp stat" walks.
   *
   * "bind=" BELONGS TO THE PORT AND NOT TO A ROLE, and that is what makes both
   * words work with one list: a listener makes a socket per entry, and the
   * session the client holds leaves by the entry of the peer's family.  A
   * listener and a client on one interface that want different local addresses
   * would need two interfaces - and two interfaces cannot hold one port number
   * between them, which is why they are one interface (Thomas).
   */
  if ((tp = tcpsock_new(ifp, (client ? TCF_CLIENT : 0) | (listen ? TCF_LISTEN : 0),
			TCPAD_AXTCP)) == NULL) {
    if (host)
      free(host);
    tcpsock_bind_free(binds);
    if_detach(ifp);
    return 1;
  }
  tp->rx = axtcp_rx;
  tp->tx = axtcp_tx;
  tp->detect = axtcp_detect;
  tp->keepalive = axtcp_keepalive;
  /* THE LIST IS THE PORT'S, and not the call's: the rebuild needs it, and a
   * session that rings again has to leave by the address it was told to leave
   * by.  tcpsock_forget() takes it away with the port.
   */
  tp->binds = binds;

  if (listen) {
    if (tcpsock_listen(tp, binds, lport)) {
      /* tcpsock_listen() has said which address and why.  A second sentence
       * here would only repeat the port number at somebody who has just been
       * told the address.
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
  /* THE TICK, and it has to be started HERE.  tcpsock_init() exists and does
   * exactly this, but nothing calls it: the axtcp_init() that was meant to call
   * it is unreferenced (the compiler says so with -Wall), and no command table
   * entry leads there.  The consequence is not visible at the attach - the port
   * listens and answers - and only shows later: nothing in tcpsock_timer_task()
   * runs, so a client never reconnects, a connection that is not identified
   * never hits its five minutes, and the keepalive never ticks.
   *
   * CALLED FROM BOTH MODULES it is harmless: the first call starts the tick and
   * every further one sees it running (see tcpsock_init()).
   */
  tcpsock_init();
  ifp->next = Ifaces;
  Ifaces = ifp;
  return 0;
}