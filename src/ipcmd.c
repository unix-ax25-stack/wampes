/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/ipcmd.c,v 1.2 1990/08/23 17:33:11 deyke Exp $ */

/* IP-related user commands */
#include <stdio.h>
#include "global.h"
#include "mbuf.h"
#include "internet.h"
#include "timer.h"
#include "netuser.h"
#include "iface.h"
#include "ip.h"
#include "cmdparse.h"

int doipaddr(),doipstat(),dottl();
struct cmds ipcmds[] = {
	"address",      doipaddr,       0, 0,      NULLCHAR,
	"status",       doipstat,       0, 0,      NULLCHAR,
	"ttl",          dottl,          0, 0,      NULLCHAR,
	NULLCHAR,       NULLFP,         0, 0,      "ip subcommands: address status ttl"
};
doip(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	return subcmd(ipcmds,argc,argv,p);
}
int
doipaddr(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *inet_ntoa();
	int32 n;

	if(argc < 2) {
		printf("%s\n",inet_ntoa(Ip_addr));
	} else if((n = resolve(argv[1])) == 0){
		printf(Badhost,argv[1]);
		return 1;
	} else
		Ip_addr = n;
	return 0;
}
int
dottl(argc,argv,p)
char *argv[];
void *p;
{
	return setlong(&ip_ttl,"IP Time-to-live",argc,argv);
}

/* "route" subcommands */
int doadd(),dodrop();
static struct cmds rtcmds[] = {
	"add", doadd, 0, 3,
	"route add <dest addr>[/<bits>] <if name> [gateway] [metric]",

	"drop", dodrop, 0, 2,
	"route drop <dest addr>[/<bits>]",

	NULLCHAR, NULLFP, 0, 0,
	"route subcommands: add, drop"
};

/* Display and/or manipulate routing table */
int
doroute(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	route_loadfile();
	if(argc < 2){
		dumproute();
		return 0;
	}
	return subcmd(rtcmds,argc,argv,p);
}
/* Add an entry to the routing table
 * E.g., "add 1.2.3.4 ax0 5.6.7.8 3"
 */
int
doadd(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	struct iface *ifp;
	int32 dest,gateway;
	unsigned bits;
	char *bitp;
	int metric;

	if(strcmp(argv[1],"default") == 0){
		dest = 0;
		bits = 0;
	} else {
		if((dest = resolve(argv[1])) == 0){
			printf(Badhost,argv[1]);
			return 1;
		}

		/* If IP address is followed by an optional slash and
		 * a length field, (e.g., 128.96/16) get it;
		 * otherwise assume a full 32-bit address
		 */
		if((bitp = strchr(argv[1],'/')) != NULLCHAR){
			bitp++;
			bits = atoi(bitp);
		} else
			bits = 32;
	}
	for(ifp=Ifaces;ifp != NULLIF;ifp = ifp->next){
		if(strcmp(argv[2],ifp->name) == 0)
			break;
	}
	if(ifp == NULLIF){
		printf("Interface \"%s\" unknown\n",argv[2]);
		return 1;
	}
	if(argc > 3){
		if((gateway = resolve(argv[3])) == 0){
			printf(Badhost,argv[3]);
			return 1;
		}
	} else {
		gateway = 0;
	}
	if(argc > 4)
		metric = atoi(argv[4]);
	else
		metric = 0;

	rt_add(dest,bits,gateway,metric,ifp);
	return 0;
}
/* Drop an entry from the routing table
 * E.g., "drop 128.96/16
 */
int
dodrop(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	char *bitp;
	unsigned bits;
	int32 n;

	if(strcmp(argv[1],"default") == 0){
		n = 0;
		bits = 0;
	} else {
		/* If IP address is followed by an optional slash and length field,
		 * (e.g., 128.96/16) get it; otherwise assume a full 32-bit address
		 */
		if((bitp = strchr(argv[1],'/')) != NULLCHAR){
			bitp++;
			bits = atoi(bitp);
		} else
			bits = 32;

		if((n = resolve(argv[1])) == 0){
			printf(Badhost,argv[1]);
			return 1;
		}
	}
	return rt_drop(n,bits);
}

/* Dump IP routing table
 * Dest              Length    Interface    Gateway          Metric
 * 192.001.002.003   32        sl0          192.002.003.004       4
 */
int
dumproute()
{
	register unsigned int i,bits;
	register struct route *rp;

	printf("Dest              Length    Interface    Gateway          Metric\n");
	if(R_default.iface != NULLIF){
		printf("default           0         %-13s",
		 R_default.iface->name);
		if(R_default.gateway != 0)
			printf("%-17s",inet_ntoa(R_default.gateway));
		else
			printf("%-17s","");
		printf("%6u\n",R_default.metric);
	}
	for(bits=1;bits<=32;bits++){
		for(i=0;i<NROUTE;i++){
			for(rp = Routes[bits-1][i];rp != NULLROUTE;rp = rp->next){
				printf("%-18s",inet_ntoa(rp->target));
				printf("%-10u",bits);
				printf("%-13s",rp->iface->name);
				if(rp->gateway != 0)
					printf("%-17s",inet_ntoa(rp->gateway));
				else
					printf("%-17s","");
				printf("%6u\n",rp->metric);
			}
		}
	}
	return 0;
}

int
doipstat(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	extern struct ip_stats Ip_stats;
	extern struct reasm *Reasmq;
	register struct reasm *rp;
	register struct frag *fp;
	char *inet_ntoa();

	printf("IP: total %ld runt %u len err %u vers err %u",
		Ip_stats.total,Ip_stats.runt,Ip_stats.length,Ip_stats.version);
	printf(" chksum err %u badproto %u\n",
		Ip_stats.checksum,Ip_stats.badproto);

	if(Reasmq != NULLREASM)
		printf("Reassembly fragments:\n");
	for(rp = Reasmq;rp != NULLREASM;rp = rp->next){
		printf("src %s",inet_ntoa(rp->source));
		printf(" dest %s",inet_ntoa(rp->dest));
		printf(" id %u pctl %u time %lu len %u\n",
			rp->id,uchar(rp->protocol),read_timer(&rp->timer),
			rp->length);
		for(fp = rp->fraglist;fp != NULLFRAG;fp = fp->next){
			printf(" offset %u last %u\n",fp->offset,fp->last);
		}
	}
	doicmpstat(argc,argv,p);
	return 0;
}

