/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.c,v 1.11 1991/12/04 18:25:28 deyke Exp $ */

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
#include <ctype.h>

static int axsend __ARGS((struct iface *iface,char *dest,char *source,
	int cmdrsp,int ctl,struct mbuf *data));
static int axroute_hash __ARGS((char *call));

/* AX.25 broadcast address: "QST-0" in shifted ascii */
char Ax25_bdcst[AXALEN] = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1,
};
char Mycall[AXALEN];
struct ax_route *Ax_routes[AXROUTESIZE];
struct iface *axroute_default_ifp;
int Digipeat = 2;       /* Controls digipeating */

/* Send IP datagrams across an AX.25 link */
int
ax_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int prec;
int del;
int tput;
int rel;
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
ax_output(iface,dest,source,pid,data)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;             /* Destination AX.25 address (7 bytes, shifted) */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
int16 pid;              /* Protocol ID */
struct mbuf *data;      /* Data field (follows PID) */
{
	struct mbuf *bp;

	/* Prepend pid to data */
	bp = pushdown(data,1);
	if(bp == NULLBUF){
		free_p(data);
		return -1;
	}
	bp->data[0] = (char)pid;
	return axsend(iface,dest,source,LAPB_COMMAND,UI,bp);
}
/* Common subroutine for sendframe() and ax_output() */
static int
axsend(iface,dest,source,cmdrsp,ctl,data)
struct iface *iface;    /* Interface to use; overrides routing table */
char *dest;             /* Destination AX.25 address (7 bytes, shifted) */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
int cmdrsp;             /* Command/response indication */
int ctl;                /* Control field */
struct mbuf *data;      /* Data field (includes PID) */
{
	struct mbuf *cbp;
	struct ax25 addr;
	struct ax_route *dp,*rp;
	int rval;
	int i;

	/* If the source addr is unspecified, use the interface address */
	if(source[0] == '\0')
		source = iface->hwaddr;

	memcpy(addr.dest,dest,AXALEN);
	memcpy(addr.source,source,AXALEN);
	addr.cmdrsp = cmdrsp;

	addr.ndigis = 0;
	/* If there's a digipeater route, get it */
	if (rp = ax_routeptr(dest, 0)) {
		for (dp = rp->digi; dp; dp = dp->digi)
			addr.ndigis++;
		i = addr.ndigis;
		for (dp = rp->digi; dp; dp = dp->digi) {
			i--;
			if (i < MAXDIGIS)
				addrcp(addr.digis[i], dp->call);
		}
	}

	addr.nextdigi = 0;

	/* Allocate mbuf for control field, and fill in */
	if((cbp = pushdown(data,1)) == NULLBUF){
		free_p(data);
		return -1;
	}
	cbp->data[0] = ctl;

