/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/arpcmd.c,v 1.4 1990/10/12 19:25:08 deyke Exp $ */

#include <stdio.h>
#include <ctype.h>
#include "global.h"
#include "mbuf.h"
#include "timer.h"
#include "enet.h"
#include "ax25.h"
#include "arp.h"
#include "netuser.h"
#include "cmdparse.h"
#include "commands.h"

static int doarpadd __ARGS((int argc,char *argv[],void *p));
static int doarpdrop __ARGS((int argc,char *argv[],void *p));
static int doarpflush __ARGS((int argc,char *argv[],void *p));
static void dumparp __ARGS((void));

static struct cmds Arpcmds[] = {
	"add", doarpadd, 0, 4,
	"arp add <hostid> ether|ax25|netrom <ether addr|callsign>",

	"drop", doarpdrop, 0, 3,
	"arp drop <hostid> ether|ax25|netrom",

	"flush", doarpflush, 0, 0,
	NULLCHAR,

	"publish", doarpadd, 0, 4,
	"arp publish <hostid> ether|ax25|netrom <ether addr|callsign>",

	NULLCHAR,
};
char *Arptypes[] = {
	"NET/ROM",
	"10 Mb Ethernet",
	"3 Mb Ethernet",
	"AX.25",
	"Pronet",
	"Chaos",
	"IEEE 802",
	"Arcnet",
	"Appletalk"
};

int
doarp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	if(argc < 2){
		dumparp();
		return 0;
	}
	return subcmd(Arpcmds,argc,argv,p);
}
static
doarpadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int16 hardware,hwalen,naddr = 1;
	int32 addr;
	char *hwaddr;
	struct arp_tab *ap;
	struct arp_type *at;
	int pub = 0;

	if(argv[0][0] == 'p')   /* Is this entry published? */
		pub = 1;
	if((addr = resolve(argv[1])) == 0){
		tprintf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':       /* Net/Rom pseudo-type */
		hardware = ARP_NETROM;
		naddr = argc - 3 ;
		if (naddr != 1) {
			tprintf("No digipeaters in NET/ROM arp entries - ") ;
			return 1 ;
		}
		break;
	case 'e':       /* "ether" */
		hardware = ARP_ETHER;
		break;
	case 'a':       /* "ax25" */
		hardware = ARP_AX25;
		naddr = argc - 3;
		break;
	case 'm':       /* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		tprintf("unknown hardware type \"%s\"\n",argv[2]);
		return -1;
	}
	/* If an entry already exists, clear it */
	if((ap = arp_lookup(hardware,addr)) != NULLARP)
		arp_drop(ap);

	at = &Arp_type[hardware];
	if(at->scan == NULLFP){
		tprintf("Attach device first\n");
		return 1;
	}
	/* Allocate buffer for hardware address and fill with remaining args */
	hwalen = at->hwalen * naddr;
	hwaddr = mallocw(hwalen);
	/* Destination address */
	(*at->scan)(hwaddr,&argv[3],argc - 3);
	ap = arp_add(addr,hardware,hwaddr,hwalen,pub);  /* Put in table */
	free(hwaddr);                           /* Clean up */
	stop_timer(&ap->timer);                 /* Make entry permanent */
	ap->timer.count = ap->timer.start = 0;
	return 0;
}
/* Remove an ARP entry */
static
doarpdrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	int16 hardware;
	int32 addr;
	struct arp_tab *ap;

	if((addr = resolve(argv[1])) == 0){
		tprintf(Badhost,argv[1]);
		return 1;
	}
	/* This is a kludge. It really ought to be table driven */
	switch(tolower(argv[2][0])){
	case 'n':
		hardware = ARP_NETROM;
		break;
	case 'e':       /* "ether" */
		hardware = ARP_ETHER;
		break;
	case 'a':       /* "ax25" */
		hardware = ARP_AX25;
		break;
	case 'm':       /* "mac appletalk" */
		hardware = ARP_APPLETALK;
		break;
	default:
		hardware = 0;
		break;
	}
	if((ap = arp_lookup(hardware,addr)) == NULLARP)
		return -1;
	arp_drop(ap);
	return 0;
}
/* Flush all automatic entries in the arp cache */
static int
doarpflush(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct arp_tab *ap;
	struct arp_tab *aptmp;
	int i;

	for(i=0;i<ARPSIZE;i++){
		for(ap = Arp_tab[i];ap != NULLARP;ap = aptmp){
			aptmp = ap->next;
			if(ap->timer.start != 0)
				arp_drop(ap);
		}
	}
	return 0;
}

/* Dump ARP table */
static void
dumparp()
{
	register int i;
	register struct arp_tab *ap;
	char e[128];

	arp_loadfile();
	tprintf("received %u badtype %u bogus addr %u reqst in %u replies %u reqst out %u\n",
	 Arp_stat.recv,Arp_stat.badtype,Arp_stat.badaddr,Arp_stat.inreq,
	 Arp_stat.replies,Arp_stat.outreq);

	tprintf("IP addr         Type           Time Q Addr\n");
	for(i=0;i<ARPSIZE;i++){
		for(ap = Arp_tab[i];ap != (struct arp_tab *)NULL;ap = ap->next){
			tprintf("%-16s",inet_ntoa(ap->ip_addr));
			tprintf("%-15s",smsg(Arptypes,NHWTYPES,ap->hardware));
			tprintf("%-5ld",read_timer(&ap->timer)*(long)MSPTICK/1000);
			if(ap->state == ARP_PENDING)
				tprintf("%-2u",len_q(ap->pending));
			else
				tprintf("  ");
			if(ap->state == ARP_VALID){
				if(Arp_type[ap->hardware].format != NULL){
					(*Arp_type[ap->hardware].format)(e,ap->hw_addr);
				} else {
					e[0] = '\0';
				}
				tprintf("%s",e);
			} else {
				tprintf("[unknown]");
			}
			if(ap->pub)
				tprintf(" (published)");
			if(tprintf("\n") == EOF)
				return;
		}
	}
	return;
}
