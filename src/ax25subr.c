/* @(#) $Id: ax25subr.c,v 1.29 2005/03/11 14:42:13 dl9sau Exp $ */

/* Low level AX.25 routines:
 *  callsign conversion
 *  control block management
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "iface.h"
#include "lapb.h"
#include "pidfilter.h"
#include "slhc.h"

struct ax25_cb *Ax25_cb;

/* Default AX.25 parameters */
int   Maxframe = 7;             /* Transmit flow control level */
int   N2 = 10;                  /* 10 retries */
/* The modulo-128 window.  Kept apart from Maxframe rather than widening it,
 * because the two are limits on different links and an operator who raised
 * one would otherwise silently raise the other.  32 is what the Linux kernel
 * uses as AX25_DEF_EWINDOW; the protocol allows up to 63.
 */
int   EMaxframe = 32;           /* Modulo-128 transmit flow control level */
int   Axwindow = 2048;          /* 2K incoming text before RNR'ing */
int   Paclen = 256;             /* 256-byte I fields */
int   Pthresh = 64;             /* Send polls for packets larger than this */
int   T1init = 5000;            /* Retransmission timeout, ms */
int   T2init = 300;             /* Acknowledge delay timeout, ms */
int   T3init = 900000;          /* Idle polling timeout, ms */
int   T4init = 60000;           /* Busy timeout, ms */
int   T5init = 3600000;         /* Idle disconnect timeout, ms */
enum lapb_version Axversion = V2; /* Protocol version */
int32 Blimit = 16;              /* Retransmission backoff limit */
int   Axigntos;                 /* Ignore TOS */

static int Nextid = 1;          /* Next control block ID */

/* Look up entry in connection table.  A link is the PAIR (ours, his), both
 * with ssid - see the note at the declaration.  A null local address asks the
 * old question, "any link to him", and is for displays only.
 *
 * Relay legs are skipped as before: their hdr.source carries the CALLER's
 * address, which we speak in but do not answer to, and axp->peer is what
 * tells them apart.
 */
struct ax25_cb *
find_ax25(uint8 *local, uint8 *remote)
{
	struct ax25_cb *axp;
	struct ax25_cb *axlast = NULL;

	/* Search list */
	for(axp = Ax25_cb; axp != NULL; axlast=axp,axp = axp->next){
		if(axp->peer == NULL && addreq(axp->hdr.dest,remote)
		   && (local == NULL || addreq(axp->hdr.source,local))){
			if(axlast != NULL){
				/* Move entry to top of list to speed
				 * future searches
				 */
				axlast->next = axp->next;
				axp->next = Ax25_cb;
				Ax25_cb = axp;
			}
			return axp;
		}
	}
	return NULL;
}

/* Is this block still in the table?  For a caller that had to let go of it -
 * an upcall may have taken it away while that caller was inside one - and
 * wants to know before touching it again.
 */
int ax25_alive(const struct ax25_cb *conn)
{
	struct ax25_cb *axp;

	for(axp = Ax25_cb; axp != NULL; axp = axp->next)
		if(axp == conn)
			return 1;
	return 0;
}

/*---------------------------------------------------------------------------*/

/* Remove entry from connection table */
void
del_ax25(struct ax25_cb *conn)
{
	int i;
	struct ax25_cb *axp;
	struct ax25_cb *axlast = NULL;
	struct ax25_cb *peer;

	for(axp = Ax25_cb; axp != NULL; axlast=axp,axp = axp->next){
		if(axp == conn)
			break;
	}
	if(axp == NULL)
		return; /* Not found */

	/* Half a link that stays in the node is no link at all, so the other
	 * half goes too.  Here and not only in disc_ax25(), because a block
	 * can be taken away without ever being disconnected - "reset" does
	 * exactly that - and the far half would then sit there as "Connected"
	 * with nobody at the other end.
	 *
	 * REMEMBERED HERE AND USED AT THE END, after this block is out of the
	 * list and freed.  Taking the far half down runs consumers' upcalls,
	 * and one of those may well come back through del_ax25() for THIS
	 * block - which would find it still listed, unlink it a second time
	 * and free it twice.  Unlinking the pair first stops the far half
	 * from finding its way back, but not a path that has the pointer
	 * already.
	 */
	peer = conn->loop;
	if(peer != NULL){
		conn->loop = NULL;
		peer->loop = NULL;
		peer->reason = LB_NORMAL;
	}

	/* Remove from list */
	if(axlast != NULL)
		axlast->next = axp->next;
	else
		Ax25_cb = axp->next;

	/* Timers should already be stopped, but just in case... */
	stop_timer(&axp->t1);
	stop_timer(&axp->t2);
	stop_timer(&axp->t3);
	stop_timer(&axp->t4);
	stop_timer(&axp->t5);
	stop_timer(&axp->loop_timer);

	/* Free allocated resources */
	for (i = 0; i < 8; i++)
		free_p(&axp->reseq[i].bp);
	free_q(&axp->txq);
	free_q(&axp->rxasm);
	while(axp->services != NULL){
		struct axservice *sp = axp->services;

		axp->services = sp->next;
		free_q(&sp->rxq);
		free(sp);
	}
#ifdef	AX25_VJCOMP
        /* MW: free VJ related structures */
        if (axp->slcomp) {
                slhc_free(axp->slcomp);
        }
#endif
	free(axp);

	/* And now the far half of a local link, with this one gone: whatever
	 * its consumers do on the way down, they cannot come back to a block
	 * that is no longer in the list.
	 */
	if(peer != NULL)
		lapbstate(peer,LAPB_DISCONNECTED);
}