	if((data = htonax25(&addr,cbp)) == NULLBUF){
		free_p(cbp);    /* Also frees data */
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
	char **mpp;
	int mcast;

	/* Pull header off packet and convert to host structure */
	if(ntohax25(&hdr,&bp) < 0){
		/* Something wrong with the header */
		iface->ax25errors++;
		free_p(bp);
		return;
	}
	/* If there were digis in this packet, then the last digi was the
	 * actual transmitter. Otherwise the source is the transmitter.
	 */

	if(hdr.nextdigi < hdr.ndigis){
		/* Packet hasn't passed all digipeaters yet. See if
		 * we have to repeat it.
		 */
		if(Digipeat && addreq(hdr.digis[hdr.nextdigi],iface->hwaddr)){
			/* Yes, kick it back out. htonax25 will set the
			 * repeated bit.
			 */
			axroute_add(iface, &hdr, 0);
			hdr.nextdigi++;
			switch(Digipeat){
			case 1:
				if((hbp = htonax25(&hdr,bp)) != NULLBUF){
					axroute(NULL,hbp);
					bp = NULLBUF;
				}
				break;
			case 2:
				lapb_input(iface,&hdr,bp);
				bp = NULLBUF;
				break;
			}
		}
		free_p(bp);     /* Dispose if not forwarded */
		return;
	}
	/* If we reach this point, then the packet has passed all digis,
	 * but it is not necessarily for us.
	 */
	if(bp == NULLBUF){
		/* Nothing left */
		return;
	}
	/* Examine destination to see if it's either addressed to us or
	 * a multicast.
	 */
	mcast = 0;
	for(mpp = Axmulti;*mpp != NULLCHAR;mpp++){
		if(addreq(hdr.dest,*mpp)){
			mcast = 1;
			break;
		}
	}
	if(!mcast && !addreq(hdr.dest,iface->hwaddr)){
		/* Not a broadcast, and not addressed to us; ignore */
		free_p(bp);
		return;
	}

	if(!mcast)
		axroute_add(iface, &hdr, 0);

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
			(*ipp->funct)(iface,(void *) 0,hdr.source,hdr.dest,bp,mcast);
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

/*---------------------------------------------------------------------------*/

static int  axroute_hash(call)
char  *call;
{
  long  hashval;

  hashval  = ((*call++ << 23) & 0x0f000000);
  hashval |= ((*call++ << 19) & 0x00f00000);
  hashval |= ((*call++ << 15) & 0x000f0000);
  hashval |= ((*call++ << 11) & 0x0000f000);
  hashval |= ((*call++ <<  7) & 0x00000f00);
  hashval |= ((*call++ <<  3) & 0x000000f0);
  hashval |= ((*call   >>  1) & 0x0000000f);
  return hashval % AXROUTESIZE;
}

/*---------------------------------------------------------------------------*/

struct ax_route *ax_routeptr(call, create)
char  *call;
int  create;
{

  int  hashval;
  struct ax_route *rp;

  hashval = axroute_hash(call);
  for (rp = Ax_routes[hashval]; rp && !addreq(rp->call, call); rp = rp->next) ;
  if (!rp && create) {
    rp = (struct ax_route *) calloc(1, sizeof(struct ax_route ));
    addrcp(rp->call, call);
    rp->next = Ax_routes[hashval];
    Ax_routes[hashval] = rp;
  }
  return rp;
}

/*---------------------------------------------------------------------------*/

void axroute_add(iface, hdr, perm)
struct iface *iface;
struct ax25 *hdr;
int  perm;
{

  char  **mpp;
  char  *call;
  char  *calls[MAXDIGIS+1];
  int  i;
  int  j;
  int  ncalls = 0;
  struct ax_route *lastnode = 0;
  struct ax_route *rp;

  call = hdr->source;
  if (!*call || addreq(call, iface->hwaddr)) return;
  for (mpp = Axmulti; *mpp; mpp++)
    if (addreq(call, *mpp)) return;
  calls[ncalls++] = call;
  for (i = 0; i < hdr->nextdigi; i++) {
    call = hdr->digis[i];
    if (!*call || addreq(call, iface->hwaddr)) return;
    for (mpp = Axmulti; *mpp; mpp++)
      if (addreq(call, *mpp)) return;
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

/*---------------------------------------------------------------------------*/

void axroute(cp, bp)
struct ax25_cb *cp;
struct mbuf *bp;
{

  char  *dest;
  struct ax_route *rp;
  struct iface *ifp;

  if (cp && cp->ifp)
    ifp = cp->ifp;
  else {
    if (bp->data[AXALEN + 6] & E)
      dest = bp->data;
    else
      for (dest = bp->data + 2 * AXALEN; ; dest += AXALEN) {
	if (!(dest[6] & REPEATED)) break;
	if (dest[6] & E) {
	  dest = bp->data;
	  break;
	}
      }
    rp = ax_routeptr(dest, 0);
    ifp = (rp && rp->ifp) ? rp->ifp : axroute_default_ifp;
  }
  if (ifp) {
    if (ifp->forw) ifp = ifp->forw;
    (*ifp->raw)(ifp, bp);
  } else
    free_p(bp);
}

/*---------------------------------------------------------------------------*/

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

/*---------------------------------------------------------------------------*/

char  *ax25hdr_to_string(hdr)
struct ax25 *hdr;
{

  char  *p;
  int  i;
  static char  buf[128];

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
    *p++ = ',';
    pax25(p, hdr->digis[i]);
    while (*p) p++;
    if (i < hdr->nextdigi) *p++ = '*';
  }
  *p = '\0';
  return buf;
}
