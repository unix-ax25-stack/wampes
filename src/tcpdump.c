/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/tcpdump.c,v 1.2 1990/08/23 17:34:08 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "timer.h"
#include "tcp.h"
#include "trace.h"

/* TCP segment header flags */
char *tcpflags[] = {
	"FIN",  /* 0x01 */
	"SYN",  /* 0x02 */
	"RST",  /* 0x04 */
	"PSH",  /* 0x08 */
	"ACK",  /* 0x10 */
	"URG"   /* 0x20 */
};

/* Dump a TCP segment header. Assumed to be in network byte order */
void
tcp_dump(fp,bpp,source,dest,check)
FILE *fp;
struct mbuf **bpp;
int32 source,dest;      /* IP source and dest addresses */
int check;              /* 0 if checksum test is to be bypassed */
{
	int i;
	struct tcp seg;
	struct pseudo_header ph;
	int16 csum;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	/* Verify checksum */
	ph.source = source;
	ph.dest = dest;
	ph.protocol = TCP_PTCL;
	ph.length = len_p(*bpp);
	csum = cksum(&ph,*bpp,ph.length);

	ntohtcp(&seg,bpp);

	fprintf(fp,"TCP: %u->%u Seq x%lx",seg.source,seg.dest,seg.seq,seg.ack);
	if(seg.flags & ACK)
		fprintf(fp," Ack x%lx",seg.ack);
	for(i=0;i<6;i++){
		if(seg.flags & 1 << i){
			fprintf(fp," %s",tcpflags[i]);
		}
	}
	fprintf(fp," Wnd %u",seg.wnd);
	if(seg.flags & URG)
		fprintf(fp," UP x%x",seg.up);
	/* Print options, if any */
	if(seg.mss != 0)
		fprintf(fp," MSS %u",seg.mss);
	if(check && csum != 0)
		fprintf(fp," CHECKSUM ERROR (%u)",i);
	fprintf(fp,"\n");
}

