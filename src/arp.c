/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arp.c,v 1.6 1991/02/24 20:16:25 deyke Exp $ */

/* Address Resolution Protocol (ARP) functions. Sits between IP and
 * Level 2, mapping IP to Level 2 addresses for all outgoing datagrams.
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "enet.h"
#include "ax25.h"
#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"

static void arp_output __ARGS((struct iface *iface,int hardware,int32 target));

/* Hash table headers */
struct arp_tab *Arp_tab[HASHMOD];

struct arp_stat Arp_stat;

/* Resolve an IP address to a hardware address; if not found,
 * initiate query and return NULLCHAR.  If an address is returned, the
 * interface driver may send the packet; if NULLCHAR is returned,
 * res_arp() will have saved the packet on its pending queue,
 * so no further action (like freeing the packet) is necessary.
 */
char *
res_arp(iface,hardware,target,bp)
struct iface *iface;    /* Pointer to interface block */
int16 hardware;         /* Hardware type */
int32 target;           /* Target IP address */
struct mbuf *bp;        /* IP datagram to be queued if unresolved */
{
	register struct arp_tab *arp;
	struct ip ip;

	if((arp = arp_lookup(hardware,target)) != NULLARP && arp->state == ARP_VALID)
		return arp->hw_addr;
	if(arp != NULLARP){
		/* Earlier packets are already pending, kick this one back
		 * as a source quench
		 */
		ntohip(&ip,&bp);
		icmp_output(&ip,bp,ICMP_QUENCH,0,NULL);
		free_p(bp);
	} else {
		/* Create an entry and put the datagram on the
		 * queue pending an answer
		 */
		arp = arp_add(target,hardware,NULLCHAR,0);
		enqueue(&arp->pending,bp);
		arp_output(iface,hardware,target);
	}
	return NULLCHAR;
}
/* Handle incoming ARP packets. This is almost a direct implementation of
 * the algorithm on page 5 of RFC 826, except for:
 * 1. Outgoing datagrams to unresolved addresses are kept on a queue
 *    pending a reply to our ARP request.
 * 2. The names of the fields in the ARP packet were made more mnemonic.
 * 3. Requests for IP addresses listed in our table as "published" are
 *    responded to, even if the address is not our own.
 */
