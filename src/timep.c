/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/timep.c,v 1.3 1994/10/09 08:23:01 deyke Exp $ */

/* Time Protocol (see RFC868) */

#include <time.h>

#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "socket.h"
#include "udp.h"
#include "timer.h"
#include "netuser.h"

static struct udp_cb *Time_server_ucb;

/*---------------------------------------------------------------------------*/

static void time_server(struct iface *iface, struct udp_cb *ucb, int cnt)
{

	struct mbuf *bp;
	struct socket fsocket;

	if (recv_udp(ucb, &fsocket, &bp) < 0) return;
	free_p(bp);
	bp = ambufw(4);
	bp->cnt = 4;
	put32(bp->data, time((time_t *) 0) + 2208988800UL);
	send_udp(&ucb->socket, &fsocket, DELAY, 0, bp, 4, 0, 0);
}

/*---------------------------------------------------------------------------*/

int time0(int argc, char **argv, void *p)
{
	if (Time_server_ucb) {
		del_udp(Time_server_ucb);
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

