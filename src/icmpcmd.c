/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/icmpcmd.c,v 1.3 1990/04/12 17:51:53 deyke Exp $ */

/* ICMP-related user commands */
#include <stdio.h>
#include <time.h>
#include "global.h"
#include "icmp.h"
#include "mbuf.h"
#include "netuser.h"
#include "internet.h"
#include "timer.h"
#include "ping.h"

int
doicmpstat()
{
	extern struct icmp_errors icmp_errors;
	extern struct icmp_stats icmp_stats;
	extern char *icmptypes[];
	register int i;

	printf("ICMP: chksum err %u no space %u icmp %u bdcsts %u\n",
	 icmp_errors.checksum,icmp_errors.nospace,icmp_errors.noloop,
	 icmp_errors.bdcsts);
	printf("type  rcvd  sent\n");
	for(i=0;i<ICMP_TYPES;i++){
		if(icmp_stats.input[i] == 0 && icmp_stats.output[i] == 0)
			continue;
		printf("%-6u%-6u%-6u",i,icmp_stats.input[i],
			icmp_stats.output[i]);
		if(icmptypes[i] != NULLCHAR)
			printf("  %s",icmptypes[i]);
		printf("\n");
	}
	return 0;
}

/* Hash table list heads */
struct ping *ping[PMOD];

/* Send ICMP Echo Request packets */
doping(argc,argv)
int argc;
char *argv[];
{
	int32 dest;
	struct ping *add_ping(),*pp1;
	register struct ping *pp;
	void ptimeout();
	int16 hval,hash_ping();
	int i;
	char *inet_ntoa();
	int16 len;

	if(argc < 2){
		printf("Host                Sent    Rcvd   %%   Srtt   Mdev  Length  Interval\n");
		for(i=0;i<PMOD;i++){
			for(pp = ping[i];pp != NULLPING;pp = pp->next){
				printf("%-16s",inet_ntoa(pp->target));
				printf("%8lu%8lu",pp->sent,pp->responses);
				printf("%4lu",
				 (long)pp->responses * 100 / pp->sent);
				printf("%7lu", pp->srtt);
				printf("%7lu", pp->mdev);
				printf("%8u", pp->len);
				printf("%10lu\n",
				 ((long)pp->timer.start * MSPTICK + 500) / 1000);
			}
		}
		return 0;
	}
	if(strcmp(argv[1],"clear") == 0){
		for(i=0;i<PMOD;i++){
			for(pp = ping[i];pp != NULLPING;pp = pp1){
				pp1 = pp->next;
				del_ping(pp);
			}
		}
		return 0;
	}
	if((dest = resolve(argv[1])) == 0){
		printf("Host %s unknown\n",argv[1]);
		return 1;
	}
	if(argc > 2) {
		len = atoi(argv[2]);
		if (len < ICMPLEN) {
			printf("packet size too small, minimum size is %d bytes\n", ICMPLEN);
			return 1;
		}
	} else
		len = 64;
	/* See if dest is already in table */
	hval = hash_ping(dest);
	for(pp = ping[hval]; pp != NULLPING; pp = pp->next){
		if(pp->target == dest){
			break;
		}
	}
	if(argc > 3){
		/* Inter-ping time is specified; set up timer structure */
		if(pp == NULLPING)
			pp = add_ping(dest);
		set_timer(&pp->timer, atoi(argv[3]) * 1000l);
		pp->timer.func = ptimeout;
		pp->timer.arg = (char *)pp;
		pp->target = dest;
		pp->len = len;
		start_timer(&pp->timer);
		pingem(dest,(int16)pp->sent++,REPEAT,len);
	} else
		pingem(dest,0,ONESHOT,len);

	return 0;
}

