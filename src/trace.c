/* @(#) $Id: trace.c,v 1.24 1999/01/22 21:20:07 deyke Exp $ */

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
#include "commands.h"
#include "trace.h"
#include "session.h"
#include "timer.h"

static void ascii_dump(FILE *fp,struct mbuf **bpp);
static void ctohex(char *buf,uint c);
static void fmtline(FILE *fp,uint addr,uint8 *buf,uint len);
void hex_dump(FILE *fp,struct mbuf **bpp);
static void showtrace(struct iface *ifp);

/* Redefined here so that programs calling dump in the library won't pull
 * in the rest of the package
 */
static char nospace[] = "No space!!\n";

/* THE WORDS, and the order of the two numbers matters: the SECOND is what the
 * word sets and the THIRD is what it clears.  "-input" sets 0 and clears IN,
 * which is the whole meaning of the dash.
 *
 * "ascii" and "hex" are the two halves of one bit pair, so each clears the
 * other: which one is wanted is a choice, not something to have both of.  The
 * pair is not cosmetic, because dump() looks at IF_TRACE_ASCII FIRST and only
 * falls through to IF_TRACE_HEX when ASCII is not set - with both bits set the
 * ASCII dump wins, so a word that meant "hex" would deliver ASCII.
 *
 * "-hex" cleared nothing and set ASCII, which is the same mistake from the
 * other side: it turned a hex trace into an ASCII one instead of off.
 */
struct tracecmd Tracecmd[] = {
	{ "input",        IF_TRACE_IN,    IF_TRACE_IN },
	{ "-input",       0,              IF_TRACE_IN },
	{ "output",       IF_TRACE_OUT,   IF_TRACE_OUT },
	{ "-output",      0,              IF_TRACE_OUT },
	{ "broadcast",    0,              IF_TRACE_NOBC },
	{ "-broadcast",   IF_TRACE_NOBC,  IF_TRACE_NOBC },
	{ "raw",          IF_TRACE_RAW,   IF_TRACE_RAW },
	{ "-raw",         0,              IF_TRACE_RAW },
	{ "ascii",        IF_TRACE_ASCII, IF_TRACE_ASCII|IF_TRACE_HEX },
	{ "-ascii",       0,              IF_TRACE_ASCII|IF_TRACE_HEX },
	{ "hex",          IF_TRACE_HEX,   IF_TRACE_ASCII|IF_TRACE_HEX },
	{ "-hex",         0,              IF_TRACE_HEX },
	{ "off",          0,              0xffff },
	{ NULL,   0,              0 }
};

void
dump(
struct iface *ifp,
int direction,
struct mbuf *bp
){
	struct mbuf *tbp;
	uint size;
	time_t timer;
	char *cp;
	struct iftype *ift;
	FILE *fp;

	if(ifp == NULL || (ifp->trace & direction) == 0
	 || (fp = ifp->trfp) == NULL)
		return; /* Nothing to trace */

	ift = ifp->iftype;
	switch(direction){
	case IF_TRACE_IN:
		if((ifp->trace & IF_TRACE_NOBC)
		 && ift != NULL
		 && (ift->addrtest != NULL)
		 && (*ift->addrtest)(ifp,bp) == 0)
			return;         /* broadcasts are suppressed */
		timer = (time_t) secclock();
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s recv:\n",cp,ifp->name);
		break;
	case IF_TRACE_OUT:
		timer = (time_t) secclock();
		cp = ctime(&timer);
		cp[24] = '\0';
		fprintf(fp,"\n%s - %s sent:\n",cp,ifp->name);
		break;
	}
	if(bp == NULL || (size = len_p(bp)) == 0){
		fprintf(fp,"empty packet!!\n");
		return;
	}
	dup_p(&tbp,bp,0,size);
	if(tbp == NULL){
		fprintf(fp,"%s",nospace);
		return;
	}
	if(ift != NULL && ift->trace != NULL)
		(*ift->trace)(fp,&tbp,1);
	if(ifp->trace & IF_TRACE_ASCII){
		/* Dump only data portion of packet in ascii */
		ascii_dump(fp,&tbp);
	} else if(ifp->trace & IF_TRACE_HEX){
		/* Dump entire packet in hex/ascii */
		free_p(&tbp);
		dup_p(&tbp,bp,0,len_p(bp));
		if(tbp != NULL)
			hex_dump(fp,&tbp);
		else
			fprintf(fp,"%s",nospace);
	}
	free_p(&tbp);
}

