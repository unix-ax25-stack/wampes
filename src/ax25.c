/* @(#) $Id: ax25.c,v 1.40 2006/02/12 17:49:57 dl9sau Exp $ */

/* Low level AX.25 code:
 *  incoming frame processing (including digipeating)
 *  IP encapsulation
 *  digipeater routing
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include <ctype.h>
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
#include "lapb.h"
#include "pidfilter.h"

/* List of AX.25 multicast addresses in network format (shifted ascii).
 * Only the first entry is used for transmission, but an incoming
 * packet with any one of these destination addresses is recognized
 * as a multicast.
 */
uint8 Ax25multi[][AXALEN] = {
	{ 'Q'<<1, 'S'<<1, 'T'<<1, ' '<<1, ' '<<1, ' '<<1, '0'<<1 }, /* QST */
	{ 'N'<<1, 'O'<<1, 'D'<<1, 'E'<<1, 'S'<<1, ' '<<1, '0'<<1 }, /* NODES */
	{ '\0' }
};
uint8 Mycall[AXALEN] = {
	'N'<<1, 'O'<<1, 'C'<<1, 'A'<<1, 'L'<<1, 'L'<<1, '0'<<1  /* NOCALL */
};
struct ax_route *Ax_routes[AXROUTESIZE];
struct iface *Axroute_default_ifp;
int Digipeat = 2;       /* Controls digipeating */

/*---------------------------------------------------------------------------*/

int
axi_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	return axui_send(bpp,iface,gateway,tos);
}

/* Send IP datagrams across an AX.25 link */
int
axui_send(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	struct mbuf *tbp;
	uint8 *hw_addr;
	struct ax25_cb *axp;
	uint8 pid;
#ifdef	AX25_VJCOMP
        int type;
#endif

	if((hw_addr = res_arp(iface,ARP_AX25,gateway,bpp)) == NULL)
		return 0;       /* Wait for address resolution */

	/* UI frames are used for any one of the following three conditions:
	 * 1. The "low delay" bit is set in the type-of-service field.
	 * 2. The "reliability" TOS bit is NOT set and the interface is in
	 *    datagram mode.
	 * 3. The destination is the broadcast address (this is helpful
	 *    when broadcasting on an interface that's in connected mode).
	 * If Axigntos is set, TOS is ignored.
	 */
	if(Axigntos)
		tos = 0;
	if((tos & IP_COS) == LDELAY
	 || ((tos & IP_COS) != RELIABILITY && (iface->send == axui_send))
	 || addreq(hw_addr,Ax25multi[0])){
		/* Use UI frame */
		return (*iface->output)(iface,hw_addr,iface->hwaddr,PID_IP,bpp);
	}
	/* Reliability is needed; use I-frames in AX.25 connection */
	/* THE PORT'S OWN CALLSIGN, not any link that happens to reach him.
	 * Two lines above, the UI branch already says it - iface->hwaddr - and
	 * the connected branch has to say the same: a datagram and a
	 * connection over one port come from one station.
	 *
	 * Riding on a link that already carries a text session to the same
	 * partner is still wanted and still happens, as long as it is OUR
	 * port's link: otherwise IP to him would stop for the length of that
	 * session.  What must not happen is taking a link that somebody opened
	 * under a different ssid - from the linux side, say.  The far end
	 * answers to what it hears, so it would relearn its ARP and look for
	 * us under a callsign this route knows nothing about.
	 */
	if((axp = find_ax25(iface->hwaddr, hw_addr)) == NULL){
		/* Open a new connection */
		struct ax25 hdr;
		struct ax25_opts opts;

		memset(&hdr,0,sizeof(struct ax25));
		memset(&opts,0,sizeof(opts));
		addrcp(hdr.dest,hw_addr);
		/* Over THIS port: the routing table would pick one of its own,
		 * and here the port is not a guess - the datagram arrived for
		 * it.  ax25_resolve_path() then stamps its callsign as the
		 * source, which is the pair we just looked for.
		 */
		opts.iface = iface;
		axp = open_ax25(&hdr,AX_ACTIVE,&opts);
		if(axp == NULL){
			free_p(bpp);
			return -1;
		}
		// xnet compatibily patch by dl9sau:
		// discard ax25 PID=text CText for compatibilty with xnet
		(void)open_axservice(axp,PID_NO_L3,
		 axserv_recv_upcall_discard,0,0,0);
	}
	if(axp->state == LAPB_DISCONNECTED){
		est_link(axp);
		lapbstate(axp,LAPB_SETUP);
	}
#ifdef	AX25_VJCOMP
	/* MW: TCP compression stuff */
        if (axp->slcomp == NULL)
            axp->slcomp = slhc_init(32, 32);

        /* Attempt compression */
        if (axp->slcomp_enable)
            type = axhc_compress(axp->slcomp, bpp, TRUE);
        else
            type = axhc_compress(axp->slcomp, bpp, FALSE);

        switch (type) {
        case SL_TYPE_IP:
            pid = PID_IP;
            break;
            
        case SL_TYPE_UNCOMPRESSED_TCP:
            pid = PID_VJUNCOMP;
            break;
            
        case SL_TYPE_COMPRESSED_TCP:
            pid = PID_VJCOMP;
            break;
            
        case SL_TYPE_ERROR:
            free_p(bpp);
            return -1;

        default:
            free_p(bpp);
            printf("vj: oops! Unhandled case in ax25.c\n");
            return -1;
        }
#else
	pid = PID_IP;
#endif
	/* Insert the PID */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = pid;
	if((tbp = segmenter(bpp,axp->paclen)) == NULL){
		free_p(bpp);
		return -1;
	}
	return send_ax25(axp,&tbp,-1);
}

