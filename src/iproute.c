/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iproute.c,v 1.7 1991/05/09 07:38:27 deyke Exp $ */

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
#include "netuser.h"
#include "icmp.h"
#include "rip.h"
#include "trace.h"
#include "pktdrvr.h"

struct route *Routes[32][HASHMOD];      /* Routing table */
struct route R_default = {              /* Default route entry */
	NULLROUTE, NULLROUTE,
	0,0,0,
	RIP_INFINITY            /* Init metric to infinity */
};

int32 Ip_addr;
static struct rt_cache Rt_cache;

/* Initialize modulo lookup table used by hash_ip() in pcgen.asm */
void
ipinit()
{
	int i;

	for(i=0;i<256;i++)
		Hashtab[i] = i % HASHMOD;
}

/* Route an IP datagram. This is the "hopper" through which all IP datagrams,
 * coming or going, must pass.
 *
 * "rxbroadcast" is set to indicate that the packet came in on a subnet
 * broadcast. The router will kick the packet upstairs regardless of the
 * IP destination address.
 */
int
ip_route(i_iface,bp,rxbroadcast)
struct iface *i_iface;  /* Input interface */
struct mbuf *bp;        /* Input packet */
int rxbroadcast;        /* True if packet had link broadcast address */
{
	struct ip ip;                   /* IP header being processed */
	int16 ip_len;                   /* IP header length */
	int16 length;                   /* Length of data portion */
	int32 gateway;                  /* Gateway IP address */
	register struct route *rp;      /* Route table entry */
	struct iface *iface;            /* Output interface, possibly forwarded */
	int16 offset;                   /* Offset into current fragment */
	int16 mf_flag;                  /* Original datagram MF flag */
	int strict = 0;                 /* Strict source routing flag */
	char prec;                      /* Extracted from tos field */
	char del;
	char tput;
	char rel;
	int16 opt_len;          /* Length of current option */
	char *opt;              /* -> beginning of current option */
	char *ptr;              /* -> pointer field in source route fields */
	struct mbuf *tbp;
	int ckgood = 1;

	if(i_iface != NULLIF){
		ipInReceives++; /* Not locally generated */
		i_iface->iprecvcnt++;
	}
	if(len_p(bp) < IPLEN){
		/* The packet is shorter than a legal IP header */
		ipInHdrErrors++;
		free_p(bp);
		return -1;
	}
	/* Sneak a peek at the IP header's IHL field to find its length */
	ip_len = (bp->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		/* The IP header length field is too small */
		ipInHdrErrors++;
		free_p(bp);
		return -1;
	}
	if(cksum(NULLHEADER,bp,ip_len) != 0){
		/* Bad IP header checksum; discard */
		ipInHdrErrors++;
		free_p(bp);
		return -1;
	}
	/* Extract IP header */
	ntohip(&ip,&bp);

	if(ip.version != IPVERSION){
		/* We can't handle this version of IP */
		ipInHdrErrors++;
		free_p(bp);
		return -1;
	}

	if(i_iface != NULLIF && ismyaddr(ip.source) == NULLIF)
		rt_add(ip.source, 32, 0L, i_iface, 1L, 0x7fffffff / 1000, 0);

	/* Trim data segment if necessary. */
	length = ip.length - ip_len;    /* Length of data portion */
	trim_mbuf(&bp,length);

	/* If we're running low on memory, return a source quench */
	if(!rxbroadcast && availmem() < Memthresh)
		icmp_output(&ip,bp,ICMP_QUENCH,0,NULLICMP);

	/* Process options, if any. Also compute length of secondary IP
	 * header in case fragmentation is needed later
	 */
	strict = 0;
	for(opt = ip.options; opt < &ip.options[ip.optlen];opt += opt_len){
		/* Most options have a length field. If this is a EOL or NOOP,
		 * this (garbage) value won't be used
		 */
		opt_len = uchar(opt[1]);

		switch(opt[0] & OPT_NUMBER){
		case IP_EOL:
			goto no_opt;    /* End of options list, we're done */
		case IP_NOOP:
			opt_len = 1;
			break;          /* No operation, skip to next option */
		case IP_SSROUTE:        /* Strict source route & record route */
			strict = 1;     /* note fall-thru */
		case IP_LSROUTE:        /* Loose source route & record route */
			/* Source routes are ignored unless we're in the
			 * destination field
			 */
			if(ismyaddr(ip.dest) == NULLIF)
				break;  /* Skip to next option */
			if(uchar(opt[2]) >= opt_len)
				break;  /* Route exhausted; it's for us */

			/* Put address for next hop into destination field,
			 * put our address into the route field, and bump
			 * the pointer
			 */
			ptr = opt + uchar(opt[2]) - 1;
			ip.dest = get32(ptr);
			put32(ptr,locaddr(ip.dest));
			opt[2] += 4;
			ckgood = 0;
			break;
		case IP_RROUTE: /* Record route */
			if(uchar(opt[2]) > opt_len-3){
				/* Route area exhausted; kick back an error */
				if(!rxbroadcast){
					union icmp_args icmp_args;

					icmp_args.pointer = IPLEN + opt - ip.options;
					icmp_output(&ip,bp,ICMP_PARAM_PROB,0,&icmp_args);
				}
				if(uchar(opt[2]) != opt_len + 1){
					free_p(bp);
					return -1;
				}
			} else {
				/* Add our address to the route */
				ptr = opt + uchar(opt[2]) - 1;
				ptr = put32(ptr,locaddr(ip.dest));
				opt[2] += 4;
				ckgood = 0;
			}
			break;
		}
	}
no_opt:

	/* See if it's a broadcast or addressed to us, and kick it upstairs */
	if(ismyaddr(ip.dest) != NULLIF || rxbroadcast){
#ifdef  GWONLY
	/* We're only a gateway, we have no host level protocols */
		if(!rxbroadcast)
			icmp_output(&ip,bp,ICMP_DEST_UNREACH,
			 ICMP_PROT_UNREACH,NULLICMP);
		ipInUnknownProtos++;
		free_p(bp);
#else
		ip_recv(i_iface,&ip,bp,rxbroadcast);
#endif
		return 0;
	}
	/* Packet is not destined to us. If it originated elsewhere, count
	 * it as a forwarded datagram.
	 */
	if(i_iface != NULLIF)
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
		icmp_output(&ip,bp,ICMP_TIME_EXCEED,0,NULLICMP);
		ipInHdrErrors++;
		free_p(bp);
		return -1;
	}
	/* Look up target address in routing table */
	if((rp = rt_lookup(ip.dest)) == NULLROUTE){
		/* No route exists, return unreachable message (we already
		 * know this can't be a broadcast)
		 */
		icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULLICMP);
		free_p(bp);
		ipOutNoRoutes++;
		return -1;
	}
	rp->uses++;

	/* Check for output forwarding and divert if necessary */
	iface = rp->iface;
	if(iface->forw != NULLIF)
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
		icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_ROUTE_FAIL,NULLICMP);
		free_p(bp);
		ipOutNoRoutes++;
		return -1;
	}
	prec = PREC(ip.tos);
	del = ip.tos & DELAY;
	tput = ip.tos & THRUPUT;
	rel = ip.tos & RELIABILITY;

	if(ip.length <= iface->mtu){
		/* Datagram smaller than interface MTU; put header
		 * back on and send normally.
		 */
		if((tbp = htonip(&ip,bp,ckgood)) == NULLBUF){
			free_p(bp);
			return -1;
		}
		iface->ipsndcnt++;
		return (*iface->send)(tbp,iface,gateway,prec,del,tput,rel);
	}
	/* Fragmentation needed */
	if(ip.flags.df){
		/* Don't Fragment set; return ICMP message and drop */
		union icmp_args icmp_args;

		icmp_args.mtu = iface->mtu;
		icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED,&icmp_args);
		free_p(bp);
		ipFragFails++;
		return -1;
	}
	/* Create fragments */
	offset = ip.offset;
	mf_flag = ip.flags.mf;          /* Save original MF flag */
	while(length != 0){             /* As long as there's data left */
		int16 fragsize;         /* Size of this fragment's data */
		struct mbuf *f_data;    /* Data portion of fragment */

		/* After the first fragment, should remove those
		 * options that aren't supposed to be copied on fragmentation
		 */
		ip.offset = offset;
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
		dup_p(&f_data,bp,offset,fragsize);
		if(f_data == NULLBUF){
			free_p(bp);
			ipFragFails++;
			return -1;
		}
		/* Put IP header back on, recomputing checksum */
		if((tbp = htonip(&ip,f_data,0)) == NULLBUF){
			free_p(f_data);
			free_p(bp);
			ipFragFails++;
			return -1;
		}
		/* and ship it out */
		if((*iface->send)(tbp,iface,gateway,prec,del,tput,rel) == -1){
			ipFragFails++;
			free_p(bp);
			return -1;
		}
		iface->ipsndcnt++;
		ipFragCreates++;
		offset += fragsize;
		length -= fragsize;
	}
	ipFragOKs++;
	free_p(bp);
	return 0;
}
int
ip_encap(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int prec;
int del;
int tput;
int rel;
{
	struct ip ip;

	dump(iface,IF_TRACE_OUT,CL_NONE,bp);
	iface->rawsndcnt++;
	iface->lastsent = secclock();

	if(gateway == 0L){
		/* Gateway must be specified */
		ntohip(&ip,&bp);
		icmp_output(&ip,bp,ICMP_DEST_UNREACH,ICMP_HOST_UNREACH,NULLICMP);
		free_p(bp);
		ipOutNoRoutes++;
		return -1;
	}
	/* Encapsulate in an IP packet from us to the gateway */
	return ip_send(INADDR_ANY,gateway,IP_PTCL,0,0,bp,0,0,0);
}

/* Add an entry to the IP routing table. Returns 0 on success, -1 on failure */
struct route *
rt_add(target,bits,gateway,iface,metric,ttl,private)
int32 target;           /* Target IP address prefix */
unsigned int bits;      /* Size of target address prefix in bits (0-32) */
int32 gateway;          /* Optional gateway to be reached via interface */
struct iface *iface;    /* Interface to which packet is to be routed */
int32 metric;           /* Metric for this route entry */
int32 ttl;              /* Lifetime of this route entry in sec */
char private;           /* Inhibit advertising this entry ? */
{
	struct route *rp,**hp;
	struct route *rptmp;
	int32 gwtmp;

	if(iface == NULLIF)
		return NULLROUTE;

	if(bits == 32 && ismyaddr(target))
		return NULLROUTE;       /* Don't accept routes to ourselves */

	/* Encapsulated routes must specify gateway, and it can't be
	 *  ourselves
	 */
	if(iface == &Encap && (gateway == 0 || ismyaddr(gateway)))
		return NULLROUTE;

	Rt_cache.route = NULLROUTE;     /* Flush cache */

	/* Zero bits refers to the default route */
	if(bits == 0){
		rp = &R_default;
	} else {
		rp = rt_blookup(target,bits);
	}
	if(rp == NULLROUTE){
		/* The target is not already in the table, so create a new
		 * entry and put it in.
		 */
		rp = (struct route *)callocw(1,sizeof(struct route));
		/* Insert at head of table */
		rp->prev = NULLROUTE;
		hp = &Routes[bits-1][hash_ip(target)];
		rp->next = *hp;
		if(rp->next != NULLROUTE)
			rp->next->prev = rp;
		*hp = rp;
		rp->uses = 0;
	}
	rp->target = target;
	rp->bits = bits;
	rp->gateway = gateway;
	rp->metric = metric;
	rp->iface = iface;
	rp->flags = private ? RTPRIVATE : 0;    /* Should anyone be told of this route? */
	rp->timer.func = rt_timeout;  /* Set the timer field */
	rp->timer.arg = (void *)rp;
	set_timer(&rp->timer,ttl*1000L);
	stop_timer(&rp->timer);
	start_timer(&rp->timer); /* start the timer if appropriate */

	/* Check to see if this created an encapsulation loop */
	gwtmp = gateway;
	for(;;){
		rptmp = rt_lookup(gwtmp);
		if(rptmp == NULLROUTE)
			break;  /* No route to gateway, so no loop */
		if(rptmp->iface != &Encap)
			break;  /* Non-encap interface, so no loop */
		if(rptmp == rp){
			rt_drop(target,bits);   /* Definite loop */
			return NULLROUTE;
		}
		if(rptmp->gateway != 0)
			gwtmp = rptmp->gateway;
	}
	route_savefile();
	return rp;
}

/* Remove an entry from the IP routing table. Returns 0 on success, -1
 * if entry was not in table.
 */
int
rt_drop(target,bits)
int32 target;
unsigned int bits;
{
	register struct route *rp;

	Rt_cache.route = NULLROUTE;     /* Flush the cache */

	if(bits == 0){
		/* Nail the default entry */
		stop_timer(&R_default.timer);
		R_default.iface = NULLIF;
		return 0;
	}
	if(bits > 32)
		bits = 32;

	/* Mask off target according to width */
	target &= ~0L << (32-bits);

	/* Search appropriate chain for existing entry */
	for(rp = Routes[bits-1][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
		if(rp->target == target)
			break;
	}
	if(rp == NULLROUTE)
		return -1;      /* Not in table */

	stop_timer(&rp->timer);
	if(rp->next != NULLROUTE)
		rp->next->prev = rp->prev;
	if(rp->prev != NULLROUTE)
		rp->prev->next = rp->next;
	else
		Routes[bits-1][hash_ip(target)] = rp->next;

	free((char *)rp);
	return 0;
}
#if 1
char Hashtab[256];      /* Modulus lookup table */

/* Compute hash function on IP address */
int16
hash_ip(addr)
register int32 addr;
{
	register int16 ret;

	ret = hiword(addr);
	ret ^= loword(addr);
	return (int16)(ret % HASHMOD);
}
#endif
#ifndef GWONLY
/* Given an IP address, return the MTU of the local interface used to
 * reach that destination. This is used by TCP to avoid local fragmentation
 */
int16
ip_mtu(addr)
int32 addr;
{
	register struct route *rp;
	struct iface *iface;

	rp = rt_lookup(addr);
	if(rp == NULLROUTE || rp->iface == NULLIF)
		return 0;

	iface = rp->iface;
	if(iface->forw != NULLIF)
		return iface->forw->mtu;
	else
		return iface->mtu;
}
/* Given a destination address, return the IP address of the local
 * interface that will be used to reach it. If there is no route
 * to the destination, pick the first non-loopback address.
 */
int32
locaddr(addr)
int32 addr;
{
	register struct route *rp;
	struct iface *ifp;

	if(ismyaddr(addr) != NULLIF)
		return addr;    /* Loopback case */

	rp = rt_lookup(addr);
	if(rp != NULLROUTE && rp->iface != NULLIF)
		ifp = rp->iface;
	else {
		/* No route currently exists, so just pick the first real
		 * interface and use its address
		 */
		for(ifp = Ifaces;ifp != NULLIF;ifp = ifp->next){
			if(ifp != &Loopback && ifp != &Encap)
				break;
		}
	}
	if(ifp == NULLIF || ifp == &Loopback)
		return 0;       /* No dice */

	if(ifp == &Encap){
		/* Recursive call - we assume that there are no circular
		 * encapsulation references in the routing table!!
		 * (There is a check at the end of rt_add() that goes to
		 * great pains to ensure this.)
		 */
		return locaddr(rp->gateway);
	}
	if(ifp->forw != NULLIF)
		return ifp->forw->addr;
	else
		return ifp->addr;
}
#endif
/* Look up target in hash table, matching the entry having the largest number
 * of leading bits in common. Return default route if not found;
 * if default route not set, return NULLROUTE
 */
struct route *
rt_lookup(target)
int32 target;
{
	register struct route *rp;
	int bits;
	int32 tsave;
	int32 mask;

	/* Examine cache first */
	if(target == Rt_cache.target && Rt_cache.route != NULLROUTE)
		return Rt_cache.route;

	tsave = target;

	mask = ~0;      /* All ones */
	for(bits = 31;bits >= 0; bits--){
		target &= mask;
		for(rp = Routes[bits][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
			if(rp->target == target){
				/* Stash in cache and return */
				Rt_cache.target = tsave;
				Rt_cache.route = rp;
				return rp;
			}
		}
		mask <<= 1;
	}
	if(R_default.iface != NULLIF){
		Rt_cache.target = tsave;
		Rt_cache.route = &R_default;
		return &R_default;
	} else
		return NULLROUTE;
}
/* Search routing table for entry with specific width */
struct route *
rt_blookup(target,bits)
int32 target;
unsigned int bits;
{
	register struct route *rp;

	if(bits == 0){
		if(R_default.iface != NULLIF)
			return &R_default;
		else
			return NULLROUTE;
	}
	/* Mask off target according to width */
	target &= ~0L << (32-bits);

	for(rp = Routes[bits-1][hash_ip(target)];rp != NULLROUTE;rp = rp->next){
		if(rp->target == target){
			return rp;
		}
	}
	return NULLROUTE;
}
/* Scan the routing table. For each entry, see if there's a less-specific
 * one that points to the same interface and gateway. If so, delete
 * the more specific entry, since it is redundant.
 */
void
rt_merge(trace)
int trace;
{
	int bits,i,j;
	struct route *rp,*rpnext,*rp1;

	for(bits=32;bits>0;bits--){
		for(i = 0;i<HASHMOD;i++){
			for(rp = Routes[bits-1][i];rp != NULLROUTE;rp = rpnext){
				rpnext = rp->next;
				for(j=bits-1;j >= 0;j--){
					if((rp1 = rt_blookup(rp->target,j)) != NULLROUTE
					 && rp1->iface == rp->iface
					 && rp1->gateway == rp->gateway){
						if(trace > 1)
							printf("merge %s %d\n",
							 inet_ntoa(rp->target),
							 rp->bits);
						rt_drop(rp->target,rp->bits);
						break;
					}
				}
			}
		}
	}
}
