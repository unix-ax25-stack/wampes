/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/udpdump.c,v 1.2 1990/08/23 17:34:35 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "udp.h"
#include "trace.h"

/* Dump a UDP header */
void
udp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;              /* If 0, bypass checksum verify */
{
	struct udp udp;
	struct pseudo_header ph;
	int16 csum;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	fprintf(fp,"UDP:");

	/* Compute checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = UDP_PTCL;
	ph.length = len_p(*bpp);
	if((csum = cksum(&ph,*bpp,ph.length)) == 0)
		check = 0;      /* No checksum error */

	ntohudp(&udp,bpp);

	fprintf(fp," %u->%u",udp.source,udp.dest);
	fprintf(fp," len %u",udp.length);
	if(udp.checksum == 0)
		check = 0;
	if(check)
		fprintf(fp," CHECKSUM ERROR (%u)",csum);

	fprintf(fp,"\n");
}

