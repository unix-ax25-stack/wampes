/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.c,v 1.15 1993/01/29 06:48:15 deyke Exp $ */

/* Low level AX.25 code:
 *  incoming frame processing (including digipeating)
 *  IP encapsulation
 *  digipeater routing
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "lapb.h"
#include "netrom.h"
#include "ip.h"
#include "devparam.h"
#include <ctype.h>
#include "lapb.h"

static int axsend __ARGS((struct iface *iface,char *dest,char *source,
	int cmdrsp,int ctl,struct mbuf *data));
static int axroute_hash __ARGS((const char *call));

/* List of AX.25 multicast addresses in network format (shifted ascii).
 * Only the first entry is used for transmission, but an incoming
 * packet with any one of these destination addresses is recognized
 * as a multicast.
 */
char Ax25multi[][AXALEN] = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* QST */
#if 0
	'M'<<1, 'A'<<1, 'I'<<1, 'L'<<1, ' '<<1, ' '<<1, '0'<<1, /* MAIL */
#endif
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1, '0'<<1, /* NODES */
#if 0
	'I'<<1, 'D'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* ID */
	'O'<<1, 'P'<<1, 'E'<<1, 'N'<<1, ' '<<1, ' '<<1, '0'<<1, /* OPEN */
	'C'<<1, 'Q'<<1, ' '<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* CQ */
	'B'<<1, 'E'<<1, 'A'<<1, 'C'<<1, 'O'<<1, 'N'<<1, '0'<<1, /* BEACON */
	'R'<<1, 'M'<<1, 'N'<<1, 'C'<<1, ' '<<1, ' '<<1, '0'<<1, /* RMNC */
	'A'<<1, 'L'<<1, 'L'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1, /* ALL */
#endif
	'\0',
};
char Mycall[AXALEN];
struct ax_route *Ax_routes[AXROUTESIZE];
struct iface *Axroute_default_ifp;
int Digipeat = 2;       /* Controls digipeating */

int
axi_send(bp,iface,gateway,tos)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int tos;
{
	return axui_send(bp,iface,gateway,tos);
}

/* Send IP datagrams across an AX.25 link */
int
axui_send(bp,iface,gateway,tos)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int tos;
{
	char *hw_addr;

	if((hw_addr = res_arp(iface,ARP_AX25,gateway,bp)) == NULLCHAR)
		return 0;       /* Wait for address resolution */

		/* Use UI frame */
		return (*iface->output)(iface,hw_addr,iface->hwaddr,PID_IP,bp);

}
/* Add header and send connectionless (UI) AX.25 packet.
 * Note that the calling order here must match enet_output
 * since ARP also uses it.
 */
int
ax_output(iface,dest,source,pid,bp)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;             /* Destination AX.25 address (7 bytes, shifted) */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
int16 pid;              /* Protocol ID */
struct mbuf *bp;        /* Data field (follows PID) */
{
	/* Prepend pid to data */
	bp = pushdown(bp,1);
	bp->data[0] = (char)pid;
	return axsend(iface,dest,source,LAPB_COMMAND,UI,bp);
}
/* Common subroutine for sendframe() and ax_output() */
static int
axsend(iface,dest,source,cmdrsp,ctl,bp)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;             /* Destination AX.25 address (7 bytes, shifted) */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
int cmdrsp;             /* Command/response indication */
int ctl;                /* Control field */
struct mbuf *bp;        /* Data field (includes PID) */
{
	struct ax25 addr;
	int rval;
	struct mbuf *data;
	struct iface *ifp;

	/* If the source addr is unspecified, use the interface address */
	if(source[0] == '\0')
		source = iface->hwaddr;

	/* Do AX.25 routing */
	memcpy(addr.dest,dest,AXALEN);
	memcpy(addr.source,source,AXALEN);
	addr.ndigis = 0;
	axroute(&addr, &ifp);

	addr.cmdrsp = cmdrsp;

	/* Allocate mbuf for control field, and fill in */
	bp = pushdown(bp,1);
	bp->data[0] = ctl;

	if((data = htonax25(&addr,bp)) == NULLBUF){
		free_p(bp);
		return -1;
	}
	/* This shouldn't be necessary because redirection has already been
	 * done at the IP router layer, but just to be safe...
	 */
	if(iface->forw != NULLIF){
		rval = (*iface->forw->raw)(iface->forw,data);
	} else {
		rval = (*iface->raw)(iface,data);
	}
	return rval;
}

/* Process incoming AX.25 packets.
 * After optional tracing, the address field is examined. If it is
 * directed to us as a digipeater, repeat it.  If it is addressed to
 * us or to QST-0, kick it upstairs depending on the protocol ID.
 */
