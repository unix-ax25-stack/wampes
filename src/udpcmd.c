/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/udpcmd.c,v 1.5 1991/02/24 20:18:00 deyke Exp $ */

/* UDP-related user commands
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "udp.h"
#include "internet.h"
#include "cmdparse.h"
#include "commands.h"

static int doudpstat __ARGS((int argc,char *argv[],void *p));

static struct cmds Udpcmds[] = {
	"status",       doudpstat,      0, 0,   NULLCHAR,
	NULLCHAR,
};
int
doudp(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(Udpcmds,argc,argv,p);
}
int
st_udp(udp,n)
struct udp_cb *udp;
int n;
{
	if(n == 0)
		tprintf("    &UCB Rcv-Q  Local socket\n");

	return tprintf("%8lx%6u  %s\n",ptol(udp),udp->rcvcnt,pinet_udp(&udp->socket));
}

/* Dump UDP statistics and control blocks */
static int
doudpstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct udp_cb *udp;
	register int i;

    if(!Shortstatus){
	for(i=1;i<=NUMUDPMIB;i++){
		tprintf("(%2u)%-20s%10lu",i,
		 Udp_mib[i].name,Udp_mib[i].value.integer);
		if(i % 2)
			tprintf("     ");
		else
			tprintf("\n");
	}
	if((i % 2) == 0)
		tprintf("\n");
    }

	tprintf("    &UCB Rcv-Q  Local socket\n");
	for(i=0;i<NUDP;i++){
		for(udp = Udps[i];udp != NULLUDP; udp = udp->next){
			if(st_udp(udp,1) == EOF)
				return 0;
		}
	}
	return 0;
}