/* Create an ax25 control block: a NEW structure, filled with all the
 * defaults.  The caller is still responsible for filling in the reply
 * address, and build_path() is what normally does it.
 *
 * It used to look the address up first and hand back what it found.  Every
 * caller had already searched by then - one of them by passing a blank
 * address, which was a way of saying "and do not find anything" - so the
 * search only made it possible to get a block back that was someone else's
 * link.  Now: whoever wants an existing link asks find_ax25() and gets to say
 * which link that is; whoever asks here gets a new one.
 *
 * The address is used for nothing but the VJ compression setting, and may be
 * null where the caller has not got one yet.
 */
struct ax25_cb *
cr_ax25(uint8 *remote)
{
	struct ax25_cb *axp;
#ifdef	AX25_VJCOMP
	struct ax_route *rp;
#endif

	axp = (struct ax25_cb *)callocw(1,sizeof(struct ax25_cb));
	axp->next = Ax25_cb;
	Ax25_cb = axp;
#ifdef	AX25_VJCOMP
	/* MW: init structures for VJ */
	if (remote != NULL && (rp = ax_routeptr(remote, 0)) != NULL) {
	    if (rp->vjcomp)
		axp->slcomp_enable = 1;

	}
#endif
	axp->state = LAPB_DISCONNECTED;
	/* Modulo-8 until something says otherwise.  This must be set and not
	 * left at the zero callocw() gives, because every sequence number on
	 * the link is masked with it - a zero mask would quietly hold V(S) and
	 * V(R) at zero for ever.
	 */
	axp->mmask = MMASK;
	axp->maxframe = 1;
	axp->window = Axwindow;
	axp->paclen = Paclen;
	axp->proto = Axversion; /* Default, can be changed by other end */
	axp->pthresh = Pthresh;
	axp->n2 = N2;
	axp->t1.func = recover;
	axp->t1.arg = axp;

	set_timer(&axp->t2,T2init);
	axp->t2.func = ax_t2_timeout;
	axp->t2.arg = axp;

	set_timer(&axp->t3,T3init);
	axp->t3.func = pollthem;
	axp->t3.arg = axp;

	set_timer(&axp->t4,T4init);
	axp->t4.func = pollthem;
	axp->t4.arg = axp;

	set_timer(&axp->t5,T5init);
	axp->t5.func = ax_t5_timeout;
	axp->t5.arg = axp;

	/* No consumer by default: the first frame for a protocol id asks the
	 * configuration what, if anything, should serve it.
	 */

	axp->id = Nextid++;

	return axp;
}

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
int
setcall(uint8 *out,const char *call)
{
	int i,csize;
	unsigned ssid;
	char *dp,c;

	if(out == NULL || call == NULL || *call == '\0')
		return -1;

	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	/* A "*" is not part of a callsign: it is the has-been-repeated mark of
	 * the TNC2 notation, and it belongs in the SSID byte, not in the text.
	 * It used to be refused in one spelling and swallowed in the other -
	 * "DB0AAA*" made the callsign field seven characters and failed, while
	 * atoi() in "DB0AAA-1*" stopped at the star and the mark was dropped
	 * without a word.  Refuse both alike; the day a caller carries the bit,
	 * it takes the mark off and sets it itself, and this still sees only a
	 * callsign.
	 *
	 * Deliberately only this character.  setcall() is tolerant of other
	 * trailing junk, and at least one caller has been relying on that - see
	 * the note about a stray CR in remote_net.c.
	 */
	if(strchr(call,'*') != NULL)
		return -1;

	dp = strchr(call,'-');
	if(dp == NULL)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > ALEN)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULL){
		dp++;   /* skip dash */
		ssid = atoi(dp);
		if(ssid > 15)
			return -1;
	} else
		ssid = 0;
	/* Copy upper-case callsign, left shifted one bit */
	for(i=0;i<csize;i++){
		c = *call++;
		if(islower(c))
			c = Xtoupper(c);
		*out++ = c << 1;
	}
	/* Pad with shifted spaces if necessary */
	for(;i<ALEN;i++)
		*out++ = ' ' << 1;

	/* Insert substation ID field and set reserved bits */
	*out = 0x60 | (ssid << 1);
	return 0;
}
int
addreq(const uint8 *a,const uint8 *b)
{
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	return (*a & SSID) == (*b & SSID);
}
/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULL otherwise.
 */
