/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/udp.c,v 1.4 1991/02/24 20:17:59 deyke Exp $ */

/* Internet User Data Protocol (UDP)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "iface.h"
#include "udp.h"
#include "ip.h"
#include "internet.h"
#include "icmp.h"

static struct udp_cb *lookup_udp __ARGS((struct socket *socket));
static int16 hash_udp __ARGS((struct socket *socket));

struct mib_entry Udp_mib[] = {
	"",                     0,
	"udpInDatagrams",       0,
	"udpNoPorts",           0,
	"udpInErrors",          0,
	"udpOutDatagrams",      0,
};

/* Hash table for UDP structures */
struct udp_cb *Udps[NUDP] = { NULLUDP} ;

/* Create a UDP control block for lsocket, so that we can queue
 * incoming datagrams.
 */
struct udp_cb *
open_udp(lsocket,r_upcall)
struct socket *lsocket;
void (*r_upcall)();
{
	register struct udp_cb *up;
	int16 hval;

	if((up = lookup_udp(lsocket)) != NULLUDP)
		return up;      /* Already exists */
	up = (struct udp_cb *)callocw(1,sizeof (struct udp_cb));
	up->socket.address = lsocket->address;
	up->socket.port = lsocket->port;
	up->r_upcall = r_upcall;

	hval = hash_udp(lsocket);
	up->next = Udps[hval];
	if(up->next != NULLUDP)
		up->next->prev = up;
	Udps[hval] = up;
	return up;
}

/* Send a UDP datagram */
int
send_udp(lsocket,fsocket,tos,ttl,data,length,id,df)
struct socket *lsocket;         /* Source socket */
struct socket *fsocket;         /* Destination socket */
char tos;                       /* Type-of-service for IP */
char ttl;                       /* Time-to-live for IP */
struct mbuf *data;              /* Data field, if any */
int16 length;                   /* Length of data field */
int16 id;                       /* Optional ID field for IP */
char df;                        /* Don't Fragment flag for IP */
{
	struct mbuf *bp;
	struct pseudo_header ph;
	struct udp udp;
	int32 laddr;

	if(length != 0 && data != NULLBUF)
		trim_mbuf(&data,length);
	else
		length = len_p(data);

	length += UDPHDR;

	laddr = lsocket->address;
	if(laddr == INADDR_ANY)
		laddr = locaddr(fsocket->address);

	udp.source = lsocket->port;
	udp.dest = fsocket->port;
	udp.length = length;

	/* Create IP pseudo-header, compute checksum and send it */
	ph.length = length;
	ph.source = laddr;
	ph.dest = fsocket->address;
	ph.protocol = UDP_PTCL;

	if((bp = htonudp(&udp,data,&ph)) == NULLBUF){
		Net_error = NO_MEM;
		free_p(data);
		return 0;
	}
	udpOutDatagrams++;
	ip_send(laddr,fsocket->address,UDP_PTCL,tos,ttl,bp,length,id,df);
	return (int)length;
}
/* Accept a waiting datagram, if available. Returns length of datagram */
int
recv_udp(up,fsocket,bp)
register struct udp_cb *up;
struct socket *fsocket;         /* Place to stash incoming socket */
struct mbuf **bp;               /* Place to stash data packet */
{
	struct socket sp;
	struct mbuf *buf;
	int16 length;

	if(up == NULLUDP){
		Net_error = NO_CONN;
		return -1;
	}
	if(up->rcvcnt == 0){
		Net_error = WOULDBLK;
		return -1;
	}
	buf = dequeue(&up->rcvq);
	up->rcvcnt--;

	/* Strip socket header */
	pullup(&buf,(char *)&sp,sizeof(struct socket));

	/* Fill in the user's foreign socket structure, if given */
	if(fsocket != NULLSOCK){
		fsocket->address = sp.address;
		fsocket->port = sp.port;
	}
	/* Hand data to user */
	length = len_p(buf);
	if(bp != NULLBUFP)
		*bp = buf;
	else
		free_p(buf);
	return (int)length;
}
/* Delete a UDP control block */
int
del_udp(up)
struct udp_cb *up;
{
	struct mbuf *bp;
	int16 hval;