#ifdef	AX25_VJCOMP
/* MW: learning ARPs'n'Routes */
static void
learn(
struct iface *ifp,
uint8 *hwaddr,
struct mbuf **bpp,
int mcast)
{
    int32 ipaddr = 0;
    struct arp_tab *ap;
    
    if (mcast || !bpp || !*bpp)
        return;

    ipaddr = get32((*bpp)->data + 12);

    /* NEVER ONE OF OURS.  Nothing stops a datagram carrying our own address
     * as its source - reflected somewhere, looped, or sent by a station that
     * holds it - and learning from it puts our own address into the table
     * pointing at whoever handed us the frame.  On db0fhn that read
     *
     *     44.130.60.101      AX.25       2119932   IGATE
     *
     * with .101 being the node itself, and traffic for it then went out over
     * the air instead of to us.  A restart cured it, which is how it was
     * found: the configuration was right all along.
     *
     * The generic learner in ip_route() has asked this all along
     * (iproute.c:102); this one had not.
     */
    if (ismyaddr(ipaddr) != NULL)
        return;

#if 1
    ap = arp_lookup(ARP_AX25, ipaddr);
    if (ap == NULL || 
        (ap != NULL && ap->state == ARP_VALID && run_timer(&ap->timer)))
    {
        arp_add(ipaddr, ARP_AX25, hwaddr, 0);
    }
#else
    /* look for a valid resolution for hwaddr */
    ap = revarp_lookup(ARP_AX25, hwaddr);
    if (ap != NULL && ap->state == ARP_VALID) {
        /* if we have a resolution but ip addresses dont match,
         * add a route. Don't ever overwrite ARPs, only
         * refresh them, but not if the timer isn't running.
         */
        if (ap->ip_addr != ipaddr) {
            rt_add(ipaddr, 32, ap->ip_addr, ifp, 1L, 0x7fffffff/1000, 0);
        } else {
            rt_drop(ipaddr, 32); 
            if (run_timer(&ap->timer)) {
                arp_add(ap->ip_addr, ARP_AX25, hwaddr, 0);
            }
        }
    } else {
        /* we have no (valid) resolution for hwaddr.
         * now there are 2 possibilities and we add
         * an ARP entry in both of them:
         * 1. the ip address is a new one (or the resolution
         *    is invalid)
         * 2. the hwaddr for a given ip address has
         *    changed.
         * You will find that both cases are equal, so
         * we simply call arp_add here without distinguishing
         * between them.
         */
        rt_drop(ipaddr, 32);
        arp_add(ipaddr, ARP_AX25, hwaddr, 0);
    }
#endif
}

