/* @(#) $Id: axheard.c,v 1.7 1999/02/01 22:24:25 deyke Exp $ */

/* AX25 link callsign monitoring. Also contains beginnings of
 * an automatic link quality monitoring scheme (incomplete)
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ax25.h"
#include "ip.h"
#include "timer.h"

static struct lq *al_create(struct iface *ifp,uint8 *addr);
static struct ld *ad_lookup(struct iface *ifp,uint8 *addr,int sort);
static struct ld *ad_create(struct iface *ifp,uint8 *addr);
struct lq *Lq;
struct ld *Ld;

/* Log the source address of an incoming packet */
void
logsrc(
struct iface *ifp,
uint8 *addr)
{
	struct lq *lp;

	if((lp = al_lookup(ifp,addr,1)) == NULL
	 && (lp = al_create(ifp,addr)) == NULL)
		return;
	lp->currxcnt++;
	lp->time = secclock();
}
/* Log the destination address of an incoming packet */
void
logdest(
struct iface *ifp,
uint8 *addr)
{
	struct ld *lp;

	if((lp = ad_lookup(ifp,addr,1)) == NULL
	 && (lp = ad_create(ifp,addr)) == NULL)
		return;
	lp->currxcnt++;
	lp->time = secclock();
}
/* Look up an entry in the source data base */
struct lq *
al_lookup(
struct iface *ifp,
uint8 *addr,
int sort)
{
	struct lq *lp;
	struct lq *lplast = NULL;

	for(lp = Lq;lp != NULL;lplast = lp,lp = lp->next){
		if(addreq(lp->addr,addr) && lp->iface == ifp){
			if(sort && lplast != NULL){
				/* Move entry to top of list */
				lplast->next = lp->next;
				lp->next = Lq;
				Lq = lp;
			}
			return lp;
		}
	}
	return NULL;
}
/* Create a new entry in the source database */
static struct lq *
al_create(
struct iface *ifp,
uint8 *addr)
{
	struct lq *lp;

	lp = (struct lq *)callocw(1,sizeof(struct lq));
	memcpy(lp->addr,addr,AXALEN);
	lp->next = Lq;
	Lq = lp;
	lp->iface = ifp;

	return lp;
}
/* Look up an entry in the destination database */
static struct ld *
ad_lookup(
struct iface *ifp,
uint8 *addr,
int sort)
{
	struct ld *lp;
	struct ld *lplast = NULL;

	for(lp = Ld;lp != NULL;lplast = lp,lp = lp->next){
		if(lp->iface == ifp && addreq(lp->addr,addr)){
			if(sort && lplast != NULL){
				/* Move entry to top of list */
				lplast->next = lp->next;
				lp->next = Ld;
				Ld = lp;
			}
			return lp;
		}
	}
	return NULL;
}
/* Create a new entry in the destination database */
static struct ld *
ad_create(
struct iface *ifp,
uint8 *addr)
{
	struct ld *lp;

	lp = (struct ld *)callocw(1,sizeof(struct ld));
	memcpy(lp->addr,addr,AXALEN);
	lp->next = Ld;
	Ld = lp;
	lp->iface = ifp;

	return lp;
}