struct iface *
ismyax25addr(
const uint8 *addr)
{
	struct iface *ifp;

	for (ifp = Ifaces; ifp; ifp = ifp->next)
		if (ifp->output == ax_output && addreq(ifp->hwaddr,addr))
			break;
	return ifp;
}
void
addrcp(
uint8 *to,
const uint8 *from)
{
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to = (*from & SSID) | 0x60;
}
/* Convert encoded AX.25 address to printable string */
char *
pax25(char *e,const uint8 *addr)
{
	int i;
	char c,*cp;

	cp = e;
	for(i=ALEN;i != 0;i--){
		c = (*addr++ >> 1) & 0x7f;
		if(c != ' ')
			*cp++ = c;
	}
	if((*addr & SSID) != 0)
		sprintf(cp,"-%d",(*addr >> 1) & 0xf);   /* ssid */
	else
		*cp = '\0';
	return e;
}

/* Figure out the frame type from the control field
 * This is done by masking out any sequence numbers and the
 * poll/final bit after determining the general class (I/S/U) of the frame
 */
uint
ftype(uint control)
{
	if((control & 1) == 0)  /* An I-frame is an I-frame... */
		return I;
	if(control & 2)         /* U-frames use all except P/F bit for type */
		return control & ~PF;
	else                    /* S-frames use low order 4 bits for type */
		return control & 0xf;
}

/* Parse the target of a connect.  Two ways of writing the same thing:
 *
 *      [ax25|flextalk] [<port>:][ ]<dest> [via] [<digi> ...] [< <mycall>]
 *      <dest> --port <port> --mycall <call> --pid <n> --silent
 *
 * <port> is the name of an interface, not a number - WAMPES has no port
 * numbering, it has labels, and a sysop who wants to type "2:" names the
 * interface "2".  One identity for a port, not two that can drift apart.
 *
 * Errors are written to err rather than printed: the console wants them on
 * the screen, the service socket wants them on its socket, and a parser has
 * no business deciding which.
 *
 * Anything not given stays as it was: hdr->source is Mycall and the routing
 * table picks the interface, exactly as before.
 */