/* Dump packet bytes, no interpretation */
void
raw_dump(
struct iface *ifp,
int direction,
struct mbuf *bp)
{
	struct mbuf *tbp;
	FILE *fp;

	if((fp = ifp->trfp) == NULL)
		return;
	fprintf(fp,"\n******* raw packet dump (%s)\n",
	 ((direction & IF_TRACE_OUT) ? "send" : "recv"));
	dup_p(&tbp,bp,0,len_p(bp));
	if(tbp != NULL)
		hex_dump(fp,&tbp);
	else
		fprintf(fp,"%s",nospace);
	fprintf(fp,"*******\n");
	free_p(&tbp);
}

/* Dump an mbuf in hex */
void
hex_dump(
FILE *fp,
struct mbuf **bpp)
{
	uint n;
	uint address;
	uint8 buf[16];

	if(bpp == NULL || *bpp == NULL || fp == NULL)
		return;

	address = 0;
	while((n = pullup(bpp,buf,sizeof(buf))) != 0){
		fmtline(fp,address,buf,n);
		address += n;
	}
}
/* Dump an mbuf in ascii */
static void
ascii_dump(
FILE *fp,
struct mbuf **bpp)
{
	int c;
	uint tot;

	if(bpp == NULL || *bpp == NULL || fp == NULL)
		return;

	tot = 0;
	while((c = PULLCHAR(bpp)) != -1){
		if((tot % 64) == 0)
			fprintf(fp,"%04x  ",tot);
		putc(isprint(c) ? c : '.',fp);
		if((++tot % 64) == 0)
			fprintf(fp,"\n");
	}
	if((tot % 64) != 0)
		fprintf(fp,"\n");
}
/* Print a buffer up to 16 bytes long in formatted hex with ascii
 * translation, e.g.,
 * 0000  30 31 32 33 34 35 36 37 38 39 3a 3b 3c 3d 3e 3f      0123456789:;<=>?
 *                                  ^ the address is written as two bytes in
 *                                  network order, so a frame longer than 255
 *                                  bytes counts 0100, 0200 and not 100, 200
 *                                  (Thomas).
 */
static void
fmtline(
FILE *fp,
uint addr,
uint8 *buf,
uint len)
{
	char line[80];
	char *aptr,*cptr;
	uint8 c;

	memset(line,' ',sizeof(line));
	ctohex(line,(uint)hibyte(addr));
	ctohex(line+2,(uint)lobyte(addr));
	aptr = &line[6];
	cptr = &line[55];
	while(len-- != 0){
		c = *buf++;
		ctohex(aptr,(uint)c);
		aptr += 3;
		*cptr++ = isprint(c) ? c : '.';
	}
	*cptr++ = '\n';
	fwrite(line,1,(unsigned)(cptr-line),fp);
}
/* Convert byte to two ascii-hex characters */
static void
ctohex(
char *buf,
uint c)
{
	static char hex[] = "0123456789abcdef";

	*buf++ = hex[hinibble(c)];
	*buf = hex[lonibble(c)];
}