/* Called by ping timeout */
void
ptimeout(p)
char *p;
{
	register struct ping *pp;

	/* Send another ping */
	pp = (struct ping *)p;
	pingem(pp->target,(int16)pp->sent++,REPEAT,pp->len);
	start_timer(&pp->timer);
}
/* Send ICMP Echo Request packet */
static int
pingem(target,seq,id,len)
int32 target;   /* Site to be pinged */
int16 seq;      /* ICMP Echo Request sequence number */
int16 id;       /* ICMP Echo Request ID */
int16 len;      /* Length of data field */
{
	struct mbuf *data;
	struct mbuf *bp,*htonicmp();
	struct icmp icmp;
	extern struct icmp_stats icmp_stats;
	int i;
	struct timeval tv;
	struct timezone tz;

	if ((data = alloc_mbuf(len)) == NULLBUF)
		return -1;
	for (i = ICMPLEN; i < len; i++)
		data->data[i] = i;
	data->data += ICMPLEN;
	data->cnt = len - ICMPLEN;
	/* Insert timestamp and build ICMP header */
	if (len >= ICMPLEN + sizeof(struct timeval)) {
		gettimeofday(&tv, &tz);
		memcpy(data->data, (char *) &tv, sizeof(struct timeval));
	}
	icmp_stats.output[ECHO]++;
	icmp.type = ECHO;
	icmp.code = 0;
	icmp.args.echo.seq = seq;
	icmp.args.echo.id = id;
	if((bp = htonicmp(&icmp,data)) == NULLBUF){
		free_p(data);
		return 0;
	}
	return ip_send(ip_addr,target,ICMP_PTCL,0,0,bp,len,0,0);
}

/* Called with incoming Echo Reply packet */
echo_proc(source,dest,icmp,bp)
int32 source;
int32 dest;
struct icmp *icmp;
struct mbuf *bp;
{
	register struct ping *pp;
	int16 hval,hash_ping();
	char *inet_ntoa();
	int32 rtt,abserr;
	struct timeval tv,timestamp;
	struct timezone tz;

	/* Get stamp */
	if(pullup(&bp,(char *)&timestamp,sizeof(timestamp))
	 != sizeof(timestamp)){
		/* The timestamp is missing! */
		rtt = -1;
	} else {
		gettimeofday(&tv, &tz);
		rtt = (tv.tv_sec  - timestamp.tv_sec ) * 1000 +
		      (tv.tv_usec - timestamp.tv_usec) / 1000;
	}
	free_p(bp);

	hval = hash_ping(source);
	for(pp = ping[hval]; pp != NULLPING; pp = pp->next)
		if(pp->target == source)
			break;
	if(pp == NULLPING || icmp->args.echo.id != REPEAT){
		printf(
		  (rtt == -1) ?
		    "%s: echo reply id %u seq %u\n" :
		    "%s: echo reply id %u seq %u, %lu ms\n",
		  inet_ntoa(source),
		  icmp->args.echo.id,icmp->args.echo.seq,
		  (long)rtt);
		fflush(stdout);
	} else {
		/* Repeated poll, just keep stats */
		pp->responses++;
		if (rtt == -1)
			return;
		if(pp->responses == 1){
			/* First response, base entire SRTT on it */
			pp->srtt = rtt;
		} else {
			abserr = rtt > pp->srtt ? rtt - pp->srtt : pp->srtt - rtt;
			pp->srtt = (7*pp->srtt + rtt + 4) >> 3;
			pp->mdev = (3*pp->mdev + abserr + 2) >> 2;
		}
	}
}
static
int16
hash_ping(dest)
int32 dest;
{
	int16 hval;

	hval = (hiword(dest) ^ loword(dest)) % PMOD;
	return hval;
}
/* Add entry to ping table */
static
struct ping *
add_ping(dest)
int32 dest;
{
	struct ping *pp;
	int16 hval,hash_ping();

	pp = (struct ping *)calloc(1,sizeof(struct ping));
	if(pp == NULLPING)
		return NULLPING;

	hval = hash_ping(dest);
	pp->prev = NULLPING;
	pp->next = ping[hval];
	if(pp->next != NULLPING)
		pp->next->prev = pp;
	ping[hval] = pp;
	return pp;
}
/* Delete entry from ping table */
static
del_ping(pp)
struct ping *pp;
{
	int16 hval,hash_ping();

	stop_timer(&pp->timer);

	if(pp->next != NULLPING)
		pp->next->prev = pp->prev;
	if(pp->prev != NULLPING) {
		pp->prev->next = pp->next;
	} else {
		hval = hash_ping(pp->target);
		ping[hval] = pp->next;
	}
	free((char *)pp);
}