int
ax25_parse_target(
int argc,
char *argv[],
struct ax25 *hdr,
struct ax25_opts *opts,
int *pid,
int *silent,
char *err,
int errlen)
{

	char *cp;
	char *p;
	char *dest = 0;
	char *port = 0;
	char *source = 0;
	int i;
	long n;
	struct iface *ifp;

	memset(hdr, 0, sizeof(struct ax25));
	memset(opts, 0, sizeof(struct ax25_opts));
	*pid = PID_NO_L3;
	*silent = 0;
	*err = '\0';

#define fail(...) { snprintf(err, errlen, __VA_ARGS__); return 1; }
#define needarg(what) \
	if (++i >= argc) fail("%s needs a value", what)

	for (i = 0; i < argc; i++) {
		cp = argv[i];
		/* The protocol word may be left out - no protocol name is a
		 * valid callsign, so there is nothing to confuse.  "flextalk"
		 * is a name for one pid and nothing else; --pid still wins.
		 */
		if (i == 0 && !strcmp(cp, "ax25"))
			continue;
		if (i == 0 && !strcmp(cp, "flextalk")) {
			/* Through the same table as everything else, so the
			 * number lives in one place - see pidfilter.c.
			 *
			 * NOT widened to every protocol name, and the reason
			 * is that this position already has an occupant: the
			 * word in front of a connect names a TRANSPORT, not a
			 * protocol id.  "connect netrom DB0AAA" is a NET/ROM
			 * session - connect_command() takes it before this
			 * parser ever sees it - while "pid=netrom" on a listen
			 * line means AX.25 protocol id 0xcf, NET/ROM level 3
			 * riding on an AX.25 connection.  One word, two
			 * meanings, in two different namespaces; letting PID
			 * names in here would fuse them.  ("arp" and "lq"
			 * would be trouble of a milder kind: setcall() takes
			 * them as callsigns, and the destination stands in
			 * this same position.)
			 *
			 * "flextalk" is safe because it names no transport and
			 * has stood here since 2005.  For everything else
			 * there is "--pid", which takes a name too now.
			 */
			*pid = pid_number("flextalk");
			continue;
		}
		if (!strcmp(cp, "via"))
			continue;               /* noise word, always has been */
		if (!strcmp(cp, "--silent")) {
			*silent = 1;
			continue;
		}
		if (!strcmp(cp, "--mycall")) {
			needarg("--mycall");
			source = argv[i];
			continue;
		}
		if (!strcmp(cp, "--port")) {
			needarg("--port");
			port = argv[i];
			continue;
		}
		if (!strcmp(cp, "--pid")) {
			needarg("--pid");
			if ((*pid = pid_number(argv[i])) < 0)
				fail("invalid pid \"%s\" - a name or a number, "
				     "see \"ax25 pid-info\"", argv[i]);
			continue;
		}
		if (*cp == '<') {
			/* "< CALL" and "<CALL" both, the shell habit either way */
			if (cp[1])
				source = cp + 1;
			else {
				needarg("<");
				source = argv[i];
			}
			continue;
		}
		if (!strncmp(cp, "--", 2))
			fail("unknown option \"%s\"", cp);

		cp = argv[i];
		if (!dest) {
			/* The port may ride in front of the destination */
			if ((p = strchr(cp, ':'))) {
				*p = '\0';
				port = cp;
				/* "hf1:DB0AAA-8" and "hf1: DB0AAA-8" both -
				 * XNET takes the space too, and a habit is a
				 * habit.
				 */
				if (!p[1])
					continue;
				cp = p + 1;
			}
		}

		/* Commas separate a path as well as spaces do.  The node world
		 * writes "DB0AAA-8 DB0BBB DB0CCC", everything outside it
		 * writes "DB0AAA-8,DB0BBB,DB0CCC", and datagram takes only the
		 * second because TNC2 says so.  Being strict here would buy
		 * nothing but a wrong guess about where the operator learned
		 * to type.
		 */
		for (p = strtok(cp, ","); p; p = strtok(NULL, ",")) {
			if (!dest) {
				dest = p;
				continue;
			}
			if (hdr->ndigis >= MAXDIGIS)
				fail("too many digipeaters (at most %d)",
				     MAXDIGIS);
			if (setcall(hdr->digis[hdr->ndigis], p))
				fail("invalid call \"%s\"", p);
			hdr->ndigis++;
		}
	}

	if (!dest)
		fail("no destination");
	if (setcall(hdr->dest, dest))
		fail("invalid call \"%s\"", dest);

	if (source) {
		if (setcall(hdr->source, source))
			fail("invalid call \"%s\"", source);
		opts->ownsource = 1;
	} else
		addrcp(hdr->source, Mycall);

	if (port) {
		if (!(ifp = if_lookup(port)))
			fail("no interface \"%s\"", port);
		if (ifp->output != ax_output)
			fail("interface \"%s\" does not carry AX.25", port);
		opts->iface = ifp;
	}

#undef needarg
#undef fail

	return 0;
}

/*---------------------------------------------------------------------------*/

int
ax25args_to_hdr(
int argc,
char *argv[],
struct ax25 *hdr)
{
  hdr->ndigis = hdr->nextdigi = hdr->ext = 0;
  if (argc < 1) {
    printf("Missing call\n");
    return 1;
  }
  addrcp(hdr->source,Mycall);
  if (setcall(hdr->dest,*argv)) {
    printf("Invalid call \"%s\"\n",*argv);
    return 1;
  }
  while (--argc) {
    argv++;
    if (strncmp("via",*argv,strlen(*argv))) {
      if (hdr->ndigis >= MAXDIGIS) {
	printf("Too many digipeaters (max %d)\n",MAXDIGIS);
	return 1;
      }
      if (setcall(hdr->digis[hdr->ndigis++],*argv)) {
	printf("Invalid call \"%s\"\n",*argv);
	return 1;
      }
    }
  }
  return 0;
}

char *
ax25hdr_to_string(
struct ax25 *hdr)
{

  char *p;
  int i;
  static char buf[128];

  if (!*hdr->dest) return "*";
  p = buf;
  if (*hdr->source) {
    pax25(p,hdr->source);
    while (*p) p++;
    *p++ = '-';
    *p++ = '>';
  }
  pax25(p,hdr->dest);
  while (*p) p++;
  for (i = 0; i < hdr->ndigis; i++) {
    strcpy(p,i == 0 ? " via " : ",");
    while (*p) p++;
    pax25(p,hdr->digis[i]);
    while (*p) p++;
    if (i < hdr->nextdigi) *p++ = '*';
  }
  *p = 0;
  return buf;
}
