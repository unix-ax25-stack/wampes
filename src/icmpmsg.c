/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmpmsg.c,v 1.3 1990/08/23 17:33:03 deyke Exp $ */

#include "global.h"
/* ICMP message types */
char *Icmptypes[] = {
	"Echo Reply",
	NULLCHAR,
	NULLCHAR,
	"Unreachable",
	"Source Quench",
	"Redirect",
	NULLCHAR,
	NULLCHAR,
	"Echo Request",
	NULLCHAR,
	NULLCHAR,
	"Time Exceeded",
	"Parameter Problem",
	"Timestamp",
	"Timestamp Reply",
	"Information Request",
	"Information Reply",
	"Address Mask Request",
	"Address Mask Reply"
};

/* ICMP unreachable messages */
char *Unreach[] = {
	"Network",
	"Host",
	"Protocol",
	"Port",
	"Fragmentation",
	"Source route"
};
/* ICMP Time exceeded messages */
char *Exceed[] = {
	"Time-to-live",
	"Fragment reassembly"
};

/* ICMP redirect messages */
char *Redirect[] = {
	"Network",
	"Host",
	"TOS & Network",
	"TOS & Host"
};

