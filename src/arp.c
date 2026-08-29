/* @(#) $Id: arp.c,v 1.20 1999/01/27 18:45:40 deyke Exp $ */

/* Address Resolution Protocol (ARP) functions. Sits between IP and
 * Level 2, mapping IP to Level 2 addresses for all outgoing datagrams.
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "iface.h"
#include "ax25.h"
#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "icmp.h"
#include "iplearn.h"

/* Ethernet type fields */
#define REVARP_TYPE     0x8035  /* Type field for reverse ARP */

static void arp_output(struct iface *iface,enum arp_hwtype hardware,int32 target);

/* Hash table headers */
struct arp_tab *Arp_tab[HASHMOD];

struct arp_stat Arp_stat;

/* Resolve an IP address to a hardware address; if not found,
 * initiate query and return NULL.  If an address is returned, the
 * interface driver may send the packet; if NULL is returned,
 * res_arp() will have saved the packet on its pending queue,
 * so no further action (like freeing the packet) is necessary.
 */
uint8 *
res_arp(
struct iface *iface,            /* Pointer to interface block */
enum arp_hwtype hardware,       /* Hardware type */
int32 target,                   /* Target IP address */
struct mbuf **bpp               /* IP datagram to be queued if unresolved */
){
	struct arp_tab *arp;
	struct ip ip;

	if((arp = arp_lookup(hardware,target)) != NULL && arp->state == ARP_VALID)
		return arp->hw_addr;
	if(arp != NULL){
		/* Earlier packets are already pending, kick this one back
		 * as a source quench
		 */
		ntohip(&ip,bpp);
		icmp_output(&ip,*bpp,ICMP_QUENCH,0,NULL);
		free_p(bpp);
	} else if(iface != NULL && iface->noarp){
		/* Auf diesem Port ist das Fragen abgeschaltet - siehe
		 * iface->noarp.  Dieselbe Antwort wie im Filterfall darunter:
		 * lieber sofort "nicht erreichbar" als ein Datagramm, das bis
		 * zum PENDING-Timer in der Queue haengt.
		 */
		ntohip(&ip,bpp);
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULL);
		free_p(bpp);
	} else if(!ip_might_learn(target,LEARN_ARP,iface)){
		/* GAR NICHT ERST FRAGEN.  Duerfen wir die Antwort nicht
		 * eintragen, dann fuehrt die Anfrage zu nichts: die Gegenseite
		 * antwortet, arp_learn() lehnt ab, und das Datagramm haengt
		 * bis zum PENDING-Timer in der Queue.  Gemessen an einer Regel
		 * "deny ... arp": Route gelernt, ARP-Anfrage hinaus, Eintrag
		 * blieb "[unknown]" mit Q 1.
		 *
		 * Die Antwort auf unsere eigene Frage bekommt dadurch KEINEN
		 * Vertrauensvorschuss - sie geht durch denselben Filter, nur
		 * dann mit dem Rufzeichen, das hier noch fehlt.  Deshalb auch
		 * ip_might_learn() und nicht ip_may_learn(..., NULL, ...).
		 */
		ntohip(&ip,bpp);
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULL);
		free_p(bpp);
	} else {
		/* Create an entry and put the datagram on the
		 * queue pending an answer
		 */
		arp = arp_add(target,hardware,NULL,0);
		enqueue(&arp->pending,bpp);
		arp_output(iface,hardware,target);
	}
	return NULL;
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
arp_input(
struct iface *iface,
struct mbuf **bpp
){
	struct arp arp;
	struct arp_tab *ap;
	struct arp_type *at;
	int i;

	Arp_stat.recv++;
	if(ntoharp(&arp,bpp) == -1)     /* Convert into host format */
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
	if(arp.hwalen > MAXHWALEN || arp.pralen != sizeof(int32)){
		/* Incorrect protocol addr length (different hw addr lengths
		 * are OK since AX.25 addresses can be of variable length)
		 */
		Arp_stat.badlen++;
		return;
	}
	if(arp.sprotaddr == 0L || arp.tprotaddr == 0L) {
		/* We are going to dead-end references for [0.0.0.0], since
		 * experience shows that these cause total lock up -- N1BEE
		 */
		Arp_stat.badaddr++;
		return;
	}
	if(memcmp(arp.shwaddr,at->bdcst,at->hwalen) == 0){
		/* This guy is trying to say he's got the broadcast address! */
		Arp_stat.badaddr++;
		return;
	}
	/* If this guy is already in the table, update its entry
	 * unless it's a manual entry (noted by the lack of a timer).
	 *
	 * Der Schutz von-Hand-gesetzter Eintraege steht jetzt in arp_learn();
	 * das lookup hier bleibt, weil ap zugleich das merge_flag der Spec ist
	 * und arp_learn() bei einem abgelehnten oder geschuetzten Eintrag nicht
	 * unterscheidbar antwortet.
	 */
	ap = NULL;      /* ap plays the role of merge_flag in the spec */
	if(arp_lookup(arp.hardware,arp.sprotaddr) != NULL){
		ap = arp_learn(arp.sprotaddr,arp.hardware,arp.shwaddr,iface);
	}
	/* See if we're the address they're looking for */
	if(ismyaddr(arp.tprotaddr) != NULL){
		if(ap == NULL)  /* Only if not already in the table */
			arp_learn(arp.sprotaddr,arp.hardware,arp.shwaddr,iface);

		if(arp.opcode == ARP_REQUEST){
			/* Swap sender's and target's (us) hardware and protocol
			 * fields, and send the packet back as a reply
			 */
			memcpy(arp.thwaddr,arp.shwaddr,(uint)arp.hwalen);
			/* Mark the end of the sender's AX.25 address
			 * in case he didn't
			 */
			if(arp.hardware == ARP_AX25)
				arp.thwaddr[arp.hwalen-1] |= E;

			memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
			arp.tprotaddr = arp.sprotaddr;
			arp.sprotaddr = iface->addr;
			arp.opcode = ARP_REPLY;
			if((*bpp = htonarp(&arp)) == NULL)
				return;

			if(iface->forw != NULL)
				(*iface->forw->output)(iface->forw,
				 arp.thwaddr,iface->forw->hwaddr,at->arptype,bpp);
			else
				(*iface->output)(iface,arp.thwaddr,
				 iface->hwaddr,at->arptype,bpp);
			Arp_stat.inreq++;
		} else {
			Arp_stat.replies++;
		}
	} else if(arp.opcode == ARP_REQUEST
	 && (ap = arp_lookup(arp.hardware,arp.tprotaddr)) != NULL
	 && ap->pub){
		/* Otherwise, respond if the guy he's looking for is
		 * published in our table.
		 */
		memcpy(arp.thwaddr,arp.shwaddr,(uint)arp.hwalen);
		memcpy(arp.shwaddr,ap->hw_addr,at->hwalen);
		arp.tprotaddr = arp.sprotaddr;
		arp.sprotaddr = ap->ip_addr;
		arp.opcode = ARP_REPLY;
		if((*bpp = htonarp(&arp)) == NULL)
			return;
		if(iface->forw != NULL)
			(*iface->forw->output)(iface->forw,
			 arp.thwaddr,iface->forw->hwaddr,at->arptype,bpp);
		else
			(*iface->output)(iface,arp.thwaddr,
			 iface->hwaddr,at->arptype,bpp);
		Arp_stat.inreq++;
	} else if(arp.opcode == REVARP_REQUEST){
		for(i=0;i<HASHMOD;i++)
			for(ap = Arp_tab[i];ap != NULL;ap = ap->next)
				if(memcmp(ap->hw_addr,arp.thwaddr,at->hwalen) == 0)
					goto found;
	found:  if(ap != NULL && ap->pub){
			memcpy(arp.shwaddr,iface->hwaddr,at->hwalen);
			arp.tprotaddr = ap->ip_addr;
			arp.sprotaddr = iface->addr;
			arp.opcode = REVARP_REPLY;
			if((*bpp = htonarp(&arp)) == NULL)
				return;
			if(iface->forw != NULL)
				(*iface->forw->output)(iface->forw,
				 arp.thwaddr,iface->forw->hwaddr,REVARP_TYPE,bpp);
			else
				(*iface->output)(iface,arp.thwaddr,
				 iface->hwaddr,REVARP_TYPE,bpp);
			Arp_stat.inreq++;
		}
	}
}
/* Ein Paar eintragen, das aus dem VERKEHR kam.
 *
 * Zwei Fragen, die vorher bei den Aufrufern standen - in ax25.c, config.c und
 * netrom.c dreimal fast gleich, und die ax25.c-Fassung wich ab (sie liess
 * einen ARP_PENDING-Eintrag nicht ueberschreiben, obwohl der GERADE auf diese
 * Antwort wartet):
 *
 *   1. Darf das gelernt werden?  Fragt den Lernfilter (ip learn).
 *   2. Steht da ein Eintrag VON HAND?  Solche haben keinen laufenden Timer -
 *      sie altern nicht und sollen von Funkverkehr nicht ueberschrieben
 *      werden.  Ein noch gueltiger, aber laufender Eintrag wird aufgefrischt.
 *
 * Bei ARP_AX25 und ARP_NETROM ist die Hardware-Adresse das Rufzeichen; nur
 * dann kann der Filter nach call= fragen.
 */
