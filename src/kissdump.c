/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/kissdump.c,v 1.1 1990/09/11 13:45:47 deyke Exp $ */

#include "global.h"
#include "mbuf.h"
#include "kiss.h"
#include "ax25.h"
#include "trace.h"

void
ki_dump(fp,bpp,check)
FILE *fp;
struct mbuf **bpp;
int check;
{
	int type;
	int val;

	fprintf(fp,"KISS: ");
	type = PULLCHAR(bpp);
	if(type == KISS_DATA){
		fprintf(fp,"Data\n");
		ax25_dump(fp,bpp,check);
		return;
	}
	val = PULLCHAR(bpp);
	switch(type){
	case KISS_TXD:
		fprintf(fp,"TX Delay: %lu ms\n",val * 10L);
		break;
	case KISS_P:
		fprintf(fp,"Persistence: %u/256\n",val + 1);
		break;
	case KISS_ST:
		fprintf(fp,"Slot time: %lu ms\n",val * 10L);
		break;
	case KISS_TXT:
		fprintf(fp,"TX Tail time: %lu ms\n",val * 10L);
		break;
	case KISS_FD:
		fprintf(fp,"Duplex: %s\n",val == 0 ? "Half" : "Full");
		break;
	case KISS_HW:
		fprintf(fp,"Hardware %u\n",val);
		break;
	case KISS_RETURN:
		fprintf(fp,"RETURN\n");
		break;
	default:
		fprintf(fp,"code %u arg %u\n",type,val);
		break;
	}
}

int
ki_forus(iface,bp)
struct iface *iface;
struct mbuf *bp;
{
	struct mbuf *bpp;
	int i;

	if(bp->data[0] != KISS_DATA)
		return 0;
	dup_p(&bpp,bp,1,AXALEN);
	i = ax_forus(iface,bpp);
	free_p(bpp);
	return i;
}
