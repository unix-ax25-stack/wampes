/* @(#) $Id: ripcmd.c,v 1.10 1999/01/22 21:20:07 deyke Exp $ */

/* RIP-related user commands
 *   Al Broscious, N3FCT
 *   Phil Karn, KA9Q
 */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "cmdparse.h"
#include "timer.h"
#include "iface.h"
#include "udp.h"
#include "rip.h"
#include "commands.h"

struct cmds Ripcmds[] = {
	{ "accept",       dodroprefuse,   0,      2,
		"rip accept <gateway> " },
	{ "add",          doripadd,       0,      3,
		"rip add <dest> <interval> [<flags>]" },
	{ "allow",        doripallow,     0,      2,
		"rip allow <gateway>" },
	{ "drop",         doripdrop,      0,      2,
		"rip drop <dest>" },
	{ "merge",        doripmerge,     0,      0,      NULL },
	{ "noallow",      doripnoallow,   0,      2,
		"rip noallow <gateway>" },
	{ "promiscuous",  dorippromisc,   0,      0,      NULL },
	{ "refuse",       doaddrefuse,    0,      2,
		"rip refuse <gateway>" },
	{ "request",      doripreq,       0,      2,      NULL },
	{ "status",       doripstat,      0,      0,      NULL },
	{ "trace",        doriptrace,     0,      0,      NULL },
	{ NULL }
};

int
dorip(
int argc,
char *argv[],
void *p)
{
	return subcmd(Ripcmds,argc,argv,p);
}

/* Add an entry to the RIP output list */
int
doripadd(
int argc,
char *argv[],
void *p)
{
	int split = 1;
	int us = 0;

	if(argc > 3)
		split = atoi(argv[3]);
	if(argc > 4)
		us = atoi(argv[4]);

	return rip_add(resolve(argv[1]),atol(argv[2]),split,us);
}

/* Add an entry to the RIP refuse list */
int
doaddrefuse(
int argc,
char *argv[],
void *p)
{
	return riprefadd(resolve(argv[1]));
}

/* Add a gateway to the list we accept updates from.  Not needed for a
 * gateway reached over an interface named in "rip add" - those are accepted
 * anyway - but for one that sends to us without us sending to it.
 */
int
doripallow(
int argc,
char *argv[],
void *p)
{
	return ripallowadd(resolve(argv[1]));
}

/* Drop a gateway from that list */
int
doripnoallow(
int argc,
char *argv[],
void *p)
{
	return ripallowdrop(resolve(argv[1]));
}

/* Take updates from anyone, as before this was configurable */
int
dorippromisc(
int argc,
char *argv[],
void *p)
{
	return setbool(&Rip_promiscuous,"RIP promiscuous learning",argc,argv);
}

/* Drop an entry from the RIP output list */
int
doripdrop(
int argc,
char *argv[],
void *p)
{
	return rip_drop(resolve(argv[1]));
}

/* Drop an entry from the RIP refuse list */
int
dodroprefuse(
int argc,
char *argv[],
void *p)
{
	return riprefdrop(resolve(argv[1]));
}

/* Initialize the RIP listener */
int
doripinit(
int argc,
char *argv[],
void *p)
{
	return rip_init();
}
int
doripstop(
int argc,
char *argv[],
void *p)
{
	del_udp(&Rip_cb);
	return 0;
}
int
doripreq(
int argc,
char *argv[],
void *p)
{
	uint replyport;

	replyport = (argc > 2) ? atoi(argv[2]) : RIP_PORT;
	return ripreq(resolve(argv[1]),replyport);
}
/* Dump RIP statistics */
int
doripstat(
int argc,
char *argv[],
void *p)
{
	struct rip_allow *ral;
	struct rip_list *rl;
	struct rip_refuse *rfl;

	printf("RIP: sent %lu rcvd %lu reqst %lu resp %lu unk %lu refused %lu\n",
	 (unsigned long)Rip_stat.output,(unsigned long)Rip_stat.rcvd,
	 (unsigned long)Rip_stat.request,(unsigned long)Rip_stat.response,
	 (unsigned long)Rip_stat.unknown,(unsigned long)Rip_stat.refusals);
	if(Rip_list != NULL){
		printf("Active RIP output interfaces:\n");
		printf("Dest Addr       Interval Split\n");
		for(rl=Rip_list; rl != NULL; rl = rl->next){
			printf("%-16s%-9lu%-6u\n",inet_ntoa(rl->dest),
			 (unsigned long)rl->interval,rl->flags.rip_split);
		}
	}
	if(Rip_refuse != NULL){
		printf("Refusing announcements from gateways:\n");
		for(rfl=Rip_refuse; rfl != NULL;rfl = rfl->next){
			printf("%s\n",inet_ntoa(rfl->target));
		}
	}
	if(Rip_promiscuous){
		printf("Learning from any gateway (rip promiscuous is on)\n");
	} else {
		printf("Learning from gateways on the interfaces listed above");
		if(Rip_allow != NULL){
			printf(", and from:\n");
			for(ral=Rip_allow; ral != NULL; ral = ral->next){
				printf("%s\n",inet_ntoa(ral->target));
			}
		} else {
			printf(" only\n");
		}
		printf("%lu update%s rejected\n",
		 (unsigned long)Rip_stat.unauthorized,
		 Rip_stat.unauthorized == 1 ? "" : "s");
	}
	return 0;
}

int
doriptrace(
int argc,
char *argv[],
void *p)
{
	return setint((int *) &Rip_trace,"RIP tracing",argc,argv);
}
int
doripmerge(
int argc,
char *argv[],
void *p)
{
	return setbool(&Rip_merge,"RIP merging",argc,argv);
}
