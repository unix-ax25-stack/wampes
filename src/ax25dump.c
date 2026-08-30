/* @(#) $Id: ax25dump.c,v 1.17 2005/03/11 14:36:09 dl9sau Exp $ */

/* AX25 header tracing
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "lapb.h"
#include "trace.h"
#include "socket.h"

static char *decode_type(uint type);

/* Dump an AX.25 packet header */
void
ax25_dump(
FILE *fp,
struct mbuf **bpp,
int check       /* Not used */
){
	char tmp[AXBUF];
	//char frmr[3];
	char frmr[5];
	int control,controlx,pid,seg;
	uint type;
	int unsegmented;
	struct ax25 hdr;
	uint8 *hp;
	char *s_ext;
	int eax25 = 0;

	/* Extract the address header */
	if(ntohax25(&hdr,bpp) < 0){
		/* Something wrong with the header */
		fprintf(fp,"AX25: bad header!\n");
		return;
	}
        eax25 = (hdr.ext & SSID_EAX25);
	s_ext = (hdr.ext & SSID_DAMA) ? " [DAMA]\n" : "\n";
	fprintf(fp, "%sAX25: ", eax25 ? "E" : "");
	fprintf(fp,"%s",pax25(tmp,hdr.source));
	fprintf(fp,"->%s",pax25(tmp,hdr.dest));
	if(hdr.ndigis > 0){
		fprintf(fp," v");
		for(hp = hdr.digis[0]; hp < &hdr.digis[hdr.ndigis][0];
		 hp += AXALEN){
			/* Print digi string */
			fprintf(fp," %s%s",pax25(tmp,hp),
			 (hp[ALEN] & REPEATED) ? "*":"");
		}
	}
	if(hdr.qso_num != -1)
		fprintf(fp," QSO %d",hdr.qso_num);
	if((control = PULLCHAR(bpp)) == -1) {
		putc('\n',fp);
		return;
	}
	type = ftype(control);
	if ((type & 0x3) == U)  /* modulo-128 only in I or S frames */
		eax25 = 0;
	if  (eax25 && (controlx = PULLCHAR(bpp)) == -1) {
		putc('\n',fp);
		return;
	}
	putc(' ',fp);
	fprintf(fp,"%s",decode_type(type));
	/* Dump poll/final bit */
	if(eax25 ? (controlx & PF_EAX25) : (control & PF)){
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
		fprintf(fp," NR=%d",(eax25 ? ((controlx>>1)&0x7f) : (control>>5)&7));
	if(type == I || type == UI){
		if(type == I)
			fprintf(fp," NS=%d",(eax25 ? ((control>>1) &0x7f) : (control>>1)&7));
		/* Decode I field */
		if((pid = PULLCHAR(bpp)) != -1){        /* Get pid */
			if(pid == PID_SEGMENT){
				unsegmented = 0;
				seg = PULLCHAR(bpp);
				fprintf(fp,"%s remain %u",seg & SEG_FIRST ?
				 " First seg;" : "",seg & SEG_REM);
				if(seg & SEG_FIRST)
					pid = PULLCHAR(bpp);
			} else
				unsegmented = 1;

			switch(pid){
			case PID_SEGMENT:
				fputs(s_ext, fp);
				break;  /* Already displayed */
			case PID_ARP:
				fprintf(fp," pid=ARP%s", s_ext);
				arp_dump(fp,bpp);
				break;
			case PID_NETROM:
				fprintf(fp," pid=NET/ROM%s", s_ext);
				/* Don't verify checksums unless unsegmented */
				netrom_dump(fp,bpp,unsegmented);
				break;
			case PID_IP:
				fprintf(fp," pid=IP%s", s_ext);
				/* Don't verify checksums unless unsegmented */
				ip_dump(fp,bpp,unsegmented);
				break;
#ifdef  AX25_VJCOMP
                        case PID_VJUNCOMP:
				fprintf(fp," pid=VJ%s", s_ext);
				/* Don't verify checksums */
				ip_dump(fp,bpp,0);
				break;
                        case PID_VJCOMP:
                                fprintf(fp," pid=VJC%s", s_ext);
                                /*sl_dump(fp,bpp,0);*/
                                break;
#endif
			case PID_X25:
				fprintf(fp," pid=X.25%s", s_ext);
				break;
			case PID_TEXNET:
				fprintf(fp," pid=TEXNET%s", s_ext);
				break;
			case PID_FLEXNET:
				fprintf(fp," pid=FLEXNET%s", s_ext);
				flexnet_dump(fp,bpp);
				break;
			case PID_FLEXTALK:
				fprintf(fp," pid=FLEXTALK%s", s_ext);
				break;
			case PID_NO_L3:
				fprintf(fp," pid=Text%s", s_ext);
				break;
			default:
				fprintf(fp," pid=0x%x%s",pid, s_ext);
			}
		}
	} else if(type == FRMR && pullup(bpp,frmr,(eax25 ? 5 : 3)) == (eax25 ? 5 : 3)){
		fprintf(fp,": %s",decode_type(ftype(frmr[0])));
		fprintf(fp," Vr = %d Vs = %d",
			(eax25 ? (frmr[3] >> 1) & EMMASK : (frmr[1] >> 5) & MMASK),
			(eax25 ? (frmr[2] >> 1) & EMMASK : (frmr[1] >> 1) & MMASK));
		if(frmr[eax25 ? 4 : 2] & W)
			fprintf(fp," Invalid control field");
		if(frmr[eax25 ? 4 : 2] & X)
			fprintf(fp," Illegal I-field");
		if(frmr[eax25 ? 4 : 2] & Y)
			fprintf(fp," Too-long I-field");
		if(frmr[eax25 ? 4 : 2] & Z)
			fprintf(fp," Invalid seq number");
		fputs(s_ext, fp);
	} else
		/* AUCH HIER die DAMA-Marke, und das war eine Luecke genau an
		 * der wichtigsten Stelle: das ganze Poll-Verfahren besteht aus
		 * S- und U-Rahmen, und ausgerechnet fuer die druckte der
		 * Monitor ein blankes Zeilenende.  Wer ein unmarkiertes SABM zu
		 * sehen glaubte, sah in Wahrheit den Monitor schweigen - mich
		 * hat es einen halben Befund gekostet.
		 */
		fputs(s_ext, fp);

}
static char *
decode_type(uint type)
{
	switch(type){
	case I:
		return "I";
	case SABM:
		return "SABM";
	case SABME:
		return "SABME";
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

/* Return 1 if this packet is directed to us, 0 otherwise. Note that
 * this checks only the ultimate destination, not the digipeater field
 */
int
ax_forus(
struct iface *iface,
struct mbuf *bp
){
	struct mbuf *bpp;
	uint8 dest[AXALEN];

	/* Duplicate the destination address */
	if(dup_p(&bpp,bp,0,AXALEN) != AXALEN){
		free_p(&bpp);
		return 0;
	}
	if(pullup(&bpp,dest,AXALEN) < AXALEN)
		return 0;
	return ax_answers_to(iface,dest);
}

