/* Low level AX.25 frame processing - address header */

#include <memory.h>
#include <stdio.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"

/* AX.25 broadcast address: "QST-0" in shifted ascii */
struct ax25_addr ax25_bdcst = {
	'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1,
	('0'<<1) | E,
};
/* NET/ROM broadcast address: "NODES-0" in shifted ascii */
static struct ax25_addr nr_bdcst = {
	'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1,
	('0'<<1) | E
};
struct ax25_addr mycall;
int digipeat;           /* Controls digipeating */

/* Send IP datagrams across an AX.25 link */
int
ax_send(bp,interface,gateway,precedence,delay,throughput,reliability)
struct mbuf *bp;
struct interface *interface;
int32 gateway;
char precedence;
char delay;
char throughput;
char reliability;
{
	char *hw_addr,*res_arp();

	if((hw_addr = res_arp(interface,ARP_AX25,gateway,bp)) == NULLCHAR)
		return 0;       /* Wait for address resolution */

		/* Use UI frame */
		return (*interface->output)(interface,hw_addr,
			interface->hwaddr,PID_FIRST|PID_LAST|PID_IP,bp);

}
/* Add AX.25 link header and send packet.
 * Note that the calling order here must match ec_output
 * since ARP also uses it.
 */
int
ax_output(interface,dest,source,pid,data)
struct interface *interface;
char *dest;             /* Destination AX.25 address (7 bytes, shifted) */
			/* Also includes digipeater string */
char *source;           /* Source AX.25 address (7 bytes, shifted) */
char pid;               /* Protocol ID */
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
	if(interface->forw != NULLIF)
		return (*interface->forw->raw)(interface->forw,abp);
	else
		return (*interface->raw)(interface,abp);
}

/*---------------------------------------------------------------------------*/

int  ax_recv(interface, bp)
struct interface *interface;
struct mbuf *bp;
{

  char  axheader[10*AXALEN+1];
  char  pid;
  int  addrsize;
  int  multicast;
  register char  *ap;
  register char  *cntrlptr;

  if (!(bp && bp->data)) goto discard;
  for (cntrlptr = bp->data; !(*cntrlptr++ & E); ) ;
  addrsize = cntrlptr - bp->data;
  if (addrsize <  2 * AXALEN || addrsize >= bp->cnt ||
      addrsize > 10 * AXALEN || addrsize % AXALEN) goto discard;
  if (!idigi(interface, bp)) return;
  for (ap = bp->data + 2 * AXALEN; ap < cntrlptr; ap += AXALEN)
    if (!(ap[6] & REPEATED)) {
      if (!ismycall(axptr(ap))) goto discard;
      ap[6] |= REPEATED;
      switch (digipeat) {
      case 1:
	axroute(NULL, bp);
	return;
      case 2:
	axproto_recv(interface, bp);
	return;
      default:
	goto discard;
      }
    }

  if ((*cntrlptr & ~PF) != UI) {
    if (!ismycall(axptr(bp->data))) goto discard;
    axproto_recv(interface, bp);
    return;
  }

  if (ismycall(axptr(bp->data)))
    multicast = 0;
  else if (addreq(axptr(bp->data), &ax25_bdcst))
    multicast = 1;
  else if (addreq(axptr(bp->data), &nr_bdcst))
    multicast = 1;
  else
    goto discard;

  pullup(&bp, axheader, addrsize + 1);
  if (pullup(&bp, &pid, 1) != 1 || !bp) return;

  switch (pid & (PID_FIRST | PID_LAST | PID_PID)) {
  case (PID_IP | PID_FIRST | PID_LAST):
    if (bp->cnt >= 20) {

      char  hw_addr[10*AXALEN];
      int32 src_ipaddr;
      register char  *fp, *tp;
      struct arp_tab *arp, *arp_add();

      src_ipaddr = (uchar(bp->data[12]) << 24) |
		   (uchar(bp->data[13]) << 16) |
		   (uchar(bp->data[14]) <<  8) |
		    uchar(bp->data[15]);
      rt_add(src_ipaddr, 32, 0, 0, interface);
      tp = hw_addr;
      memcpy(tp, axheader + AXALEN, AXALEN);
      tp[6] = (tp[6] & SSID) | 0x60;
      tp += AXALEN;
      fp = axheader + addrsize - AXALEN;
      while (fp > axheader + AXALEN) {
	memcpy(tp, fp, AXALEN);
	tp[6] = (tp[6] & SSID) | 0x60;
	fp -= AXALEN;
	tp += AXALEN;
      }
      tp[-1] |= E;
      if (arp = arp_add(src_ipaddr, ARP_AX25, hw_addr, addrsize - AXALEN, 0)) {
	stop_timer(&arp->timer);
	arp->timer.count = arp->timer.start = 0;
      }
    }
    ip_route(bp, multicast);
    return;
  case (PID_ARP | PID_FIRST | PID_LAST):
    arp_input(interface, bp);
    return;
  case (PID_NETROM | PID_FIRST | PID_LAST):
    nr_nodercv(axptr(axheader + AXALEN), bp);
    return;
  default:
    goto discard;
  }

discard:
  free_p(bp);
}

/*---------------------------------------------------------------------------*/

/* Initialize AX.25 entry in arp device table */
axarp()
{
	int psax25(),setpath();

	arp_init(ARP_AX25,AXALEN,PID_FIRST|PID_LAST|PID_IP,
	 PID_FIRST|PID_LAST|PID_ARP,(char *)&ax25_bdcst,psax25,setpath);
}