	if(up == NULLUDP){
		Net_error = INVALID;
		return -1;
	}
	/* Get rid of any pending packets */
	while(up->rcvcnt != 0){
		bp = up->rcvq;
		up->rcvq = up->rcvq->anext;
		free_p(bp);
		up->rcvcnt--;
	}
	hval = hash_udp(&up->socket);
	if(up->prev == NULLUDP)
		Udps[hval] = up->next;          /* First on list */
	else
		up->prev->next = up->next;
	if(up->next != NULLUDP)
		up->next->prev = up->prev;

	free((char *)up);
	return 0;
}
/* Process an incoming UDP datagram */
void
udp_input(iface,ip,bp,rxbroadcast)
struct iface *iface;    /* Input interface */
struct ip *ip;          /* IP header */
struct mbuf *bp;        /* UDP header and data */
int rxbroadcast;        /* The only protocol that accepts 'em */
{
	struct pseudo_header ph;
	struct udp udp;
	struct udp_cb *up;
	struct socket lsocket;
	struct socket fsocket;
	struct mbuf *packet;
	int ckfail = 0;
	int16 length;

	if(bp == NULLBUF)
		return;

	/* Create pseudo-header and verify checksum */
	ph.source = ip->source;
	ph.dest = ip->dest;
	ph.protocol = ip->protocol;
	length = ip->length - IPLEN - ip->optlen;
	ph.length = length;

	if(cksum(&ph,bp,length) != 0)
		/* Checksum apparently failed, note for later */
		ckfail++;

	/* Extract UDP header in host order */
	ntohudp(&udp,&bp);

	/* If the checksum field is zero, then ignore a checksum error.
	 * I think this is dangerously wrong, but it is in the spec.
	 */
	if(ckfail && udp.checksum != 0){
		udpInErrors++;
		free_p(bp);
		return;
	}
	/* If this was a broadcast packet, pretend it was sent to us */
	if(rxbroadcast){
		lsocket.address = iface->addr;
	} else
		lsocket.address = ip->dest;

	lsocket.port = udp.dest;
	/* See if there's somebody around to read it */
	if((up = lookup_udp(&lsocket)) == NULLUDP){
		/* Nope, return an ICMP message */
		if(!rxbroadcast){
			bp = htonudp(&udp,bp,&ph);
			icmp_output(ip,bp,ICMP_DEST_UNREACH,ICMP_PORT_UNREACH,NULL);
		}
		udpNoPorts++;
		free_p(bp);
		return;
	}
	/* Create space for the foreign socket info */
	if((packet = pushdown(bp,sizeof(fsocket))) == NULLBUF){
		/* No space, drop whole packet */
		free_p(bp);
		udpInErrors++;
		return;
	}
	fsocket.address = ip->source;
	fsocket.port = udp.source;
	memcpy(&packet->data[0],(char *)&fsocket,sizeof(fsocket));

	/* Queue it */
	enqueue(&up->rcvq,packet);
	up->rcvcnt++;
	udpInDatagrams++;
	if(up->r_upcall)
		(*up->r_upcall)(iface,up,up->rcvcnt);
}
/* Look up UDP socket, return control block pointer or NULLUDP if nonexistant */
static
struct udp_cb *
lookup_udp(socket)
struct socket *socket;
{
	register struct udp_cb *up;

	for(up = Udps[hash_udp(socket)];up != NULLUDP;up = up->next){
		if(socket->port == up->socket.port
		 && (socket->address == up->socket.address
		     || up->socket.address == INADDR_ANY))
			break;
	}
	return up;
}

/* Hash a UDP socket (address and port) structure */
static
int16
hash_udp(socket)
struct socket *socket;
{
	/* Hash depends only on port number, to make addr wildcarding work */
	return (int16)(socket->port % NUDP);
}

void udp_garbage(red)
int red;
{
	int i;
	struct udp_cb *udp;

	for(i=0;i<NUDP;i++){
		for(udp = Udps[i];udp != NULLUDP; udp = udp->next){
			mbuf_crunch(&udp->rcvq);
		}
	}
}