/* MW: handle incoming VJ compressed packets */
void
ax_rx_vjcomp(
struct iface *ifp,
struct ax25_cb *axp,
uint8 *ax_src,
uint8 *ax_dest,
struct mbuf **bpp,
int mcast)
{
        axp->slcomp_enable = 1;
        /* MW: check if we already have initialized slots,
         * if not, it's too late. flush the frame.
         */
        if (axp->slcomp == NULL) {
                axp->slcomp = slhc_init(32, 32);
                free_p(bpp);
                return;
        }
        if (slhc_uncompress(axp->slcomp, bpp) <= 0) {
                free_p(bpp);
                return;
        }
        learn(ifp, ax_src, bpp, mcast); 
        ip_route(ifp, bpp, 0);
}

void 
ax_rx_vjuncomp(
struct iface *ifp,
struct ax25_cb *axp,
uint8 *ax_src,
uint8 *ax_dest,
struct mbuf **bpp,
int mcast)
{
        axp->slcomp_enable = 1;

        /* MW: check if we already have initialized slots,
         * do so if not.
         */
        if (axp->slcomp == NULL) {
                axp->slcomp = slhc_init(32, 32);
        }
        if (slhc_remember(axp->slcomp, bpp) <= 0) {
                free_p(bpp);
                return;
        }
        learn(ifp, ax_src, bpp, mcast); 
        ip_route(ifp, bpp, 0);
}

extern int axhc_remember(struct slcompress *, struct mbuf **);

void ax_rx_ip(
struct iface *ifp,
struct ax25_cb *axp,
uint8 *ax_src,
uint8 *ax_dest,
struct mbuf **bpp,
int mcast)
{
/*
    if (axp->slcomp == NULL) {
        axp->slcomp = slhc_init(32, 32);
    }
    if (axhc_remember(axp->slcomp, bpp) <= 0) {
        free_p(bpp);
        return;
    }
*/
    learn(ifp, ax_src, bpp, mcast);
    ip_route(ifp, bpp, 0);
}
#endif

/* Send a UI frame with the header exactly as given: this source, this
 * destination, this path, out of this interface.
 *
 * ax_output() cannot do it.  Its comment promises "Interface to use;
 * overrides routing table" and that holds for the interface - the one axroute
 * picks is computed and thrown away - but the path is rewritten all the same,
 * because axsend() calls axroute() and that appends digipeaters from the
 * route file.  A frame handed to us with WIDE1-1,WIDE2-1 would go out with
 * our own routing mixed in, which is nonsense on the air.  Here we are the
 * originator, not a digipeater: what was given is what is sent.
 *
 * That also lifts the one-digipeater limit, which lives in axsend()'s ax_via
 * parameter and was only ever meant for the ARP multicast case.
 */

int
ax_send_ui(
struct iface *iface,
struct ax25 *hdr,
int pid,
struct mbuf **bpp
){
	uint8 *idest;

	if(iface == NULL || iface->raw == NULL){
		free_p(bpp);
		return -1;
	}
	if(pid_blocked(iface,PF_OUT,pid)){
		free_p(bpp);
		return -1;
	}
	if(hdr->source[0] == '\0')
		addrcp(hdr->source,iface->hwaddr);

	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = (uint8) pid;
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = UI;
	htonax25(hdr,bpp);

	idest = (hdr->ndigis != 0 && hdr->nextdigi != hdr->ndigis) ?
		hdr->digis[hdr->nextdigi] : hdr->dest;
	if(iface->forw != NULL){
		logsrc(iface->forw,iface->forw->hwaddr);
		logdest(iface->forw,idest);
		return (*iface->forw->raw)(iface->forw,bpp);
	}
	logsrc(iface,iface->hwaddr);
	logdest(iface,idest);
	return (*iface->raw)(iface,bpp);
}

