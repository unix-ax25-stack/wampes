/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipdump.c,v 1.11 1996/08/11 18:16:09 deyke Exp $ */

/* IP header tracing routines
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "iface.h"
#include "ip.h"
#include "trace.h"
#include "netuser.h"

void ipldump(FILE *fp,struct ip *ip,struct mbuf **bpp,int check);

void
ip_dump(
FILE *fp,
struct mbuf **bpp,
int check
){
	struct ip ip;
	uint16 ip_len;
	uint16 csum;

	if(bpp == NULL || *bpp == NULL)
		return;

	/* Sneak peek at IP header and find length */
	ip_len = ((*bpp)->data[0] & 0xf) << 2;
	if(ip_len < IPLEN){
		fprintf(fp,"IP: bad header\n");
		return;
	}
	if(check && (csum = cksum(NULL,*bpp,ip_len)) != 0)
		fprintf(fp,"IP: CHECKSUM ERROR (%u)",csum);

	ntohip(&ip,bpp);        /* Can't fail, we've already checked ihl */
	ipldump(fp,&ip,bpp,check);
}
void
ipip_dump(
FILE *fp,
struct mbuf **bpp,
int32 source,
int32 dest,
int check
){
	ip_dump(fp,bpp,check);
}
void
ipldump(
FILE *fp,
struct ip *ip,
struct mbuf **bpp,
int check)
{
	uint16 length;
	int i;

	/* Trim data segment if necessary. */
	length = ip->length - (IPLEN + ip->optlen);     /* Length of data portion */
	trim_mbuf(bpp,length);
	fprintf(fp,"IP: len %u",ip->length);
	fprintf(fp," %s",inet_ntoa(ip->source));
	fprintf(fp,"->%s ihl %u ttl %u",
		inet_ntoa(ip->dest),IPLEN + ip->optlen,ip->ttl);
	if(ip->tos != 0)
		fprintf(fp," tos %u",ip->tos);
	if(ip->offset != 0 || ip->flags.mf)
		fprintf(fp," id %u offs %u",ip->id,ip->offset);
	if(ip->flags.df)
		fprintf(fp," DF");
	if(ip->flags.mf){
		fprintf(fp," MF");
		check = 0;      /* Bypass host-level checksum verify */
	}
	if(ip->flags.congest){
		fprintf(fp," CE");
	}
	if(ip->offset != 0){
		putc('\n',fp);
		return;
	}
	for(i=0;Iplink[i].proto != 0;i++){
		if(Iplink[i].proto == ip->protocol){
			fprintf(fp," prot %s\n",Iplink[i].name);
			(*Iplink[i].dump)(fp,bpp,ip->source,ip->dest,check);
			return;
		}
	}
	fprintf(fp," prot %u\n",ip->protocol);
}

