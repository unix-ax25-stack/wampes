/* @(#) $Id: iproute.c,v 1.40 1999/04/25 16:56:41 deyke Exp $ */

/* Lower half of IP, consisting of gateway routines
 * Includes routing and options processing code
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "timer.h"
#include "internet.h"
#include "ip.h"
#include "tcp.h"
#include "netuser.h"
#include "icmp.h"
#include "rip.h"
#include "trace.h"
#include "ipfilter.h"
#include "iplearn.h"

struct route *Routes[32][HASHMOD];      /* Routing table */
struct route R_default = {              /* Default route entry */
	NULL, NULL,
	0,0,0,
	RIP_INFINITY            /* Init metric to infinity */
};

static struct rt_cache Rt_cache[HASHMOD];
int32 Rtlookups;
int32 Rtchits;

static int q_pkt(struct iface *iface,int32 gateway,struct ip *ip,
	struct mbuf **bpp,int ckgood);

/* Route an IP datagram. This is the "hopper" through which all IP datagrams,
 * coming or going, must pass.
 *
 * "rxbroadcast" is set to indicate that the packet came in on a subnet
 * broadcast. The router will kick the packet upstairs regardless of the
 * IP destination address.
 */
int
ip_route(
struct iface *i_iface,  /* Input interface */
struct mbuf **bpp,      /* Input packet */
int rxbroadcast         /* True if packet had link broadcast address */
){
	struct ip ip;                   /* IP header being processed */
	uint ip_len;                    /* IP header length */
	uint length;                    /* Length of data portion */
	int32 gateway;                  /* Gateway IP address */
	struct route *rp;       /* Route table entry */
	struct iface *iface;            /* Output interface, possibly forwarded */
	uint offset;                    /* Starting offset of current datagram */
	uint mf_flag;                   /* Original datagram MF flag */
	int strict = 0;                 /* Strict source routing flag */
	uint opt_len = 0;       /* Length of current option */
	uint8 *opt;             /* -> beginning of current option */
	int i;
	int ckgood = IP_CS_OLD; /* Has good checksum without modification */
	int pointer;            /* Relative pointer index for sroute/rroute */

	if(i_iface != NULL){
		ipInReceives++; /* Not locally generated */
		i_iface->iprecvcnt++;
	}
	if(len_p(*bpp) < IPLEN){
		/* The packet is shorter than a legal IP header */
		ipInHdrErrors++;
		free_p(bpp);
		return -1;
	}
	/* Sneak a peek at the IP header's IHL field to find its length */
	ip_len = ((*bpp)->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		/* The IP header length field is too small */
		ipInHdrErrors++;
		free_p(bpp);
		return -1;
	}
	if(cksum(NULL,*bpp,ip_len) != 0){
		/* Bad IP header checksum; discard */
		ipInHdrErrors++;
		free_p(bpp);
		return -1;
	}
	/* Extract IP header */
	ntohip(&ip,bpp);

	if(ip.version != IPVERSION){
		/* We can't handle this version of IP */
		ipInHdrErrors++;
		free_p(bpp);
		return -1;
	}

	if(!ipfilter(ip.source)||!ipfilter(ip.dest)){
		free_p(bpp);
		return -1;
	}

	/* Der allgemeine Lerner: kein Rufzeichen, denn auf dieser Ebene ist die
	 * Layer-2-Quelle nicht mehr da - nur der Port.  Regeln mit call= gehen
	 * hier also ins Leere, Regeln mit iface= greifen.
	 */
	if(if_learns_routes(i_iface) && ismyaddr(ip.source) == NULL)
		rt_learn(ip.source, 32, 0L, i_iface, 1L, 0x7fffffff / 1000, NULL);

	/* Process options, if any. Also compute length of secondary IP
	 * header in case fragmentation is needed later
	 */
	strict = 0;
	for(i=0;i<ip.optlen;i += opt_len){
		/* First check for the two special 1-byte options */
		switch(ip.options[i] & OPT_NUMBER){
		case IP_EOL:
			goto no_opt;    /* End of options list, we're done */
		case IP_NOOP:
			opt_len = 1;
			continue;       /* No operation, skip to next option */
		}
		/* Not a 1-byte option, so ensure that there's at least
		 * two bytes of option left, that the option length is
		 * at least two, and that there's enough space left for
		 * the specified option length.
		 */
		if(ip.optlen - i < 2
		 || ((opt_len = ip.options[i+1]) < 2)
		 || ip.optlen - i < opt_len){
			/* Truncated option, send ICMP and drop packet */
			if(!rxbroadcast){
				union icmp_args icmp_args;

				icmp_args.pointer = IPLEN + i;
				icmp_output(&ip,*bpp,ICMP_PARAM_PROB,0,&icmp_args);
			}
			free_p(bpp);
			return -1;
		}
		opt = &ip.options[i];

		switch(opt[0] & OPT_NUMBER){
		case IP_SSROUTE:        /* Strict source route & record route */
			strict = 1;     /* note fall-thru */
		case IP_LSROUTE:        /* Loose source route & record route */
			/* Source routes are ignored unless we're in the
			 * destination field
			 */
			if(opt_len < 3){
				/* Option is too short to be a legal sroute.
				 * Send an ICMP message and drop it.
				 */
				if(!rxbroadcast){
					union icmp_args icmp_args;

					icmp_args.pointer = IPLEN + i;
					icmp_output(&ip,*bpp,ICMP_PARAM_PROB,0,&icmp_args);
				}
				free_p(bpp);
				return -1;
			}
			if(ismyaddr(ip.dest) == NULL)
				break;  /* Skip to next option */
			pointer = opt[2];
			if(pointer + 4 > opt_len)
				break;  /* Route exhausted; it's for us */

			/* Put address for next hop into destination field,
			 * put our address into the route field, and bump
			 * the pointer. We've already ensured enough space.
			 */
			ip.dest = get32(&opt[pointer-1]);
			put32(&opt[pointer-1],locaddr(ip.dest));
			opt[2] += 4;
			ckgood = IP_CS_NEW;
			break;
		case IP_RROUTE: /* Record route */
			if(opt_len < 3){
				/* Option is too short to be a legal rroute.
				 * Send an ICMP message and drop it.
				 */
				if(!rxbroadcast){
					union icmp_args icmp_args;

					icmp_args.pointer = IPLEN + i;
					icmp_output(&ip,*bpp,ICMP_PARAM_PROB,0,&icmp_args);
				}
				free_p(bpp);
				return -1;
			}
			pointer = opt[2];
			if(pointer + 4 > opt_len){
				/* Route area exhausted; send an ICMP msg */
				if(!rxbroadcast){
					union icmp_args icmp_args;

					icmp_args.pointer = IPLEN + i;
					icmp_output(&ip,*bpp,ICMP_PARAM_PROB,0,&icmp_args);
				}
				/* Also drop if odd-sized */
				if(pointer != opt_len){
					free_p(bpp);
					return -1;
				}
			} else {
				/* Add our address to the route.
				 * We've already ensured there's enough space.
				 */
				put32(&opt[pointer-1],locaddr(ip.dest));
				opt[2] += 4;
				ckgood = IP_CS_NEW;
			}
			break;
		}
	}
no_opt:

	/* See if it's a broadcast or addressed to us, and kick it upstairs */
	if(ismyaddr(ip.dest) != NULL || rxbroadcast){
		ip_recv(i_iface,&ip,bpp,rxbroadcast,0);
		return 0;
	}
	/* Packet is not destined to us. If it originated elsewhere, count
	 * it as a forwarded datagram.
	 */
	if(i_iface != NULL)
		ipForwDatagrams++;

	/* Adjust the header checksum to allow for the modified TTL */
	ip.checksum += 0x100;
	if((ip.checksum & 0xff00) == 0)
		ip.checksum++;  /* end-around carry */

	/* Decrement TTL and discard if zero. We don't have to check
	 * rxbroadcast here because it's already been checked
	 */
	if(--ip.ttl == 0){
		/* Send ICMP "Time Exceeded" message */
		icmp_output(&ip,*bpp,ICMP_TIME_EXCEED,0,NULL);
		ipInHdrErrors++;
		free_p(bpp);
		return -1;
	}
	/* Look up target address in routing table */
	if((rp = rt_lookup(ip.dest)) == NULL){
		/* No route exists, return unreachable message (we already
		 * know this can't be a broadcast)
		 */
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULL);
		free_p(bpp);
		ipOutNoRoutes++;
		return -1;
	}
	rp->uses++;

	/* Check for output forwarding and divert if necessary */
	iface = rp->iface;
	if(iface->forw != NULL)
		iface = iface->forw;

	/* Find gateway; zero gateway in routing table means "send direct" */
	if(rp->gateway == 0)
		gateway = ip.dest;
	else
		gateway = rp->gateway;

	if(strict && gateway != ip.dest){
		/* Strict source routing requires a direct entry
		 * Again, we know this isn't a broadcast
		 */
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_ROUTE_FAIL,NULL);
		free_p(bpp);
		ipOutNoRoutes++;
		return -1;
	}
	if(ip.length <= iface->mtu){
		/* Datagram smaller than interface MTU; put header
		 * back on and send normally.
		 */
		return q_pkt(iface,gateway,&ip,bpp,ckgood);
	}
	/* Fragmentation needed */
	if(ip.flags.df){
		/* Don't Fragment set; return ICMP message and drop */
		union icmp_args icmp_args;

		icmp_args.mtu = iface->mtu;
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,&icmp_args);
		free_p(bpp);
		ipFragFails++;
		return -1;
	}
	/* Create fragments */
	offset = ip.offset;             /* Remember starting offset */
	mf_flag = ip.flags.mf;          /* Save original MF flag */
	length = ip.length - ip_len;    /* Length of data portion */
	while(length != 0){             /* As long as there's data left */
		uint fragsize;          /* Size of this fragment's data */
		struct mbuf *f_data;    /* Data portion of fragment */

		/* After the first fragment, should remove those
		 * options that aren't supposed to be copied on fragmentation
		 */
		if(length + ip_len <= iface->mtu){
			/* Last fragment; send all that remains */
			fragsize = length;
			ip.flags.mf = mf_flag;  /* Pass original MF flag */
		} else {
			/* More to come, so send multiple of 8 bytes */
			fragsize = (iface->mtu - ip_len) & 0xfff8;
			ip.flags.mf = 1;
		}
		ip.length = fragsize + ip_len;

		/* Duplicate the fragment */
		dup_p(&f_data,*bpp,ip.offset-offset,fragsize);
		if(f_data == NULL){
			free_p(bpp);
			ipFragFails++;
			return -1;
		}
		if(q_pkt(iface,gateway,&ip,&f_data,IP_CS_NEW) == -1){
			free_p(bpp);
			ipFragFails++;
			return -1;
		}
		ipFragCreates++;
		ip.offset += fragsize;
		length -= fragsize;
	}
	ipFragOKs++;
	free_p(bpp);
	return 0;
}
/* Direct IP input routine for packets without link-level header */
void
ip_proc(
struct iface *iface,
struct mbuf **bpp
){
	ip_route(iface,bpp,0);
}

