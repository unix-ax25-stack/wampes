/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25dump.c,v 1.2 1990/08/23 17:32:31 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "timer.h"
#include "trace.h"

static char *decode_type __ARGS((int type));

/* Dump an AX.25 packet header */
void
ax25_dump(fp,bpp,check)
FILE *fp;
struct mbuf **bpp;
int check;      /* Not used */
{
	char tmp[20];
	char control,pid;
	int16 type,ftype();
	struct ax25 hdr;
	struct ax25_addr *hp;

	fprintf(fp,"AX25: ");
	/* Extract the address header */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		fprintf(fp," bad header!\n");
		return;
	}
	pax25(tmp,&hdr.source);
	fprintf(fp,"%s",tmp);
	pax25(tmp,&hdr.dest);
	fprintf(fp,"->%s",tmp);
	if(hdr.ndigis > 0){
		fprintf(fp," v");
		for(hp = &hdr.digis[0]; hp < &hdr.digis[hdr.ndigis]; hp++){
			/* Print digi string */
			pax25(tmp,hp);
			fprintf(fp," %s%s",tmp,(hp->ssid & REPEATED) ? "*":"");
		}
	}
	if(pullup(bpp,&control,1) != 1)
		return;

	putc(' ',fp);
	type = ftype(control);
	fprintf(fp,"%s",decode_type(type));
	/* Dump poll/final bit */
	if(control & PF){
		switch(hdr.cmdrsp){
		case LAPB_COMMAND:
			fprintf(fp,"(P)");
			break;
		case LAPB_RESPONSE:
			fprintf(fp,"(F)");
			break;
		default:
			fprintf(fp,"(P/F)");
			break;
		}
	}
	/* Dump sequence numbers */
	if((type & 0x3) != U)   /* I or S frame? */
		fprintf(fp," NR=%d",(control>>5)&7);
	if(type == I || type == UI){
		if(type == I)
			fprintf(fp," NS=%d",(control>>1)&7);
		/* Decode I field */
		if(pullup(bpp,&pid,1) == 1){    /* Get pid */
			switch(pid & (PID_FIRST | PID_LAST)){
			case PID_FIRST:
				fprintf(fp," First frag");
				break;
			case PID_LAST:
				fprintf(fp," Last frag");
				break;
			case PID_FIRST|PID_LAST:
				break;  /* Complete message, say nothing */
			case 0:
				fprintf(fp," Middle frag");
				break;
			}
			fprintf(fp," pid=");
			switch(pid & 0x3f){
			case PID_ARP:
				fprintf(fp,"ARP\n");
				break;
			case PID_NETROM:
				fprintf(fp,"NET/ROM\n");
				break;
			case PID_IP:
				fprintf(fp,"IP\n");
				break;
			case PID_NO_L3:
				fprintf(fp,"Text\n");
				break;
			default:
				fprintf(fp,"0x%x\n",pid);
			}
			/* Only decode frames that are the first in a
			 * multi-frame sequence
			 */
			switch(pid & (PID_PID | PID_FIRST)){
			case PID_ARP | PID_FIRST:
				arp_dump(fp,bpp);
				break;
			case PID_IP | PID_FIRST:
				/* Only checksum complete frames */
				ip_dump(fp,bpp,pid & PID_LAST);
				break;
			case PID_NETROM | PID_FIRST:
				netrom_dump(fp,bpp);
				break;
			}
		}
	} else if(type == FRMR && pullup(bpp,tmp,3) == 3){
		fprintf(fp,": %s",decode_type(ftype(tmp[0])));
		fprintf(fp," Vr = %d Vs = %d",(tmp[1] >> 5) & MMASK,
			(tmp[1] >> 1) & MMASK);
		if(tmp[2] & W)
			fprintf(fp," Invalid control field");
		if(tmp[2] & X)
			fprintf(fp," Illegal I-field");
		if(tmp[2] & Y)
			fprintf(fp," Too-long I-field");
		if(tmp[2] & Z)
			fprintf(fp," Invalid seq number");
		fputc('\n',fp);
	} else
		fputc('\n',fp);

	fflush(stdout);
}
/* Display NET/ROM network and transport headers */
void
netrom_dump(fp,bpp)
FILE *fp;
struct mbuf **bpp;
{
	struct ax25_addr src,dest;
	char x;
	char tmp[16];
	char thdr[5];
	register i;