/* Add header and send connectionless (UI) AX.25 packet.
 * Note that the calling order here must match enet_output
 * since ARP also uses it.
 */
int
ax_output(
struct iface *iface,    /* Interface to use; overrides routing table */
uint8 *dest,            /* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,          /* Source AX.25 address (7 bytes, shifted) */
uint pid,               /* Protocol ID */
struct mbuf **bpp       /* Data field (follows PID) */
){
	if(pid_blocked(iface,PF_OUT,(int) pid)){
		free_p(bpp);
		return -1;
	}
	/* Prepend pid to data */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = (uint8)pid;

	// dl9sau: patch for ARP requests (to QST-0 or directly to a digipeater)
	// for an extended "collision domain"
	if (iface && iface->iftype && addreq(dest,Ax25multi[0])) {
		uint8 **ax_via;
		struct mbuf *tbp;
		int i;
		int ret = 0;
		int no_direct_arp = 0;
		for (i = 0, ax_via = iface->iftype->ax_mcast_digis; i < AX_MCAST_DIGIS_MAX && *ax_via; ax_via++, i++) {
			if (!ax_via[0][0]) {
			  no_direct_arp = 1;
			  continue;
			}
			dup_p(&tbp,*bpp,0,len_p(*bpp));
			if (ax_via[0][AXALEN-1] & 0x01) {
			  uint8 digi[AXALEN];
			  addrcp(digi, ax_via[0]);
			  // delete marker - just 2b sure
			  digi[AXALEN-1] &= ~0x01;
			  ret = axsend(iface,dest,source,LAPB_COMMAND,UI,&tbp, digi);
			} else {
			  ret = axsend(iface,*ax_via,source,LAPB_COMMAND,UI,&tbp, 0);
			}
		}
		if (no_direct_arp)
		  return ret;
	}
	return axsend(iface,dest,source,LAPB_COMMAND,UI,bpp, 0);
}
/* Common subroutine for sendframe() and ax_output() */
int
axsend(
struct iface *iface,    /* Interface to use; overrides routing table */
uint8 *dest,            /* Destination AX.25 address (7 bytes, shifted) */
uint8 *source,          /* Source AX.25 address (7 bytes, shifted) */
enum lapb_cmdrsp cmdrsp,/* Command/response indication */
int ctl,                /* Control field */
struct mbuf **bpp,      /* Data field (includes PID) */
uint8 *ax_via           /* forced via, for multicast (QST-0 ARP) via digipeater */ 
){
	struct ax25 addr;
	struct iface *ifp;
	uint8 *idest;
	int rval;

	/* If the source addr is unspecified, use the interface address */
	if(source[0] == '\0')
		source = iface->hwaddr;

	/* Do AX.25 routing */
	memset(&addr,0,sizeof(struct ax25));
	memcpy(addr.source,source,AXALEN);
	memcpy(addr.dest,dest,AXALEN);
	if (ax_via) {
	  memcpy(addr.digis[0], ax_via, AXALEN);
	  addr.ndigis = 1;
	}
	axroute(&addr, &ifp);
	addr.cmdrsp = cmdrsp;

	if(addr.ndigis != 0 && addr.nextdigi != addr.ndigis){
		idest = addr.digis[addr.nextdigi];
	} else {
		idest = dest;
	}

	/* Allocate mbuf for control field, and fill in */
	pushdown(bpp,NULL,1);
	(*bpp)->data[0] = ctl;

	htonax25(&addr,bpp);
	/* This shouldn't be necessary because redirection has already been
	 * done at the IP router layer, but just to be safe...
	 */
	if(iface->forw != NULL){
		logsrc(iface->forw,iface->forw->hwaddr);
		logdest(iface->forw,idest);
		rval = (*iface->forw->raw)(iface->forw,bpp);
	} else {
		logsrc(iface,iface->hwaddr);
		logdest(iface,idest);
		rval = (*iface->raw)(iface,bpp);
	}
	return rval;
}
/* Does this interface answer to that address?  Three sources, and they have
 * to be one answer: the interface's own callsign; a callsign one of its links
 * was opened under, because a connect made with "< CALL" gets its reply
 * addressed to that and nothing else would recognise it; and a callsign the
 * node was told to listen for, which has to come from configuration because
 * the first frame of an incoming connection arrives before there is any
 * control block to look it up in.
 *
 * The control blocks are the list for the second kind - nothing to register
 * and nothing to clean up, since a callsign goes when its link does.
 *
 * NOT the legs of a relayed connection, and this cost a working digipeat
 * path for four days.  When someone reaches a station THROUGH us, WAMPES
 * terminates both halves and carries HIS callsign onward as the source of
 * the second one (axroute(), and it is what makes the far end see "him via
 * us").  We do not answer to that callsign, we speak in his name - so
 * counting it here told lapb_input() that the answer coming back was
 * addressed to us, whereupon find_ax25() would not look at relay legs
 * either and the frame fell into a fresh control block and was dropped.
 * The same test that find_ax25() uses keeps them apart: a link of our own
 * has no peer.
 *
 * ax_recv() asks this to decide whether to process a frame and ax_forus()
 * to decide whether to show it.  They used to ask separately and only the
 * first was taught the other two sources, so an operator could watch a
 * working link and see nothing come in.
 */
