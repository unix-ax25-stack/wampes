/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25.c,v 1.5 1990/09/11 13:44:57 deyke Exp $ */

/* Low level AX.25 frame processing - address header */

#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "ip.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"

/* AX.25 broadcast address: "QST-0" in shifted ascii */
struct ax25_addr Ax25_bdcst = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1,
	('0'<<1) | E,
};
/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static struct ax25_addr nr_bdcst = {
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1,
	('0'<<1) | E
};
char Mycall[AXALEN];
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
			/* Also includes digipeater string */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
int16 pid;              /* Protocol ID */
struct mbuf *data;      /* Data field (follows PID) */
{
	struct mbuf *abp,*cbp,*htonax25();
	struct ax25 addr;

	/* Allocate mbuf for control and PID fields, and fill in */
	if((cbp = pushdown(data,2)) == NULLBUF){
		free_p(data);
		return -1;
	}
	cbp->data[0] = UI;
	cbp->data[1] = pid;

	atohax25(&addr,dest,(struct ax25_addr *)source);
	if((abp = htonax25(&addr,cbp)) == NULLBUF){
		free_p(cbp);    /* Also frees data */
		return -1;
	}
	/* This shouldn't be necessary because redirection has already been
	 * done at the IP router layer, but just to be safe...
	 */
	if(iface->forw != NULLIF)
		return (*iface->forw->raw)(iface->forw,abp);
	else
		return (*iface->raw)(iface,abp);
}

/*---------------------------------------------------------------------------*/

void
ax_recv(iface, bp)
struct iface *iface;
struct mbuf *bp;
{

  char  axheader[10*AXALEN+1];
  int  addrsize;
  int  multicast;
  int  pid;
  register char  *ap;
  register char  *cntrlptr;

  if (!(bp && bp->data)) goto discard;
  for (cntrlptr = bp->data; !(*cntrlptr++ & E); ) ;
  addrsize = cntrlptr - bp->data;
  if (addrsize <  2 * AXALEN || addrsize >= bp->cnt ||
      addrsize > 10 * AXALEN || addrsize % AXALEN) goto discard;
  if (!idigi(iface, bp)) return;
  for (ap = bp->data + 2 * AXALEN; ap < cntrlptr; ap += AXALEN)
    if (!(ap[6] & REPEATED)) {
      if (!ismycall(ap)) goto discard;
      ap[6] |= REPEATED;
      switch (Digipeat) {
      case 1:
	axroute(NULL, bp);
	return;
      case 2:
	axproto_recv(iface, bp);
	return;
      default:
	goto discard;
      }
    }

  if ((*cntrlptr & ~PF) != UI) {
    if (!ismycall(bp->data)) goto discard;
    axproto_recv(iface, bp);
    return;
  }

  if (ismycall(bp->data))
    multicast = 0;
  else if (addreq(bp->data, (char *) &Ax25_bdcst))
    multicast = 1;
  else if (addreq(bp->data, (char *) &nr_bdcst))
    multicast = 1;
  else
    goto discard;

  pullup(&bp, axheader, addrsize + 1);
  pid = PULLCHAR(&bp);
  if (!bp) return;

  switch (pid) {
  case PID_IP:
    if (bp->cnt >= 20) {

      char  hw_addr[10*AXALEN];
      int32 src_ipaddr;
      register char  *fp, *tp;
      struct arp_tab *arp;

      src_ipaddr = (uchar(bp->data[12]) << 24) |
		   (uchar(bp->data[13]) << 16) |
		   (uchar(bp->data[14]) <<  8) |
		    uchar(bp->data[15]);
      rt_add(src_ipaddr, 32, 0, iface, 1, 0, 0);
      tp = hw_addr;
      addrcp(tp, axheader + AXALEN);
      tp += AXALEN;
      fp = axheader + addrsize - AXALEN;
      while (fp > axheader + AXALEN) {
	addrcp(tp, fp);
	fp -= AXALEN;
	tp += AXALEN;
      }
      tp[-1] |= E;
      if (arp = arp_add(src_ipaddr, ARP_AX25, hw_addr, addrsize - AXALEN, 0)) {
	stop_timer(&arp->timer);
	arp->timer.count = arp->timer.start = 0;
      }
    }
    ip_route(iface, bp, multicast);
    return;
  case PID_ARP:
    arp_input(iface, bp);
    return;
  case PID_NETROM:
    nr3_input(bp, axptr(axheader + AXALEN));
    return;
  default:
    goto discard;
  }

discard:
  free_p(bp);
}

