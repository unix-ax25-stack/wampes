/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmpmsg.c,v 1.2 1990/04/05 11:14:38 deyke Exp $ */

#include "global.h"
/* ICMP message types */
char *icmptypes[] = {
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
char *unreach[] = {
	"Network",
	"Host",
	"Protocol",
	"Port",
	"Fragmentation",
	"Source route"
};
/* ICMP Time exceeded messages */
char *exceed[] = {
	"Time-to-live",
	"Fragment reassembly"
};

/* ICMP redirect messages */
char *redirect[] = {
	"Network",
	"Host",
	"TOS & Network",
	"TOS & Host"
};