int
ax_answers_to(
struct iface *iface,
const uint8 *addr
){
	struct ax25_cb *axp;

	if(addreq(addr,iface->hwaddr))
		return 1;
	for(axp = Ax25_cb; axp != NULL; axp = axp->next)
		if(axp->peer == NULL && axp->iface == iface &&
		   addreq(axp->hdr.source,addr))
			return 1;
	return axlisten_active(addr);
}

/* May this frame teach us a route?  Everything may, except the one kind that
 * is only noise: a broadcast with no network layer above it.  That is APRS
 * and the plain beacons, whose paths are generic - WIDE1-1 and WIDE2-2 lead
 * nowhere - and as an APRS digipeater we would fill the table within hours.
 *
 * A list of what may not, rather than of what may.  The other way round was
 * tried and was wrong: it silently dropped NET/ROM neighbour discovery, and
 * an IP-over-AX.25 exchange digipeated through us teaches a path that has
 * just been proven to work.
 */
static int
learn_from(
struct mbuf *bp
){
	int i;
	int n = 0;
	int pid = -1;

	if (bp == NULL)
		return 0;
	if ((*bp->data & ~PF) != UI)
		return 1;               /* connected mode */

	/* The PID follows the control field, and the frame may be split
	 * across mbufs, so walk rather than index.
	 */
	for (; bp && pid < 0; bp = bp->next)
		for (i = 0; i < bp->cnt; i++)
			if (n++ == 1) {
				pid = bp->data[i];
				break;
			}
	return pid != PID_NO_L3;
}

/* Process incoming AX.25 packets.
 * After optional tracing, the address field is examined. If it is
 * directed to us as a digipeater, repeat it.  If it is addressed to
 * us or to QST-0, kick it upstairs depending on the protocol ID.
 */
