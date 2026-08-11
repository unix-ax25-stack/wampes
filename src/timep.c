/* @(#) $Id: timep.c,v 1.8 2006/02/12 17:49:57 dl9sau Exp $ */

/* Time Protocol (see RFC868) */

#include <time.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "udp.h"
#include "timer.h"
#include "netuser.h"
#include "proc.h"

static struct udp_cb *Time_server_ucb;

/* Shortest gap between two answers, in ms, and how many were dropped */
#define SERVER_MIN_INTERVAL     20
static int32 Lastserved;
static int32 Refused;

/*---------------------------------------------------------------------------*/

static void time_server(struct iface *iface, struct udp_cb *ucb, int cnt)
{

	struct mbuf *bp;
	struct socket fsocket;

	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	free_p(&bp);

	/* The reply is indistinguishable from a request here - RFC 868 has no
	 * header at all - so two of these servers pointed at each other answer
	 * each other forever.  A client always asks from an ephemeral port, so
	 * a well known one as the source is either another server or a forged
	 * address someone wants packets sent to.
	 */
	if (fsocket.port < 1024) {
		Refused++;
		return;
	}
	if (Msclock - Lastserved < SERVER_MIN_INTERVAL) {
		Refused++;
		return;
	}
	Lastserved = Msclock;

	bp = ambufw(4);
	bp->cnt = 4;
	put32(bp->data, time(0) + 2208988800UL);
	send_udp(&ucb->socket, &fsocket, LDELAY, 0, &bp, 4, 0, 0);
}

/*---------------------------------------------------------------------------*/

int time0(int argc, char **argv, void *p)
{
	if (Time_server_ucb) {
		del_udp(&Time_server_ucb);
		Time_server_ucb = 0;
	}
	return 0;
}

/*---------------------------------------------------------------------------*/

int time1(int argc, char **argv, void *p)
{
	struct socket lsocket;

	if (!Time_server_ucb) {
		lsocket.address = INADDR_ANY;
		lsocket.port = IPPORT_TIME;
		Time_server_ucb = open_udp(&lsocket, time_server);
	}
	return 0;
}
