/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25subr.c,v 1.8 1993/01/29 06:48:16 deyke Exp $ */

/* Low level AX.25 routines:
 *  callsign conversion
 *  control block management
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include "lapb.h"
#include <ctype.h>

struct ax25_cb *Ax25_cb;

/* Default AX.25 parameters */
int Maxframe = 7;               /* Transmit flow control level */
int N2 = 10;                    /* Retry limit */
int Axwindow = 2048;            /* Local flow control limit */
int Paclen = 236;               /* Maximum outbound packet size */
int Pthresh = 64;               /* Send polls for packets larger than this */
int T1init = 5000;              /* Retransmission timeout, ms */
int T2init = 2;                 /* Acknowledgement delay timeout, ms */
int T3init = 900000;            /* No-activity timeout, ms */
int T4init = 60000;             /* Busy timeout, ms */
int T5init = 100;               /* Packet assembly timeout, ms */

/* Look up entry in connection table */
struct ax25_cb *
find_ax25(addr)
register char *addr;
{
	register struct ax25_cb *axp;
	struct ax25_cb *axlast = NULLAX25;

	/* Search list */
	for(axp = Ax25_cb; axp != NULLAX25; axlast=axp,axp = axp->next){
		if(axp->peer == NULLAX25 && addreq(axp->hdr.dest,addr)){
			if(axlast != NULLAX25){
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
	return NULLAX25;
}

/* Remove entry from connection table */
void
del_ax25(conn)
struct ax25_cb *conn;
{

	int i;
	register struct ax25_cb *axp;
	struct ax25_cb *axlast = NULLAX25;

	for(axp = Ax25_cb; axp != NULLAX25; axlast=axp,axp = axp->next){
		if(axp == conn)
			break;
	}
	if(axp == NULLAX25)
		return; /* Not found */

	if(axp == Axcb_server)
		Axcb_server = NULLAX25;

	/* Remove from list */
	if(axlast != NULLAX25)
		axlast->next = axp->next;
	else
		Ax25_cb = axp->next;

	/* Timers should already be stopped, but just in case... */
	stop_timer(&axp->t1);
	stop_timer(&axp->t2);
	stop_timer(&axp->t3);
	stop_timer(&axp->t4);
	stop_timer(&axp->t5);

	/* Free allocated resources */
	for (i = 0; i < 8; i++)
		free_p(axp->reseq[i].bp);
	free_q(&axp->sndq);
	free_q(&axp->txq);
	free_q(&axp->rxq);
	free((char *)axp);
}

/* Create an ax25 control block. Allocate a new structure, if necessary,
 * and fill it with all the defaults. The caller
 * is still responsible for filling in the reply address
 */
struct ax25_cb *
cr_ax25(prototype)
struct ax25_cb *prototype;
{
	register struct ax25_cb *axp;

  if (prototype) {
    axp = malloc(sizeof(*axp));
    *axp = *prototype;
  } else
    axp = calloc(1, sizeof(*axp));
  axp->state = LAPB_DISCONNECTED;
  axp->cwind = 1;
  axp->t1.func = recover;
  axp->t1.arg = axp;
  axp->t2.func = (void (*)()) t2_timeout;
  axp->t2.arg = axp;
  axp->t3.func = pollthem;
  axp->t3.arg = axp;
  axp->t4.func = (void (*)()) t4_timeout;
  axp->t4.arg = axp;
  axp->t5.func = (void (*)()) t5_timeout;
  axp->t5.arg = axp;
  axp->next = Ax25_cb;
  return Ax25_cb = axp;
}

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
int
setcall(out,call)
char *out;
char *call;
{
	int csize;
	unsigned ssid;
	register int i;
	register char *dp;
	char c;

	if(out == NULLCHAR || call == NULLCHAR || *call == '\0')
		return -1;

	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	dp = strchr(call,'-');
	if(dp == NULLCHAR)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > ALEN)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULLCHAR){
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
			c = toupper(c);
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
addreq(a,b)
const char *a;
const char *b;
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
 * NULLIF otherwise.
 */
struct iface *
ismyax25addr(addr)
const char *addr;
{
	register struct iface *ifp;

	for (ifp = Ifaces; ifp; ifp = ifp->next)
		if (ifp->output == ax_output && addreq(ifp->hwaddr, addr))
			break;
	return ifp;
}
void
addrcp(to,from)
char *to;
const char *from;
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
pax25(e,addr)
char *e;
char *addr;
{
	register int i;
	char c;
	char *cp;

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
int16
ftype(control)
register int control;
{
	if((control & 1) == 0)  /* An I-frame is an I-frame... */
		return I;
	if(control & 2)         /* U-frames use all except P/F bit for type */
		return (int16)(uchar(control) & ~PF);
	else                    /* S-frames use low order 4 bits for type */
		return (int16)(uchar(control) & 0xf);
}

int
ax25args_to_hdr(argc, argv, hdr)
int argc;
char *argv[];
struct ax25 *hdr;
{
  hdr->ndigis = hdr->nextdigi = 0;
  if (argc < 1) {
    printf("Missing call\n");
    return 1;
  }
  if (setcall(hdr->dest, *argv)) {
    printf("Invalid call \"%s\"\n", *argv);
    return 1;
  }
  while (--argc) {
    argv++;
    if (strncmp("via", *argv, strlen(*argv))) {
      if (hdr->ndigis >= MAXDIGIS) {
	printf("Too many digipeaters (max %d)\n", MAXDIGIS);
	return 1;
      }
      if (setcall(hdr->digis[hdr->ndigis++], *argv)) {
	printf("Invalid call \"%s\"\n", *argv);
	return 1;
      }
    }
  }
  return 0;
}

char *
ax25hdr_to_string(hdr)
struct ax25 *hdr;
{

  char *p;
  int i;
  static char buf[128];

  if (!*hdr->dest) return "*";
  p = buf;
  if (*hdr->source) {
    pax25(p, hdr->source);
    while (*p) p++;
    *p++ = '-';
    *p++ = '>';
  }
  pax25(p, hdr->dest);
  while (*p) p++;
  for (i = 0; i < hdr->ndigis; i++) {
    strcpy(p, i == 0 ? " via " : ",");
    while (*p) p++;
    pax25(p, hdr->digis[i]);
    while (*p) p++;
    if (i < hdr->nextdigi) *p++ = '*';
  }
  *p = 0;
  return buf;
}

