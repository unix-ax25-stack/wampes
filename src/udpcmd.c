/* @(#) $Id: udpcmd.c,v 1.14 1999/01/22 21:20:07 deyke Exp $ */

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

static int doudpstat(int argc,char *argv[],void *p);

static struct cmds Udpcmds[] = {
	{ "status",       doudpstat,      0, 0,   NULL },
	{ NULL }
};
int
doudp(
int argc,
char *argv[],
void *p)
{
	return subcmd(Udpcmds,argc,argv,p);
}
int
st_udp(
struct udp_cb *udp,
int n)
{
	if(n == 0)
		printf("    &UCB Rcv-Q  Local socket\n");

	return printf("%08lx%6u  %s\n",(long)udp,udp->rcvcnt,pinet_udp(&udp->socket));
}

/* Dump UDP statistics and control blocks */
static int
doudpstat(
int argc,
char *argv[],
void *p)
{
	register struct udp_cb *udp;
	register int i;

    if(!Shortstatus){
	for(i=1;i<=NUMUDPMIB;i++){
		printf("(%2u)%-20s%10lu",i,
		 Udp_mib[i].name,Udp_mib[i].value.integer);
		if(i % 2)
			printf("     ");
		else
			printf("\n");
	}
	if((i % 2) == 0)
		printf("\n");
    }

	printf("    &UCB Rcv-Q  Local socket\n");
	for(udp = Udps;udp != NULL; udp = udp->next){
		if(st_udp(udp,1) == EOF)
			return 0;
	}
	return 0;
}