void
arp_input(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct arp arp;
	struct arp_tab *ap;
	struct arp_type *at;
	int i;

	Arp_stat.recv++;
	if(ntoharp(&arp,&bp) == -1)     /* Convert into host format */
		return;
	if(arp.hardware >= NHWTYPES){
		/* Unknown hardware type, ignore */
		Arp_stat.badtype++;
		return;
	}
	at = &Arp_type[arp.hardware];
	if(arp.protocol != at->iptype){
		/* Unsupported protocol type, ignore */
		Arp_stat.badtype++;
		return;
	}
	if(uchar(arp.hwalen) > MAXHWALEN || uchar(arp.pralen) != sizeof(int32)){
		/* Incorrect protocol addr length (different hw addr lengths
		 * are OK since AX.25 addresses can be of variable length)
		 */
		Arp_stat.badlen++;
		return;
	}
	if(memcmp(arp.shwaddr,at->bdcst,at->hwalen) == 0){
		/* This guy is trying to say he's got the broadcast address! */
		Arp_stat.badaddr++;
		return;
	}
	/* If this guy is already in the table, update its entry
	 * unless it's a manual entry (noted by the lack of a timer)
	 */
	ap = NULLARP;   /* ap plays the role of merge_flag in the spec */
	if((ap = arp_lookup(arp.hardware,arp.sprotaddr)) != NULLARP
	 && dur_timer(&ap->timer) != 0){
		ap = arp_add(arp.sprotaddr,arp.hardware,arp.shwaddr,0);
	}
	/* See if we're the address they're looking for */
	if(ismyaddr(arp.tprotaddr) != NULLIF){
		if(ap == NULLARP)       /* Only if not already in the table */
			arp_add(arp.sprotaddr,arp.hardware,arp.shwaddr,0);

		if(arp.opcode == ARP_REQUEST){
			/* Swap sender's and target's (us) hardware and protocol
			 * fields, and send the packet back as a reply
			 */
			memcpy(arp.thwaddr,arp.shwaddr,(int16)uchar(arp.hwalen));
			/* Mark the end of the sender's AX.25 address
			 * in case he didn't
			 */
			if(arp.hardware == ARP_AX25)
				arp.thwaddr[uchar(arp.hwalen)-1] |= E;

			memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
			arp.tprotaddr = arp.sprotaddr;
			arp.sprotaddr = iface->addr;
			arp.opcode = ARP_REPLY;
			if((bp = htonarp(&arp)) == NULLBUF)
				return;

			if(iface->forw != NULLIF)
				(*iface->forw->output)(iface->forw,
				 arp.thwaddr,iface->forw->hwaddr,at->arptype,bp);
			else
				(*iface->output)(iface,arp.thwaddr,
				 iface->hwaddr,at->arptype,bp);
			Arp_stat.inreq++;
		} else {
			Arp_stat.replies++;
		}
	} else if(arp.opcode == ARP_REQUEST
	 && (ap = arp_lookup(arp.hardware,arp.tprotaddr)) != NULLARP
	 && ap->pub){
		/* Otherwise, respond if the guy he's looking for is
		 * published in our table.
		 */
		memcpy(arp.thwaddr,arp.shwaddr,(int16)uchar(arp.hwalen));
		memcpy(arp.shwaddr,ap->hw_addr,at->hwalen);
		arp.tprotaddr = arp.sprotaddr;
		arp.sprotaddr = ap->ip_addr;
		arp.opcode = ARP_REPLY;
		if((bp = htonarp(&arp)) == NULLBUF)
			return;
		if(iface->forw != NULLIF)
			(*iface->forw->output)(iface->forw,
			 arp.thwaddr,iface->forw->hwaddr,at->arptype,bp);
		else
			(*iface->output)(iface,arp.thwaddr,
			 iface->hwaddr,at->arptype,bp);
		Arp_stat.inreq++;
	} else if(arp.opcode == REVARP_REQUEST){
		for(i=0;i<HASHMOD;i++)
			for(ap = Arp_tab[i];ap != NULLARP;ap = ap->next)
				if(memcmp(ap->hw_addr, arp.thwaddr,at->hwalen) == 0)
					goto found;
	found:  if(ap != NULLARP && ap->pub){
			memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
			arp.tprotaddr = ap->ip_addr;
			arp.sprotaddr = iface->addr;
			arp.opcode = REVARP_REPLY;
			if((bp = htonarp(&arp)) == NULLBUF)
				return;
			if(iface->forw != NULLIF)
				(*iface->forw->output)(iface->forw,
				 arp.thwaddr,iface->forw->hwaddr,REVARP_TYPE,bp);
			else
				(*iface->output)(iface,arp.thwaddr,
				 iface->hwaddr,REVARP_TYPE,bp);
			Arp_stat.inreq++;
		}
	}
}
/* Add an IP-addr / hardware-addr pair to the ARP table */
struct arp_tab *
arp_add(ipaddr,hardware,hw_addr,pub)
int32 ipaddr;           /* IP address, host order */
int16 hardware;         /* Hardware type */
char *hw_addr;          /* Hardware address, if known; NULLCHAR otherwise */
int pub;                /* Publish this entry? */
{
	struct mbuf *bp;
	register struct arp_tab *ap;
	struct arp_type *at;
	unsigned hashval;

	if(hardware >=NHWTYPES)
		return NULLARP; /* Invalid hardware type */
	at = &Arp_type[hardware];

	if((ap = arp_lookup(hardware,ipaddr)) == NULLARP){
		/* New entry */
		ap = (struct arp_tab *)callocw(1,sizeof(struct arp_tab));
		ap->hw_addr = mallocw(at->hwalen);
		ap->timer.func = arp_drop;
		ap->timer.arg = ap;
		ap->hardware = hardware;
		ap->ip_addr = ipaddr;

		/* Put on head of hash chain */
		hashval = hash_ip(ipaddr);
		ap->prev = NULLARP;
		ap->next = Arp_tab[hashval];
		Arp_tab[hashval] = ap;
		if(ap->next != NULLARP){
			ap->next->prev = ap;
		}
	}
	if(hw_addr == NULLCHAR){
		/* Await response */
		ap->state = ARP_PENDING;
		set_timer(&ap->timer,Arp_type[hardware].pendtime * 1000L);
	} else {
		/* Response has come in, update entry and run through queue */
		ap->state = ARP_VALID;
		set_timer(&ap->timer,ARPLIFE*1000L);
		memcpy(ap->hw_addr,hw_addr,at->hwalen);
		ap->pub = pub;
		arp_savefile();
		while((bp = dequeue(&ap->pending)) != NULLBUF)
			ip_route(NULLIF,bp,0);
	}
	start_timer(&ap->timer);
	return ap;
}

/* Remove an entry from the ARP table */
void
arp_drop(p)
void *p;
{
	register struct arp_tab *ap;

	ap = (struct arp_tab *)p;
	if(ap == NULLARP)
		return;
	stop_timer(&ap->timer); /* Shouldn't be necessary */
	if(ap->next != NULLARP)
		ap->next->prev = ap->prev;
	if(ap->prev != NULLARP)
		ap->prev->next = ap->next;
	else
		Arp_tab[hash_ip(ap->ip_addr)] = ap->next;
	free_q(&ap->pending);
	free(ap->hw_addr);
	free((char *)ap);
}

/* Look up the given IP address in the ARP table */
struct arp_tab *
arp_lookup(hardware,ipaddr)
int16 hardware;
int32 ipaddr;
{
	register struct arp_tab *ap;

	arp_loadfile();
	for(ap = Arp_tab[hash_ip(ipaddr)]; ap != NULLARP; ap = ap->next){
		if(ap->ip_addr == ipaddr && ap->hardware == hardware)
			break;
	}
	return ap;
}
/* Send an ARP request to resolve IP address target_ip */
static void
arp_output(iface,hardware,target)
struct iface *iface;
int16 hardware;
int32 target;
{
	struct arp arp;
	struct mbuf *bp;
	struct arp_type *at;

	at = &Arp_type[hardware];
	if(iface->output == NULLFP)
		return;

	arp.hardware = hardware;
	arp.protocol = at->iptype;
	arp.hwalen = at->hwalen;
	arp.pralen = sizeof(int32);
	arp.opcode = ARP_REQUEST;
	memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
	arp.sprotaddr = iface->addr;
	memset(arp.thwaddr,0,at->hwalen);
	arp.tprotaddr = target;
	if((bp = htonarp(&arp)) == NULLBUF)
		return;
	(*iface->output)(iface,at->bdcst,
		iface->hwaddr,at->arptype,bp);
	Arp_stat.outreq++;
}