void
ax_recv(
struct iface *iface,
struct mbuf **bpp
){
	uint8 control;
	struct ax25 hdr;
	uint8 (*mpp)[AXALEN];
	int mcast;
	uint8 *isrc,*idest;     /* "immediate" source and destination */

	/* Pull header off packet and convert to host structure */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		iface->ax25errors++;
		free_p(bpp);
		return;
	}
	/* If there were digis in this packet and at least one has
	 * been passed, then the last passed digi is the immediate source.
	 * Otherwise it is the original source.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != 0)
		isrc = hdr.digis[hdr.nextdigi-1];
	else
		isrc = hdr.source;

	/* If there are digis in this packet and not all have been passed,
	 * then the immediate destination is the next digi. Otherwise it
	 * is the final destination.
	 */
	if(hdr.ndigis != 0 && hdr.nextdigi != hdr.ndigis)
		idest = hdr.digis[hdr.nextdigi];
	else
		idest = hdr.dest;

	/* Don't log our own packets if we overhear them, as they're
	 * already logged by axsend() and by the digipeater code.
	 */
	if(!addreq(isrc,iface->hwaddr)){
		logsrc(iface,isrc);
		logdest(iface,idest);
	}
	/* Examine immediate destination for a multicast address */
	mcast = 0;
	for(mpp = Ax25multi;(*mpp)[0] != '\0';mpp++){
		if(addreq(idest,*mpp)){
			mcast = 1;
			break;
		}
	}
	if(!mcast && !ax_answers_to(iface,idest)){
		/* Not a broadcast, and not for any callsign we answer to. */
		free_p(bpp);
		return;
	}
	/* At this point, packet is either addressed to us, or is
	 * a multicast.
	 */
	/* Not from every broadcast - see learn_from() above. */
	if(learn_from(*bpp))
		axroute_add(iface, &hdr, 0);
	if(hdr.nextdigi < hdr.ndigis){
		/* Packet requests digipeating. See if we can repeat it. */
		if(Digipeat && !mcast){
			/* Yes, kick it back out. htonax25 will set the
			 * repeated bit.
			 */
			hdr.nextdigi++;
			if(Digipeat == 1 ||
			   (*bpp && (*(*bpp)->data & ~PF) == UI) ||
			   addreq(hdr.source, hdr.dest)) {
				struct iface *ifp;
				axroute(&hdr, &ifp);
				htonax25(&hdr,bpp);
				if (ifp) {
					logsrc(ifp,ifp->hwaddr);
					logdest(ifp,hdr.nextdigi != hdr.ndigis ? hdr.digis[hdr.nextdigi] : hdr.dest);
					(*ifp->raw)(ifp, bpp);
				}
			} else {
				lapb_input(iface,&hdr,bpp);
			}
		}
		free_p(bpp);    /* Dispose if not forwarded */
		return;
	}
	/* If we reach this point, then the packet has passed all digis,
	 * and is either addressed to us or is a multicast.
	 */
	if(*bpp == NULL)
		return;         /* Nothing left */

	/* Sneak a peek at the control field. This kludge is necessary because
	 * AX.25 lacks a proper protocol ID field between the address and LAPB
	 * sublayers; a control value of UI indicates that LAPB is to be
	 * bypassed.
	 */
	control = *(*bpp)->data & ~PF;

	if(control == UI){
		int pid;
		struct axlink *ipp;

		(void) PULLCHAR(bpp);
		if((pid = PULLCHAR(bpp)) == -1)
			return;         /* No PID */
		/* Does this protocol get in here at all?  Above everything
		 * else, clients included: a port gate is about the port.  We
		 * are past digipeating at this point, so what is merely
		 * repeated through us never comes here - see pidfilter.c.
		 */
		if(pid_blocked(iface,PF_IN,pid)){
			free_p(bpp);
			return;
		}
		/* A datagram client that asked for this callsign and pid
		 * comes first - the node's own protocols keep everything
		 * nobody claimed.  A port callsign can never be claimed this
		 * way; axlisten_drop_local() sees to that when the port takes
		 * its name.
		 */
		if(axlisten_ui_deliver(iface,&hdr,pid,bpp))
			return;
		/* Find network level protocol and hand it off */
		for(ipp = Axlink;ipp->funct != NULL;ipp++){
			if(ipp->pid == pid)
				break;
		}
		if(ipp->funct != NULL)
			(*ipp->funct)(iface,NULL,hdr.source,hdr.dest,bpp,mcast);
		else
			free_p(bpp);
		return;
	}
	/* Everything from here down is connected-mode LAPB, so ignore
	 * multicasts
	 */
	if(mcast){
		free_p(bpp);
		return;
	}

	lapb_input(iface,&hdr,bpp);
}