/* Modify or displace interface trace flags */
int
dotrace(
int argc,
char *argv[],
void *p)
{
	struct iface *ifp;
	struct tracecmd *tp;
	const char *path = 0;
	int i;

	if(argc < 2){
		for(ifp = Ifaces; ifp != NULL; ifp = ifp->next)
			showtrace(ifp);
		return 0;
	}
	if((ifp = if_lookup(argv[1])) == NULL){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc == 2){
		showtrace(ifp);
		return 0;
	}
	/* EVERY WORD COUNTS, and each is one of three things.
	 *
	 * "file=<path>" says where the trace goes.  It is the only way to say it,
	 * and it is new: as a bare word the file used to be whatever followed the
	 * FIRST option, so "trace foo in ascii output ascii" stored 0x0010, wrote
	 * the trace into a file called "ascii" in the node's directory, and threw
	 * "output" and the second "ascii" away without a word of complaint.  That
	 * is the worst of both: a command that looks like it says three things,
	 * says one, and leaves a file behind as evidence.
	 *
	 * A word from the table above sets and clears its bits.  A word beginning
	 * with a digit is a trace word read as HEX - htoi() has always done that,
	 * so "trace foo 111" stores 0x0111 and not 111: input, output and the
	 * ASCII dump, which is a coincidence that made the number look like it
	 * worked.  Said here because the next person to be surprised by it will
	 * read it here and not in a comment about ASCII.
	 *
	 * Anything else is refused by name, because a word that is neither is
	 * something the sysop meant and we did not understand, and silently
	 * ignoring it is how "the trace does not work" becomes a mystery.
	 */
	for(i = 2; i < argc; i++){
		if(!strncmp(argv[i],"file=",5)){
			path = argv[i] + 5;
			continue;
		}
		for(tp = Tracecmd;tp->name != NULL;tp++)
			if(strncmp(tp->name,argv[i],strlen(argv[i])) == 0)
				break;
		if(tp->name != NULL)
			ifp->trace = (ifp->trace & ~tp->mask) | tp->val;
		else if(isdigit((unsigned char) argv[i][0]))
			ifp->trace = htoi(argv[i]);
		else{
			printf("\"%s\" is not a trace option - "
			       "trace <iface> [input|output|ascii|hex|raw|broadcast|off]\n"
			       "                     [file=<path>]\n",argv[i]);
			return 1;
		}
	}
	if(ifp->trfp != NULL && ifp->trfp != stdout){
		/* Close existing trace file */
		fclose(ifp->trfp);
	}
	ifp->trfp = stdout;
	if(path != NULL){
		if((ifp->trfp = fopen(path,APPEND_TEXT)) == NULL){
			printf("Can't write to %s\n",path);
			ifp->trfp = stdout;
		}else{
			/* A trace file is something one watches while the node
			 * runs.  Block buffering would hold a session's worth
			 * of lines back and lose them outright if the node is
			 * killed, which is how a trace usually ends.
			 */
			setvbuf(ifp->trfp,NULL,_IOLBF,0);
		}
	}
	showtrace(ifp);
	return 0;
}
/* Display the trace flags for a particular interface */
static void
showtrace(struct iface *ifp)
{
	if(ifp == NULL)
		return;
	printf("%s:",ifp->name);
	if(ifp->trace & (IF_TRACE_IN | IF_TRACE_OUT | IF_TRACE_RAW)){
		if(ifp->trace & IF_TRACE_IN)
			printf(" input");
		if(ifp->trace & IF_TRACE_OUT)
			printf(" output");

		if(ifp->trace & IF_TRACE_NOBC)
			printf(" - no broadcasts");

		/* BOTH WORDS ARE TRUE OF A HALF-SET PAIR TOO, and saying only
		 * one of them is how "hex" came to deliver ASCII: dump() reads
		 * IF_TRACE_ASCII first, so ASCII wins when both are set.
		 */
		if((ifp->trace & IF_TRACE_ASCII) && (ifp->trace & IF_TRACE_HEX))
			printf(" (ASCII, so the HEX dump is not used)");
		else if(ifp->trace & IF_TRACE_HEX)
			printf(" (Hex/ASCII dump)");
		else if(ifp->trace & IF_TRACE_ASCII)
			printf(" (ASCII dump)");
		else
			printf(" (headers only)");

		if(ifp->trace & IF_TRACE_RAW)
			printf(" Raw output");
	} else
		printf(" tracing off");
	/* AND THE NUMBER, which is the only part of this that cannot be wrong
	 * about itself.  The words above cannot: a word that sets no direction
	 * stores a value that traces nothing at all - "trace foo hex" is 0x0200
	 * and not one line ever appears - and there is no way to see that from
	 * the words alone.  ifconfig <iface> verbose prints the same number.
	 */
	printf("  trace 0x%04x\n",(unsigned) ifp->trace);
}

/* shut down all trace files */
void
shuttrace(void)
{
	struct iface *ifp;

	for(ifp = Ifaces; ifp != NULL; ifp = ifp->next){
		if(ifp->trfp != NULL && ifp->trfp != stdout)
			fclose(ifp->trfp);
		ifp->trfp = NULL;
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

	if((fp = ifp->trfp) == NULL)
		return;
	t = secclock();
	cp = ctime((time_t *) &t);
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

	if(ifp->trfp == NULL)
		return -1;
	va_start(ap,fmt);
	ret = vfprintf(ifp->trfp,fmt,ap);
	va_end(ap);
	return ret;
}