	if(bpp == NULLBUFP || *bpp == NULLBUF)
		return;
	/* See if it is a routing broadcast */
	if(uchar(*(*bpp)->data) == 0xff) {
		pullup(bpp,tmp,1);              /* Signature */
		pullup(bpp,tmp,ALEN);
		tmp[ALEN] = '\0';
		fprintf(fp,"NET/ROM Routing: %s\n",tmp);
		for(i = 0;i < 11;i++) {
			if (pullup(bpp,tmp,AXALEN) < AXALEN)
				break;
			memcpy(src.call,tmp,ALEN);
			src.ssid = tmp[ALEN];
			pax25(tmp,&src);
			fprintf(fp,"        %12s",tmp);
			pullup(bpp,tmp,ALEN);
			tmp[ALEN] = '\0';
			fprintf(fp,"%8s",tmp);
			pullup(bpp,tmp,AXALEN);
			memcpy(src.call,tmp, ALEN);
			src.ssid = tmp[ALEN];
			pax25(tmp,&src);
			fprintf(fp,"    %12s", tmp);
			pullup(bpp,tmp,1);
			fprintf(fp,"    %3u\n", (unsigned)uchar(tmp[0]));
		}
		return;
	}
	/* Decode network layer */
	pullup(bpp,tmp,AXALEN);
	memcpy(src.call,tmp,ALEN);
	src.ssid = tmp[ALEN];
	pax25(tmp,&src);
	fprintf(fp,"NET/ROM: %s",tmp);

	pullup(bpp,tmp,AXALEN);
	memcpy(dest.call,tmp,ALEN);
	dest.ssid = tmp[ALEN];
	pax25(tmp,&dest);
	fprintf(fp,"->%s",tmp);

	pullup(bpp,&x,1);
	fprintf(fp," ttl %d\n",uchar(x));

	/* Read first five bytes of "transport" header */
	pullup(bpp,thdr,5);
	switch(thdr[4] & 0xf){
	case 0: /* network PID extension */
		if (uchar(thdr[0]) == PID_IP && uchar(thdr[1]) == PID_IP)
			ip_dump(fp,bpp,1) ;
		else
			fprintf(fp,"         protocol family %x, proto %x",
					uchar(thdr[0]), uchar(thdr[1])) ;
		break ;
	case 1: /* Connect request */
		fprintf(fp,"         conn rqst: ckt %d/%d",uchar(thdr[0]),uchar(thdr[1]));
		pullup(bpp,&x,1);
		fprintf(fp," wnd %d",x);
		pullup(bpp,(char *)&src,AXALEN);
		pax25(tmp,&src);
		fprintf(fp," %s",tmp);
		pullup(bpp,(char *)&dest,AXALEN);
		pax25(tmp,&dest);
		fprintf(fp,"->%s",tmp);
		break;
	case 2: /* Connect acknowledgement */
		fprintf(fp,"         conn ack: ur ckt %d/%d my ckt %d/%d",
			uchar(thdr[0]), uchar(thdr[1]), uchar(thdr[2]),
			uchar(thdr[3]));
		pullup(bpp,&x,1);
		fprintf(fp," wnd %d",x);
		break;
	case 3: /* Disconnect request */
		fprintf(fp,"         disc: ckt %d/%d",uchar(thdr[0]),uchar(thdr[1]));
		break;
	case 4: /* Disconnect acknowledgement */
		fprintf(fp,"         disc ack: ckt %d/%d",uchar(thdr[0]),uchar(thdr[1]));
		break;
	case 5: /* Information (data) */
		fprintf(fp,"         info: ckt %d/%d",uchar(thdr[0]),uchar(thdr[1]));
		fprintf(fp," txseq %d rxseq %d",uchar(thdr[2]), uchar(thdr[3]));
		break;
	case 6: /* Information acknowledgement */
		fprintf(fp,"         info ack: ckt %d/%d rxseq %d",
				uchar(thdr[0]),uchar(thdr[1]),uchar(thdr[3]));
		break;
	default:
		fprintf(fp,"         unknown transport type %d", thdr[4] & 0x0f) ;
		break;
	}
	if(thdr[4] & 0x80)
		fprintf(fp," CHOKE");
	if(thdr[4] & 0x40)
		fprintf(fp," NAK");
	fputc('\n',fp);
}
static char *
decode_type(type)
int16 type;
{
	switch(uchar(type)){
	case I:
		return "I";
	case SABM:
		return "SABM";
	case DISC:
		return "DISC";
	case DM:
		return "DM";
	case UA:
		return "UA";
	case RR:
		return "RR";
	case RNR:
		return "RNR";
	case REJ:
		return "REJ";
	case FRMR:
		return "FRMR";
	case UI:
		return "UI";
	default:
		return "[invalid]";
	}
}

