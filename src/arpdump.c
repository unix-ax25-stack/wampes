/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arpdump.c,v 1.3 1990/09/11 13:44:52 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "arp.h"
#include "netuser.h"
#include "trace.h"

void
arp_dump(fp,bpp)
FILE *fp;
struct mbuf **bpp;
{
	struct arp arp;
	struct arp_type *at;
	int is_ip = 0;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;
	fprintf(fp,"ARP: len %d",len_p(*bpp));
	if(ntoharp(&arp,bpp) == -1){
		fprintf(fp," bad packet\n");
		return;
	}
	if(arp.hardware < NHWTYPES)
		at = &Arp_type[arp.hardware];
	else
		at = NULLATYPE;

	/* Print hardware type in Ascii if known, numerically if not */
	fprintf(fp," hwtype %s",smsg(Arptypes,NHWTYPES,arp.hardware));

	/* Print hardware length only if unknown type, or if it doesn't match
	 * the length in the known types table
	 */
	if(at == NULLATYPE || arp.hwalen != at->hwalen)
		fprintf(fp," hwlen %u",arp.hwalen);

	/* Check for most common case -- upper level protocol is IP */
	if(at != NULLATYPE && arp.protocol == at->iptype){
		fprintf(fp," prot IP");
		is_ip = 1;
	} else {
		fprintf(fp," prot 0x%x prlen %u",arp.protocol,arp.pralen);
	}
	switch(arp.opcode){
	case ARP_REQUEST:
		fprintf(fp," op REQUEST");
		break;
	case ARP_REPLY:
		fprintf(fp," op REPLY");
		break;
	default:
		fprintf(fp," op %u",arp.opcode);
		break;
	}
	if(is_ip)
		fprintf(fp," target %s",inet_ntoa(arp.tprotaddr));
	putc('\n',fp);
}
