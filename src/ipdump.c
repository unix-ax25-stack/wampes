/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipdump.c,v 1.2 1990/08/23 17:33:13 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "timer.h"
#include "iface.h"
#include "ip.h"
#include "trace.h"
#include "netuser.h"

void
ip_dump(fp,bpp,check)
FILE *fp;
struct mbuf **bpp;
int check;
{
	struct ip ip;
	int16 ip_len;
	int16 offset;
	int16 length;
	int16 csum;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;

	fprintf(fp,"IP:");
	/* Sneak peek at IP header and find length */
	ip_len = ((*bpp)->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		fprintf(fp," bad header\n");
		return;
	}
	if(check)
		csum = cksum(NULLHEADER,*bpp,ip_len);
	else
		csum = 0;

	ntohip(&ip,bpp);        /* Can't fail, we've already checked ihl */

	/* Trim data segment if necessary. */
	length = ip.length - ip_len;    /* Length of data portion */
	trim_mbuf(bpp,length);
	fprintf(fp," len %u",ip.length);
	fprintf(fp," %s",inet_ntoa(ip.source));
	fprintf(fp,"->%s ihl %u ttl %u",
		inet_ntoa(ip.dest),ip_len,uchar(ip.ttl));
	if(ip.tos != 0)
		fprintf(fp," tos %u",uchar(ip.tos));
	offset = (ip.fl_offs & F_OFFSET) << 3;
	if(offset != 0 || (ip.fl_offs & MF))
		fprintf(fp," id %u offs %u",ip.id,offset);
	if(ip.fl_offs & DF)
		fprintf(fp," DF");
	if(ip.fl_offs & MF){
		fprintf(fp," MF");
		check = 0;      /* Bypass host-level checksum verify */
	}
	if(csum != 0)
		fprintf(fp," CHECKSUM ERROR (%u)",csum);

	if(offset != 0){
		fprintf(fp,"\n");
		return;
	}
	switch(uchar(ip.protocol)){
	case TCP_PTCL:
		fprintf(fp," prot TCP\n");
		tcp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	case UDP_PTCL:
		fprintf(fp," prot UDP\n");
		udp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	case ICMP_PTCL:
		fprintf(fp," prot ICMP\n");
		icmp_dump(fp,bpp,ip.source,ip.dest,check);
		break;
	default:
		fprintf(fp," prot %u\n",uchar(ip.protocol));
		break;
	}
}