void
ax_recv(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct mbuf *hbp;
	char control;
	struct ax25 hdr;
	char (*mpp)[AXALEN];
	int mcast;
	char *isrc,*idest;      /* "immediate" source and destination */

	/* Pull header off packet and convert to host structure */
	if(ntohax25(&hdr,&bp) < 0){
		/* Something wrong with the header */
		iface->ax25errors++;
		free_p(bp);
		return;
	}
#if 0
	/* If there were digis in this packet and at least one has
	 * been passed, then the last passed digi is the immediate source.
	 * Otherwise it is the original source.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != 0)
		isrc = hdr.digis[hdr.nextdigi-1];
	else
		isrc = hdr.source;
#endif

	/* If there are digis in this packet and not all have been passed,
	 * then the immediate destination is the next digi. Otherwise it
	 * is the final destination.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != hdr.ndigis)
		idest = hdr.digis[hdr.nextdigi];
	else
		idest = hdr.dest;

#if 0
	/* Don't log our own packets if we overhear them, as they're
	 * already logged by axsend() and by the digipeater code.
	 */
	if(!addreq(isrc,iface->hwaddr)){
		logsrc(iface,isrc);
		logdest(iface,idest);
	}
#endif
	/* Examine immediate destination for a multicast address */
	mcast = 0;
	for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
		if(addreq(idest,*mpp)){
			mcast = 1;
			break;
		}
	}
	if(!mcast && !addreq(idest,iface->hwaddr)){
		/* Not a broadcast, and not addressed to us. Inhibit
		 * transmitter to avoid colliding with addressed station's
		 * response, and discard packet.
		 */
#if 0
		if(iface->ioctl != NULL)
			(*iface->ioctl)(iface,PARAM_MUTE,1,-1);
#endif
		free_p(bp);
		return;
	}
#if 0
	if(!mcast && iface->ioctl != NULL){
		/* Packet was sent to us; abort transmit inhibit */
		(*iface->ioctl)(iface,PARAM_MUTE,1,0);
	}
#endif
	/* At this point, packet is either addressed to us, or is
	 * a multicast.
	 */
	axroute_add(iface, &hdr, 0);
	if(hdr.nextdigi < hdr.ndigis){
		/* Packet requests digipeating. See if we can repeat it. */
		if(Digipeat && !mcast){
			/* Yes, kick it back out. htonax25 will set the
			 * repeated bit.
			 */
			hdr.nextdigi++;
			if(Digipeat == 1 ||
			   (bp && (*bp->data & ~PF) == UI) ||
			   addreq(hdr.source, hdr.dest) ||
			   is_flexnet(hdr.source, 0) && is_flexnet(hdr.dest, 0)) {
				struct iface *ifp;
				axroute(&hdr, &ifp);
				if((hbp = htonax25(&hdr,bp)) != NULLBUF){
					bp = hbp;
					if (ifp) {
						(*ifp->raw)(ifp, bp);
						bp = NULLBUF;
					}
				}
			} else {
				lapb_input(iface,&hdr,bp);
				bp = NULLBUF;
			}
		}
		free_p(bp);     /* Dispose if not forwarded */
		return;
	}
	/* If we reach this point, then the packet has passed all digis,
	 * and is either addressed to us or is a multicast.
	 */
	if(bp == NULLBUF)
		return;         /* Nothing left */

	/* Sneak a peek at the control field. This kludge is necessary because
	 * AX.25 lacks a proper protocol ID field between the address and LAPB
	 * sublayers; a control value of UI indicates that LAPB is to be
	 * bypassed.
	 */
	control = *bp->data & ~PF;

	if(uchar(control) == UI){
		int pid;
		struct axlink *ipp;

		(void) PULLCHAR(&bp);
		if((pid = PULLCHAR(&bp)) == -1)
			return;         /* No PID */
		/* Find network level protocol and hand it off */
		for(ipp = Axlink;ipp->funct        ;ipp++){
			if(ipp->pid == pid)
				break;
		}
		if(ipp->funct        )
			(*ipp->funct)(iface,NULLAX25,hdr.source,hdr.dest,bp,mcast);
		else
			free_p(bp);
		return;
	}
	/* Everything from here down is connected-mode LAPB, so ignore
	 * multicasts
	 */
	if(mcast){
		free_p(bp);
		return;
	}

	lapb_input(iface,&hdr,bp);
}

int
valid_remote_call(call)
const char *call;
{
  char (*mpp)[AXALEN];

  if (!*call || ismyax25addr(call)) return 0;
  for (mpp = Ax25multi; (*mpp)[0]; mpp++)
    if (addreq(*mpp, call)) return 0;
  return 1;
}

