/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/iface.c,v 1.2 1990/08/23 17:33:05 deyke Exp $ */

#include "global.h"
#include "iface.h"

struct iface *
if_lookup(name)
char *name;
{
	register struct iface *iface;

	for(iface = Ifaces; iface != NULLIF; iface = iface->next)
		if(strcmp(iface->name,name) == 0)
			break;
	return iface;
}

/* Return iface pointer if 'addr' belongs to one of our interfaces,
 * NULLIF otherwise.
 * This is used to tell if an incoming IP datagram is for us, or if it
 * has to be routed.
 */
struct iface *
ismyaddr(addr)
int32 addr;
{
	extern int32 Ip_addr;

	if(addr == Ip_addr)
		return Ifaces;
	return 0;
}

/* Given a network mask, return the number of contiguous 1-bits starting
 * from the most significant bit.
 */
static int
mask2width(mask)
int32 mask;
{
	int width,i;

	width = 0;
	for(i = 31;i >= 0;i--){
		if(!(mask & (1L << i)))
			break;
		width++;
	}
	return width;
}

/* Divert output packets from one interface to another. Useful for ARP
 * and digipeat frames coming in from receive-only interfaces
 */
doforward(argc,argv)
int argc;
char *argv[];
{
	struct iface *iface,*iface1;

	if(argc < 2){
		for(iface = Ifaces; iface != NULLIF; iface = iface->next){
			if(iface->forw != NULLIF){
				printf("%s -> %s\n",iface->name,iface->forw->name);
			}
		}
		return 0;
	}
	if((iface = if_lookup(argv[1])) == NULLIF){
		printf("Interface %s unknown\n",argv[1]);
		return 1;
	}
	if(argc < 3){
		if(iface->forw == NULLIF)
			printf("%s not forwarded\n",iface->name);
		else
			printf("%s -> %s\n",iface->name,iface->forw->name);
		return 0;
	}
	if((iface1 = if_lookup(argv[2])) == NULLIF){
		printf("Interface %s unknown\n",argv[2]);
		return 1;
	}
	if(iface1 == iface){
		/* Forward to self means "turn forwarding off" */
		iface->forw = NULLIF;
	} else {
		if(iface1->output != iface->output)
			printf("Warning: Interfaces of different type\n");
		iface->forw = iface1;
	}
	return 0;
}
