/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ax25subr.c,v 1.5 1990/10/12 19:25:17 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "ax25.h"
#include <ctype.h>

/*
 * setcall - convert callsign plus substation ID of the form
 * "KA9Q-0" to AX.25 (shifted) address format
 *   Address extension bit is left clear
 *   Return -1 on error, 0 if OK
 */
int
setcall(out,call)
char *out;
char *call;
{
	int csize;
	unsigned ssid;
	register int i;
	register char *dp;
	char c;

	if(out == NULLCHAR || call == NULLCHAR || *call == '\0'){
		return -1;
	}
	/* Find dash, if any, separating callsign from ssid
	 * Then compute length of callsign field and make sure
	 * it isn't excessive
	 */
	dp = strchr(call,'-');
	if(dp == NULLCHAR)
		csize = strlen(call);
	else
		csize = dp - call;
	if(csize > ALEN)
		return -1;
	/* Now find and convert ssid, if any */
	if(dp != NULLCHAR){
		dp++;   /* skip dash */
		ssid = atoi(dp);
		if(ssid > 15)
			return -1;
	} else
		ssid = 0;
	/* Copy upper-case callsign, left shifted one bit */
	for(i=0;i<csize;i++){
		c = *call++;
		if(islower(c))
			c = toupper(c);
		*out++ = c << 1;
	}
	/* Pad with shifted spaces if necessary */
	for(;i<ALEN;i++)
		*out++ = ' ' << 1;

	/* Insert substation ID field and set reserved bits */
	*out = 0x60 | (ssid << 1);
	return 0;
}
/* Set a digipeater string in an ARP table entry */
int
setpath(out,in,cnt)
char *out;      /* Target char array containing addresses in net form */
char *in[];     /* Input array of tokenized callsigns in ASCII */
int cnt;        /* Number of callsigns in array */
{
	if(cnt == 0)
		return;
	while(cnt-- > 0){
		setcall(out,*in++);
		out += AXALEN;
	}
	out[-1] |= E;
}
int
addreq(a,b)
register char *a,*b;
{
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	if (*a++ != *b++) return 0;
	return (*a & SSID) == (*b & SSID);
}
void
addrcp(to,from)
register char *to,*from;
{
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to++ = *from++;
	*to = (*from & SSID) | 0x60;
}
/* Convert encoded AX.25 address to printable string */
char *
pax25(e,addr)
char *e;
char *addr;
{
	register int i;
	char c;
	char *cp;

	cp = e;
	for(i=ALEN;i != 0;i--){
		c = (*addr++ >> 1) & 0x7f;
		if(c != ' ')
			*cp++ = c;
	}
	if ((*addr & SSID) != 0)
		sprintf(cp,"-%d",(*addr >> 1) & 0xf);   /* ssid */
	else
		*cp = '\0';
	return e;
}

/* Print a string of AX.25 addresses in the form
 * "KA9Q-0 [via N4HY-0,N2DSY-2]"
 * Designed for use by ARP - arg is a char string
 */
char *
psax25(e,addr)
char *e,*addr;
{
  int i;
  char *cp;

  cp = e;
  *cp = '\0';
  for (i = 0; ; i++) {
    pax25(cp, addr);
    if (addr[ALEN] & E) break;
    addr += AXALEN;
    if (i)
      strcat(cp, ",");
    else
      strcat(cp, " via ");
    while (*cp) cp++;
  }
  return e;
}
char *
getaxaddr(ap,cp)
register struct ax25_addr *ap;
register char *cp;
{
	memcpy(ap->call,cp,ALEN);
	cp += ALEN;
	ap->ssid = *cp++;
	return cp;
}
char *
putaxaddr(cp,ap)
register char *cp;
register struct ax25_addr *ap;
{
	memcpy(cp,ap->call,ALEN);
	cp += ALEN;
	*cp++ = ap->ssid;
	return cp;
}

/* Convert an AX.25 ARP table entry into a host format address structure
 * ready for use in transmitting a packet
 */
int
atohax25(hdr,hwaddr,source)
register struct ax25 *hdr;
register char *hwaddr;
struct ax25_addr *source;
{
	register struct ax25_addr *axp;

	hwaddr = getaxaddr(&hdr->dest,hwaddr);  /* Destination address */
	ASSIGN(hdr->source,*source);            /* Source address */
	if(hdr->dest.ssid & E){
		/* No digipeaters */
		hdr->ndigis = 0;
		hdr->dest.ssid &= ~E;
		hdr->source.ssid |= E;
		return 2;
	}
	hdr->source.ssid &= ~E;
	hdr->dest.ssid &= ~E;
	for(axp = hdr->digis; axp < &hdr->digis[MAXDIGIS]; axp++){
		hwaddr = getaxaddr(axp,hwaddr);
		if(axp->ssid & E){
			hdr->ndigis = axp - hdr->digis + 1;
			return hdr->ndigis;
		}
	}
	return -1;
}

/* Figure out the frame type from the control field
 * This is done by masking out any sequence numbers and the
 * poll/final bit after determining the general class (I/S/U) of the frame
 */
int16
ftype(control)
register int control;
{
	if((control & 1) == 0)  /* An I-frame is an I-frame... */
		return I;
	if(control & 2)         /* U-frames use all except P/F bit for type */
		return (int16)(uchar(control) & ~PF);
	else                    /* S-frames use low order 4 bits for type */
		return (int16)(uchar(control) & 0xf);
}

