/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmpmsg.c,v 1.4 1991/02/24 20:16:57 deyke Exp $ */

/* ICMP message type tables
 * Copyright 1991 Phil Karn, KA9Q
 */

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
	"Information Reply"
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