struct arp_tab *
arp_learn(
int32 ipaddr,
enum arp_hwtype hardware,
uint8 *hw_addr,
struct iface *ifp)
{
	struct arp_tab *ap;
	const uint8 *call;

	call = (hardware == ARP_AX25 || hardware == ARP_NETROM) ? hw_addr : NULL;

	if(!ip_may_learn(ipaddr,32,LEARN_ARP,call,ifp))
		return NULL;

	ap = arp_lookup(hardware,ipaddr);
	if(ap != NULL && ap->state == ARP_VALID && !run_timer(&ap->timer))
		return ap;      /* Von Hand gesetzt - stehen lassen */

	return arp_add(ipaddr,hardware,hw_addr,0);
}

/* Add an IP-addr / hardware-addr pair to the ARP table */
struct arp_tab *
arp_add(
int32 ipaddr,                   /* IP address, host order */
enum arp_hwtype hardware,       /* Hardware type */
uint8 *hw_addr,                 /* Hardware address, if known; NULL otherwise */
int pub)                        /* Publish this entry? */
{
	struct mbuf *bp;
	struct arp_tab *ap;
	struct arp_type *at;
	unsigned hashval;

	if(hardware >=NHWTYPES)
		return NULL;    /* Invalid hardware type */
	at = &Arp_type[hardware];

	if((ap = arp_lookup(hardware,ipaddr)) == NULL){
		/* New entry */
		ap = (struct arp_tab *)callocw(1,sizeof(struct arp_tab));
		ap->hw_addr = (uint8 *) mallocw(at->hwalen);
		ap->timer.func = arp_drop;
		ap->timer.arg = ap;
		ap->hardware = hardware;
		ap->ip_addr = ipaddr;

		/* Put on head of hash chain */
		hashval = hash_ip(ipaddr);
		ap->prev = NULL;
		ap->next = Arp_tab[hashval];
		Arp_tab[hashval] = ap;
		if(ap->next != NULL){
			ap->next->prev = ap;
		}
	}
	if(hw_addr == NULL){
		/* Await response */
		ap->state = ARP_PENDING;
		set_timer(&ap->timer,Arp_type[hardware].pendtime * 1000L);
	} else {
		/* Response has come in, update entry and run through queue */
		ap->state = ARP_VALID;
		set_timer(&ap->timer,ARPLIFE*1000L);
		memcpy(ap->hw_addr,hw_addr,at->hwalen);
		ap->pub = pub;
		arp_savefile(NULL);
		while((bp = dequeue(&ap->pending)) != NULL)
			ip_route(NULL,&bp,0);
	}
	start_timer(&ap->timer);
	return ap;
}