/* Add an IP datagram to an interface output queue, sorting first by
 * the precedence field in the IP header, and secondarily by an
 * "interactive" flag set by peeking at the transport layer to see
 * if the packet belongs to what appears to be an interactive session.
 * A layer violation, yes, but a useful one...
 */
static int
q_pkt(
struct iface *iface,
int32 gateway,
struct ip *ip,
struct mbuf **bpp,
int ckgood
){
	iface->ipsndcnt++;
	htonip(ip,bpp,ckgood);

	if(iface->send){
		(*iface->send)(bpp,iface,gateway,ip->tos & 0xfc);
	} else {
		free_p(bpp);
		return -1;
	}
	return 0;
}
int
ip_encap(
struct mbuf **bpp,
struct iface *iface,
int32 gateway,
uint8 tos
){
	struct ip ip;

	dump(iface,IF_TRACE_OUT,*bpp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();
	if(gateway == 0L){
		/* Gateway must be specified */
		ntohip(&ip,bpp);
		icmp_output(&ip,*bpp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULL);
		free_p(bpp);
		ipOutNoRoutes++;
		return -1;
	}
	/* Encapsulate in an IP packet from us to the gateway.
	 * The outer source address is taken from the encap interface
	 * structure. This defaults to INADDR_ANY, so unless it is
	 * changed (with iface encap ipaddr ...), the IP address
	 * of the physical interface used to reach the encap gateway
	 * will be used.
	 */
	return ip_send(Encap.addr,gateway,IP4_PTCL,tos,0,bpp,0,0,0);
}
/* Eine gelernte Route eintragen - fuer alles, was aus dem Verkehr kommt.
 *
 * Der Unterschied zu rt_add() ist der Lernfilter, und der braucht zwei
 * Angaben, die rt_add() nicht hat: WER die Adresse beansprucht (call) und
 * ueber welchen Port sie hereinkam (steckt schon in iface).  Deshalb zwei
 * Funktionen statt eines Schalters - die Angaben stehen dort, wo sie
 * anfallen, und die Aufrufer von Hand muessen nichts mitgeben, was sie gar
 * nicht wissen.
 *
 * private gibt es hier nicht: Gelerntes ist nie privat.
 */
struct route *
rt_learn(
int32 target,
unsigned int bits,
int32 gateway,
struct iface *iface,
int32 metric,
int32 ttl,
const uint8 *call       /* Wer die Adresse beansprucht, NULL wo unbekannt */
){
	if(bits > 32)
		bits = 32;

	/* ERST MASKIEREN, DANN FRAGEN.  Eine INP3-Ankuendigung nennt eine
	 * HOSTadresse mit Praefixlaenge - "ich bin .129, dahinter das /25" -,
	 * und rt_add() traegt daraus 44.130.60.128/25 ein.  Der Filter muss
	 * ueber dasselbe entscheiden, das nachher in der Tabelle steht, sonst
	 * beurteilt er eine Adresse, die es dort nie gibt.  Fuers Austragen
	 * gilt dasselbe: rt_drop() maskiert ebenfalls, es findet den Eintrag
	 * also auch dann, wenn man ihm wieder die .129 reicht - aber nur mit
	 * DERSELBEN Praefixlaenge.
	 */
	if(bits)
		target &= ~0U << (32-bits);
	else
		target = 0;

	if(!ip_may_learn(target,bits,LEARN_ROUTE,call,iface))
		return NULL;

	return rt_add(target,bits,gateway,iface,metric,ttl,0);
}

/* Add an entry to the IP routing table. Returns 0 on success, -1 on failure */
struct route *
rt_add(
int32 target,           /* Target IP address prefix */
unsigned int bits,      /* Size of target address prefix in bits (0-32) */
int32 gateway,          /* Optional gateway to be reached via interface */
struct iface *iface,    /* Interface to which packet is to be routed */
int32 metric,           /* Metric for this route entry */
int32 ttl,              /* Lifetime of this route entry in sec */
uint8 private           /* Inhibit advertising this entry ? */
){
	struct route *rp,**hp;
	int i;

	if(iface == NULL)
		return NULL;

	if(bits > 32)
		bits = 32;              /* Bulletproofing */

	if(bits == 32 && ismyaddr(target))
		return NULL;    /* Don't accept routes to ourselves */

	/* Hier stand "if(ttl && !ipfilter(target)) return NULL" - eine
	 * Sperrliste fuer ADRESSEN, die zugleich das LERNEN gatterte, erkannt
	 * an einer laufenden Lebensdauer.  Beides ist jetzt getrennt: ipfilter
	 * entscheidet ueber die Zustellung (ip_route()), das Lernen entscheidet
	 * der Lernfilter in rt_learn().  VERHALTENSAENDERUNG fuer bestehende
	 * Installationen: wer mit ipfilter Routen fernhalten wollte, braucht
	 * jetzt eine Regel "ip learn deny <praefix>" dazu.
	 */

	/* Mask off don't-care bits of target */
	if(bits)
		target &= ~0U << (32-bits);
	else
		target = 0;

	/* Encapsulated routes must specify gateway, and it can't be
	 *  ourselves
	 */
	if(iface == &Encap && (gateway == 0 || ismyaddr(gateway)))
		return NULL;

	/* Zero bits refers to the default route */
	if(bits == 0){
		rp = &R_default;
	} else {
		rp = rt_blookup(target,bits);
	}
	if(rp == NULL){
		/* The target is not already in the table, so create a new
		 * entry and put it in.
		 */
		rp = (struct route *)callocw(1,sizeof(struct route));
		/* Insert at head of table */
		rp->prev = NULL;
		hp = &Routes[bits-1][hash_ip(target)];
		rp->next = *hp;
		if(rp->next != NULL)
			rp->next->prev = rp;
		*hp = rp;
		rp->uses = 0;
	}else{
		/* Don't let temporary routes overwrite permanent routes */
		if(ttl>0 && rp->iface != NULL && !run_timer(&rp->timer))
			return NULL;
	}
	if(rp->target != target || rp->gateway != gateway || rp->iface != iface)
		for(i=0;i<HASHMOD;i++)
			Rt_cache[i].route = NULL;  /* Flush cache */
	rp->target = target;
	rp->bits = bits;
	rp->gateway = gateway;
	rp->metric = metric;
	rp->iface = iface;
	rp->flags.rtprivate = private;  /* Should anyone be told of this route? */
	rp->timer.func = rt_timeout;  /* Set the timer field */
	rp->timer.arg = (void *)rp;
	if (ttl <= 0L) {
		set_timer(&rp->timer, 0L);
	} else if (ttl >= MAXINT32 / 1000L) {
		set_timer(&rp->timer, MAXINT32);
	} else {
		set_timer(&rp->timer, ttl * 1000L);
	}
	stop_timer(&rp->timer);
	start_timer(&rp->timer); /* start the timer if appropriate */

	route_savefile(NULL);
	return rp;
}

/* Remove an entry from the IP routing table. Returns 0 on success, -1
 * if entry was not in table.
 */
int
rt_drop(
int32 target,
unsigned int bits
){
	struct route *rp;
	int i;

	for(i=0;i<HASHMOD;i++)
		Rt_cache[i].route = NULL;       /* Flush the cache */

	if(bits == 0){
		/* Nail the default entry */
		stop_timer(&R_default.timer);
		R_default.iface = NULL;
		return 0;
	}
	if(bits > 32)
		bits = 32;

	/* Mask off target according to width */
	if(bits)
		target &= ~0U << (32-bits);
	else
		target = 0;

	/* Search appropriate chain for existing entry */
	for(rp = Routes[bits-1][hash_ip(target)];rp != NULL;rp = rp->next){
		if(rp->target == target)
			break;
	}
	if(rp == NULL)
		return -1;      /* Not in table */

	stop_timer(&rp->timer);
	if(rp->next != NULL)
		rp->next->prev = rp->prev;
	if(rp->prev != NULL)
		rp->prev->next = rp->next;
	else
		Routes[bits-1][hash_ip(target)] = rp->next;

	free(rp);
	return 0;
}

/* Compute hash function on IP address */
uint
hash_ip(
int32 addr
){
	uint ret;

	ret = hiword(addr);
	ret ^= loword(addr);
	return ret % HASHMOD;
}
/* Given an IP address, return the MTU of the local interface used to
 * reach that destination. This is used by TCP to avoid local fragmentation
 */
uint
ip_mtu(
int32 addr
){
	struct route *rp;
	struct iface *iface;

	rp = rt_lookup(addr);
	if(rp == NULL || rp->iface == NULL)
		return 0;
	if(rp->iface == &Encap){
		/* Recurse to the actual hardware interface */
		return ip_mtu(rp->gateway) - IPLEN;     /* no options? */
	}
	iface = rp->iface;
	if(iface->forw != NULL)
		return iface->forw->mtu;
	else
		return iface->mtu;
}
/* Given a destination address, return the IP address of the local
 * interface that will be used to reach it. If there is no route
 * to the destination, pick the first non-loopback address.
 */
int32
locaddr(
int32 addr)
{
	struct route *rp;
	struct iface *ifp;

	if(ismyaddr(addr) != NULL)
		return addr;    /* Loopback case */

	if((rp = rt_lookup(addr)) != NULL)
		ifp = rp->iface;
	else
		ifp = NULL;

	if(ifp == &Encap){
		if((rp = rt_lookup(rp->gateway)) != NULL)
			ifp = rp->iface;
		else
			ifp = NULL;
	}
	if(ifp == NULL){
		/* No route currently exists, so just pick the first real
		 * interface and use its address
		 */
		for(ifp = Ifaces;ifp != NULL;ifp = ifp->next){
			if(ifp != &Loopback && ifp != &Encap)
				break;
		}
	}
	if(ifp == NULL || ifp == &Loopback)
		return 0;       /* No dice */

	if(ifp->forw != NULL)
		return ifp->forw->addr;
	else
		return ifp->addr;
}
/* Look up target in hash table, matching the entry having the largest number
 * of leading bits in common. Return default route if not found;
 * if default route not set, return NULL
 */
struct route *
rt_lookup(
int32 target)
{
	struct route *rp;
	int bits;
	int32 tsave;
	int32 mask;
	struct rt_cache *rcp;

	Rtlookups++;
	/* Examine cache first */
	rcp = &Rt_cache[hash_ip(target)];
	if(target == rcp->target && (rp = rcp->route) != NULL){
		Rtchits++;
		return rp;
	}
	tsave = target;

	mask = ~0;      /* All ones */
	for(bits = 31;bits >= 0; bits--){
		target &= mask;
		for(rp = Routes[bits][hash_ip(target)];rp != NULL;rp = rp->next){
			if(rp->target != target
			 || (rp->iface == &Encap && rp->gateway == tsave))
				continue;
			/* Stash in cache and return */
			rcp->target = tsave;
			rcp->route = rp;
			return rp;
		}
		mask <<= 1;
	}
	if(R_default.iface != NULL){
		rcp->target = tsave;
		rcp->route = &R_default;
		return &R_default;
	} else
		return NULL;
}
/* Search routing table for entry with specific width */
struct route *
rt_blookup(
int32 target,
unsigned int bits)
{
	struct route *rp;

	if(bits == 0){
		if(R_default.iface != NULL)
			return &R_default;
		else
			return NULL;
	}
	/* Mask off target according to width */
	if(bits)
		target &= ~0U << (32-bits);
	else
		target = 0;

	for(rp = Routes[bits-1][hash_ip(target)];rp != NULL;rp = rp->next){
		if(rp->target == target){
			return rp;
		}
	}
	return NULL;
}
/* Scan the routing table. For each entry, see if there's a less-specific
 * one that points to the same interface and gateway. If so, delete
 * the more specific entry, since it is redundant.
 */
void
rt_merge(
int trace
){
	int bits,i,j;
	struct route *rp,*rpnext,*rp1;

	for(bits=32;bits>0;bits--){
		for(i = 0;i<HASHMOD;i++){
			for(rp = Routes[bits-1][i];rp != NULL;rp = rpnext){
				rpnext = rp->next;
				if(!run_timer(&rp->timer))
					continue;
				for(j=bits-1;j >= 0;j--){
					if((rp1 = rt_blookup(rp->target,j)) != NULL){
						if(rp1->iface != rp->iface
						 || rp1->gateway != rp->gateway)
							 goto Next;
						if(trace > 1)
							printf("merge %s %d\n",
							 inet_ntoa(rp->target),
							 rp->bits);
						rt_drop(rp->target,rp->bits);
						break;
					}
				}
Next:                           ;
			}
		}
	}
}
