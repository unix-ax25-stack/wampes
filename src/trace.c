/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/trace.c,v 1.16 1994/02/07 12:39:05 deyke Exp $ */

/* Packet tracing - top level and generic routines, including hex/ascii
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <sys/types.h>
#include <stdio.h>
#ifdef ibm032
#define vfprintf fprintf
#endif
#include <ctype.h>
#include <time.h>
#include "global.h"
#include <stdarg.h>
#include "mbuf.h"
#include "iface.h"
#include "pktdrvr.h"
#include "commands.h"
#include "trace.h"
#include "session.h"
#include "timer.h"

static void ascii_dump(FILE *fp,struct mbuf **bpp);
static void ctohex(char *buf,int    c);
static void fmtline(FILE *fp,int    addr,char *buf,int    len);
void hex_dump(FILE *fp,struct mbuf **bpp);
static void showtrace(struct iface *ifp);

/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
static char nospace[] = "No space!!\n";

struct tracecmd Tracecmd[] = {
	"input",        IF_TRACE_IN,    IF_TRACE_IN,
	"-input",       0,              IF_TRACE_IN,
	"output",       IF_TRACE_OUT,   IF_TRACE_OUT,
	"-output",      0,              IF_TRACE_OUT,
	"broadcast",    0,              IF_TRACE_NOBC,
	"-broadcast",   IF_TRACE_NOBC,  IF_TRACE_NOBC,
	"raw",          IF_TRACE_RAW,   IF_TRACE_RAW,
	"-raw",         0,              IF_TRACE_RAW,
	"ascii",        IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX,
	"-ascii",       0,              IF_TRACE_ASCII|IF_TRACE_HEX,
	"hex",          IF_TRACE_HEX,   IF_TRACE_ASCII|IF_TRACE_HEX,
	"-hex",         IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX,
	"off",          0,              0xffff,
	NULLCHAR,       0,              0
};

void
dump(ifp,direction,bp)
register struct iface *ifp;
int direction;
struct mbuf *bp;
{
	struct mbuf *tbp;
	uint16 size;
	time_t timer;
	char *cp;
	struct iftype *ift;
	FILE *fp;

	if(ifp == NULL || (ifp->trace & direction) == 0
	 || (fp = ifp->trfp) == NULLFILE)
		return; /* Nothing to trace */

	ift = ifp->iftype;
	switch(direction){
	case IF_TRACE_IN:
		if((ifp->trace & IF_TRACE_NOBC)
		 && ift != NULLIFT
		 && (ift->addrtest != NULLFP)
		 && (*ift->addrtest)(ifp,bp) == 0)
			return;         /* broadcasts are suppressed */
		timer = secclock();
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s recv:\n",cp,ifp->name);
		break;
	case IF_TRACE_OUT:
		timer = secclock();
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s sent:\n",cp,ifp->name);
		break;
	}
	if(bp == NULLBUF || (size = len_p(bp)) == 0){
		fprintf(fp,"empty packet!!\n");
		return;
	}
	dup_p(&tbp,bp,0,size);
	if(tbp == NULLBUF){
		fprintf(fp,nospace);
		return;
	}
	if(ift != NULLIFT && ift->trace != NULLVFP)
		(*ift->trace)(fp,&tbp,1);
	if(ifp->trace & IF_TRACE_ASCII){
		/* Dump only data portion of packet in ascii */
		ascii_dump(fp,&tbp);
	} else if(ifp->trace & IF_TRACE_HEX){
		/* Dump entire packet in hex/ascii */
		free_p(tbp);
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp != NULLBUF)
			hex_dump(fp,&tbp);
		else
			fprintf(fp,nospace);
	}
	free_p(tbp);
}

/* Dump packet bytes, no interpretation */
void
raw_dump(ifp,direction,bp)
struct iface *ifp;
int direction;
struct mbuf *bp;
{
	struct mbuf *tbp;
	FILE *fp;

	if((fp = ifp->trfp) == NULLFILE)
		return;
	fprintf(fp,"\n******* raw packet dump (%s)\n",
	 ((direction & IF_TRACE_OUT) ? "send" : "recv"));
	dup_p(&tbp,bp,0,len_p(bp));
	if(tbp != NULLBUF)
		hex_dump(fp,&tbp);
	else
		fprintf(fp,nospace);
	fprintf(fp,"*******\n");
	free_p(tbp);
}