/* Remove an entry from the ARP table */
void
arp_drop(
void *p)
{
	struct arp_tab *ap;

	ap = (struct arp_tab *)p;
	if(ap == NULL)
		return;
	stop_timer(&ap->timer); /* Shouldn't be necessary */
	if(ap->next != NULL)
		ap->next->prev = ap->prev;
	if(ap->prev != NULL)
		ap->prev->next = ap->next;
	else
		Arp_tab[hash_ip(ap->ip_addr)] = ap->next;
	if(ap->pending != NULL){
		struct ip ip;
		struct mbuf *bp;
		bp = dequeue(&ap->pending);
		if(bp != NULL){
			ntohip(&ip,&bp);
			icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULL);
			free_p(&bp);
		}
	}
	free_q(&ap->pending);
	free(ap->hw_addr);
	free(ap);
}

/* Look up the given IP address in the ARP table */
struct arp_tab *
arp_lookup(
enum arp_hwtype hardware,
int32 ipaddr)
{
	struct arp_tab *ap;

	arp_loadfile();
	for(ap = Arp_tab[hash_ip(ipaddr)]; ap != NULL; ap = ap->next){
		if(ap->ip_addr == ipaddr && ap->hardware == hardware)
			break;
	}
	return ap;
}
/* Send an ARP request to resolve IP address target_ip */
static void
arp_output(
struct iface *iface,
enum arp_hwtype hardware,
int32 target)
{
	struct arp arp;
	struct mbuf *bp;
	struct arp_type *at;

	at = &Arp_type[hardware];
	if(iface->output == NULL)
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
	if((bp = htonarp(&arp)) == NULL)
		return;
	(*iface->output)(iface,at->bdcst,
		iface->hwaddr,at->arptype,&bp);
	Arp_stat.outreq++;
}

/* Look up the given hardware address in the ARP table */
struct arp_tab *
revarp_lookup(
enum arp_hwtype hardware,
uint8 *hw_addr)
{
	struct arp_tab *ap;
	int hwalen;
	int i;

	arp_loadfile();
	hwalen = Arp_type[hardware].hwalen;
	for(i=0;i<HASHMOD;i++)
		for(ap = Arp_tab[i]; ap != NULL; ap = ap->next)
			if(memcmp(ap->hw_addr,hw_addr,hwalen) == 0)
				return ap;
	return NULL;
}