static int
axroute_hash(call)
const char *call;
{
  int hashval;

  hashval  = ((*call++ << 23) & 0x0f000000);
  hashval |= ((*call++ << 19) & 0x00f00000);
  hashval |= ((*call++ << 15) & 0x000f0000);
  hashval |= ((*call++ << 11) & 0x0000f000);
  hashval |= ((*call++ <<  7) & 0x00000f00);
  hashval |= ((*call++ <<  3) & 0x000000f0);
  hashval |= ((*call   >>  1) & 0x0000000f);
  return hashval;
}

int
is_flexnet(call, store)
const char *call;
int store;
{

#define FTABLESIZE 23

  struct ftable_t {
    struct ftable_t *next;
    char call[AXALEN];
  };

  static struct ftable_t *ftable[FTABLESIZE];
  struct ftable_t **tp;
  struct ftable_t *p;

  tp = ftable + (axroute_hash(call) % FTABLESIZE);
  for (p = *tp; p && !addreq(p->call, call); p = p->next) ;
  if (!p && store) {
    p = malloc(sizeof(*p));
    addrcp(p->call, call);
    p->next = *tp;
    *tp = p;
  }
  return (int) (p != 0);
}

struct ax_route *
ax_routeptr(call, create)
const char *call;
int create;
{

  struct ax_route **tp;
  struct ax_route *rp;

  tp = Ax_routes + (axroute_hash(call) % AXROUTESIZE);
  for (rp = *tp; rp && !addreq(rp->target, call); rp = rp->next) ;
  if (!rp && create) {
    rp = calloc(1, sizeof(*rp));
    addrcp(rp->target, call);
    rp->next = *tp;
    *tp = rp;
  }
  return rp;
}

void
axroute_add(iface, hdr, perm)
struct iface *iface;
struct ax25 *hdr;
int perm;
{

  char *call;
  char *calls[MAXDIGIS+1];
  int i;
  int j;
  int ncalls = 0;
  struct ax_route *lastnode = 0;
  struct ax_route *rp;

  call = hdr->source;
  if (!valid_remote_call(call)) return;
  calls[ncalls++] = call;
  for (i = 0; i < hdr->nextdigi; i++) {
    call = hdr->digis[i];
    if (!valid_remote_call(call)) return;
    for (j = 0; j < ncalls; j++)
      if (addreq(call, calls[j])) return;
    calls[ncalls++] = call;
  }

  for (i = ncalls - 1; i >= 0; i--) {
    rp = ax_routeptr(calls[i], 1);
    if (perm || !rp->perm) {
      if (lastnode) {
	rp->digi = lastnode;
	rp->ifp = 0;
      } else {
	rp->digi = 0;
	rp->ifp = iface;
      }
      rp->perm = perm;
    }
    rp->time = secclock();
    lastnode = rp;
  }
  axroute_savefile();
}

void
axroute(hdr, ifpp)
struct ax25 *hdr;
struct iface **ifpp;
{

  char *idest;
  int d;
  int i;
  struct ax_route *rp;
  struct iface *ifp;

  /*** Find my last address ***/

  hdr->nextdigi = 0;
  for (i = hdr->ndigis - 1; i >= 0; i--)
    if (ismyax25addr(hdr->digis[i])) {
      hdr->nextdigi = i + 1;
      break;
    }

  /*** Remove all digipeaters before me ***/

  d = hdr->nextdigi - 1;
  if (d > 0) {
    for (i = d; i < hdr->ndigis; i++) addrcp(hdr->digis[i-d], hdr->digis[i]);
    hdr->ndigis -= d;
    hdr->nextdigi = 1;
  }

  /*** Add necessary digipeaters and find interface ***/

  ifp = 0;
  idest = hdr->nextdigi < hdr->ndigis ? hdr->digis[hdr->nextdigi] : hdr->dest;
  for (rp = ax_routeptr(idest, 0); rp; rp = rp->digi) {
    if (rp->digi && hdr->ndigis < MAXDIGIS) {
      for (i = hdr->ndigis - 1; i >= hdr->nextdigi; i--)
	addrcp(hdr->digis[i+1], hdr->digis[i]);
      hdr->ndigis++;
      addrcp(hdr->digis[hdr->nextdigi], rp->digi->target);
    }
    ifp = rp->ifp;
  }
  if (!ifp) ifp = Axroute_default_ifp;
  if (ifp && ifp->forw) ifp = ifp->forw;
  *ifpp = ifp;

  /*** Replace my address with hwaddr of interface ***/

  if (ifp)
    addrcp(hdr->nextdigi ? hdr->digis[0] : hdr->source, ifp->hwaddr);
}

/* Handle ordinary incoming data (no network protocol) */
void
axnl3(iface,axp,src,dest,bp,mcast)
struct iface *iface;
struct ax25_cb *axp;
char *src;
char *dest;
struct mbuf *bp;
int mcast;
{
	free_p(bp);
}
