/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmpdump.c,v 1.2 1990/08/23 17:33:02 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "icmp.h"
#include "trace.h"

/* Dump an ICMP header */
void
icmp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;
int check;              /* If 0, bypass checksum verify */
{
	struct icmp icmp;
	int16 csum;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;
	csum = cksum(NULLHEADER,*bpp,len_p(*bpp));

	ntohicmp(&icmp,bpp);

	if(uchar(icmp.type) <= 16 && Icmptypes[uchar(icmp.type)] != NULLCHAR)
		fprintf(fp,"ICMP: %s",Icmptypes[uchar(icmp.type)]);
	else
		fprintf(fp,"ICMP: type %u",uchar(icmp.type));

	switch(uchar(icmp.type)){
	case ICMP_DEST_UNREACH:
		if(uchar(icmp.code) <= 5)
			fprintf(fp," %s",Unreach[uchar(icmp.code)]);
		else
			fprintf(fp," code %u",uchar(icmp.code));
		break;
	case ICMP_REDIRECT:
		if(uchar(icmp.code) <= 3)
			fprintf(fp," %s",Redirect[uchar(icmp.code)]);
		else
			fprintf(fp," code %u",uchar(icmp.code));
		break;
	case ICMP_TIME_EXCEED:
		if(uchar(icmp.code) <= 1)
			fprintf(fp," %s",Exceed[uchar(icmp.code)]);
		else
			fprintf(fp," code %u",uchar(icmp.code));
		break;
	case ICMP_PARAM_PROB:
		fprintf(fp," pointer = %u",icmp.args.pointer);
		break;
	case ICMP_ECHO:
	case ICMP_ECHO_REPLY:
	case ICMP_INFO_RQST:
	case ICMP_INFO_REPLY:
	case ICMP_TIMESTAMP:
	case ICMP_TIME_REPLY:
		fprintf(fp," id %u seq %u",icmp.args.echo.id,icmp.args.echo.seq);
		break;
	}
	if(check && csum != 0){
		fprintf(fp," CHECKSUM ERROR (%u)",csum);
	}
	fprintf(fp,"\n");
	/* Dump the offending IP header, if any */
	switch(icmp.type){
	case ICMP_DEST_UNREACH:
	case ICMP_TIME_EXCEED:
	case ICMP_PARAM_PROB:
	case ICMP_QUENCH:
	case ICMP_REDIRECT:
		fprintf(fp,"Returned ");
		ip_dump(fp,bpp,0);
	}
}