/* Dump an mbuf in hex */
void
hex_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	uint16 n;
	uint16 address;
	char buf[16];

	if(bpp == NULLBUFP || *bpp == NULLBUF || fp == NULLFILE)
		return;

	address = 0;
	while((n = pullup(bpp,buf,sizeof(buf))) != 0){
		fmtline(fp,address,buf,n);
		address += n;
	}
}
/* Dump an mbuf in ascii */
static void
ascii_dump(fp,bpp)
FILE *fp;
register struct mbuf **bpp;
{
	int c;
	register uint16 tot;

	if(bpp == NULLBUFP || *bpp == NULLBUF || fp == NULLFILE)
		return;

	tot = 0;
	while((c = PULLCHAR(bpp)) != -1){
		if((tot % 64) == 0)
			fprintf(fp,"%04x  ",tot);
		putc(isprint(uchar(c)) ? c : '.',fp);
		if((++tot % 64) == 0)
			fprintf(fp,"\n");
	}
	if((tot % 64) != 0)
		fprintf(fp,"\n");
}
/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000: 30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f  0123456789:;<=>?
 */
static void
fmtline(fp,addr,buf,len)
FILE *fp;
uint16 addr;
char *buf;
uint16 len;
{
	char line[80];
	register char *aptr,*cptr;
	register char c;

	memset(line,' ',sizeof(line));
	ctohex(line,(uint16)hibyte(addr));
	ctohex(line+2,(uint16)lobyte(addr));
	aptr = &line[6];
	cptr = &line[55];
	while(len-- != 0){
		c = *buf++;
		ctohex(aptr,(uint16)uchar(c));
		aptr += 3;
		*cptr++ = isprint(uchar(c)) ? c : '.';
	}
	*cptr++ = '\n';
	fwrite(line,1,(unsigned)(cptr-line),fp);
}
/* Convert byte to two ascii-hex characters */
static void
ctohex(buf,c)
register char *buf;
register uint16 c;
{
	static char hex[] = "0123456789abcdef";

	*buf++ = hex[hinibble(c)];
	*buf = hex[lonibble(c)];
}

/* Modify or displace interface trace flags */
int
dotrace(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	struct tracecmd *tp;

	if(argc < 2){
		for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next)
			showtrace(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULLIF){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showtrace(ifp);
		return 0;
	}
	/* MODIFY THIS TO HANDLE MULTIPLE OPTIONS */
	if(argc >= 3){
		for(tp = Tracecmd;tp->name != NULLCHAR;tp++)
			if(strncmp(tp->name,argv[2],strlen(argv[2])) == 0)
				break;
		if(tp->name != NULLCHAR)
			ifp->trace = (ifp->trace & ~tp->mask) | tp->val;
		else
			ifp->trace = htoi(argv[2]);
	}
	if(ifp->trfp != NULLFILE && ifp->trfp != stdout){
		/* Close existing trace file */
		fclose(ifp->trfp);
	}
	ifp->trfp = stdout;
	if(argc >= 4){
		if((ifp->trfp = fopen(argv[3],APPEND_TEXT)) == NULLFILE){
			printf("Can't write to %s\n",argv[3]);
			ifp->trfp = stdout;
		}
	}
	showtrace(ifp);
	return 0;
}
/* Display the trace flags for a particular interface */
static void
showtrace(ifp)
register struct iface *ifp;
{
	if(ifp == NULLIF)
		return;
	printf("%s:",ifp->name);
	if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT | IF_TRACE_RAW)){
		if(ifp->trace & IF_TRACE_IN)
			printf(" input");
		if(ifp->trace & IF_TRACE_OUT)
			printf(" output");

		if(ifp->trace & IF_TRACE_NOBC)
			printf(" - no broadcasts");

		if(ifp->trace & IF_TRACE_HEX)
			printf(" (Hex/ASCII dump)");
		else if(ifp->trace & IF_TRACE_ASCII)
			printf(" (ASCII dump)");
		else
			printf(" (headers only)");

		if(ifp->trace & IF_TRACE_RAW)
			printf(" Raw output");

		printf("\n");
	} else
		printf(" tracing off\n");
}

/* shut down all trace files */
void
shuttrace()
{
	struct iface *ifp;

	for(ifp = Ifaces; ifp != NULLIF; ifp = ifp->next){
		if(ifp->trfp != NULLFILE && ifp->trfp != stdout)
			fclose(ifp->trfp);
		ifp->trfp = NULLFILE;
	}
}

/* Log messages of the form
 * Tue Jan 31 00:00:00 1987 44.64.0.7:1003 open FTP
 */
void
trace_log(struct iface *ifp,char *fmt, ...)
{
	va_list ap;
	char *cp;
	long t;
	FILE *fp;

	if((fp = ifp->trfp) == NULLFILE)
		return;
	t = secclock();
	cp = ctime(&t);
	rip(cp);
	fprintf(fp,"%s - ",cp);
	va_start(ap,fmt);
	vfprintf(fp,fmt,ap);
	va_end(ap);
	fprintf(fp,"\n");
}
int
tprintf(struct iface *ifp,char *fmt, ...)
{
	va_list ap;
	int ret = 0;

	if(ifp->trfp == NULLFILE)
		return -1;
	va_start(ap,fmt);
	ret = vfprintf(ifp->trfp,fmt,ap);
	va_end(ap);
	return ret;
}