int
valid_remote_call(
const uint8 *call)
{
	uint8 (*mpp)[AXALEN];

	if (!*call || ismyax25addr(call))
		return 0;
	for (mpp = Ax25multi; (*mpp)[0]; mpp++)
		if (addreq(*mpp, call))
			return 0;
	return 1;
}

static int
axroute_hash(
const uint8 *call)
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

struct ax_route *
ax_routeptr(
const uint8 *call,
int create)
{

	struct ax_route **tp;
	struct ax_route *rp;

	tp = Ax_routes + (axroute_hash(call) % AXROUTESIZE);
	for (rp = *tp; rp && !addreq(rp->target, call); rp = rp->next)
		;
	if (!rp && create) {
		/* The rest of the tree uses callocw(); this one dereferenced
		 * the result of calloc() straight away.
		 */
		rp = (struct ax_route *) callocw(1, sizeof(struct ax_route));
		addrcp(rp->target, call);
		rp->next = *tp;
		*tp = rp;
	}
	return rp;
}

void
axroute_add(
struct iface *iface,
struct ax25 *hdr,
int perm)
{

	uint8 *call;
	uint8 *calls[MAXDIGIS+1];
	int i;
	int j;
	int ncalls = 0;
	struct ax_route *lastnode = 0;
	struct ax_route *rp;

	call = hdr->source;
	if (!valid_remote_call(call))
		return;
	calls[ncalls++] = call;
	for (i = 0; i < hdr->nextdigi; i++) {
		call = hdr->digis[i];
		if (!valid_remote_call(call))
			return;
		for (j = 0; j < ncalls; j++)
			if (addreq(call, calls[j]))
				return;
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
	axroute_savefile(NULL);
}

void
axroute(
struct ax25 *hdr,
struct iface **ifpp)
{
	int depth;

	uint8 *idest;
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
		for (i = d; i < hdr->ndigis; i++)
			addrcp(hdr->digis[i-d], hdr->digis[i]);
		hdr->ndigis -= d;
		hdr->nextdigi = 1;
	}

	/*** Add necessary digipeaters and find interface ***/

	ifp = 0;
	idest = hdr->nextdigi < hdr->ndigis ? hdr->digis[hdr->nextdigi] : hdr->dest;
	/* A digipeater chain comes from the route file or from "ax route add",
	 * and nothing there stops it from pointing back at itself.  It can
	 * never usefully be longer than the header can hold anyway.
	 */
	for (rp = ax_routeptr(idest, 0), depth = 0; rp && depth <= MAXDIGIS;
	     rp = rp->digi, depth++) {
		if (rp->digi && hdr->ndigis < MAXDIGIS) {
			for (i = hdr->ndigis - 1; i >= hdr->nextdigi; i--)
				addrcp(hdr->digis[i+1], hdr->digis[i]);
			hdr->ndigis++;
			addrcp(hdr->digis[hdr->nextdigi], rp->digi->target);
		}
		ifp = rp->ifp;
	}
	if (!ifp)
		ifp = Axroute_default_ifp;
	if (ifp && ifp->forw)
		ifp = ifp->forw;
	*ifpp = ifp;

	/*** Replace my address with hwaddr of interface ***/

	if (ifp)
		addrcp(hdr->nextdigi ? hdr->digis[0] : hdr->source, ifp->hwaddr);
}
