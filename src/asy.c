/* @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/asy.c,v 1.1 1990/09/11 13:44:56 deyke Exp $ */

#include <stdio.h>
#include "global.h"
#include "iface.h"
#include "pktdrvr.h"
#include "netuser.h"
#include "asy.h"
#include "ax25.h"
#include "kiss.h"
#include "slip.h"
#include "nrs.h"
#include "config.h"
/* #include "proc.h" */
#include "commands.h"

/* Attach a serial interface to the system
 * argv[0]: hardware type, must be "asy"
 * argv[1]: I/O address, e.g., "0x3f8"
 * argv[2]: vector, e.g., "4"
 * argv[3]: mode, may be:
 *          "slip" (point-to-point SLIP)
 *          "ax25" (AX.25 frame format in SLIP for raw TNC)
 *          "nrs" (NET/ROM format serial protocol)
 * argv[4]: interface label, e.g., "sl0"
 * argv[5]: receiver ring buffer size in bytes
 * argv[6]: maximum transmission unit, bytes
 * argv[7]: interface speed, e.g, "9600"
 * argv[8]: optional flags, e.g., 'c' for cts flow control
 */
int
asy_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
	register struct iface *if_asy;
	int dev;
	int mode;
	int xdev;
	int trigchar = -1;
	char cts;

	if(Nasy >= ASY_MAX){
		tprintf("Too many asynch controllers\n");
		return -1;
	}
	if(if_lookup(argv[4]) != NULLIF){
		tprintf("Interface %s already exists\n",argv[4]);
		return -1;
	}
	if(strcmp(argv[3],"slip") == 0)
		mode = SLIP_MODE;
	else if(strcmp(argv[3],"ax25") == 0)
		mode = AX25_MODE;
	else if(strcmp(argv[3],"nrs") == 0)
		mode = NRS_MODE;
	else
		mode = UNKNOWN_MODE;

	dev = Nasy++;

	/* Create interface structure and fill in details */
	if_asy = (struct iface *)callocw(1,sizeof(struct iface));
	if_asy->addr = Ip_addr;
	if_asy->name = strdup(argv[4]);
	if_asy->mtu = atoi(argv[6]);
	if_asy->dev = dev;
	if_asy->stop = asy_stop;

	switch(mode){
#ifdef  SLIP
	case SLIP_MODE:
		setencap(if_asy,"SLIP");
		if_asy->ioctl = asy_ioctl;
		if_asy->raw = slip_raw;
		if_asy->flags = 0;
		if(Nslip >= SLIP_MAX) {
			tprintf("Too many slip devices\n");
			return -1;
		}
		xdev = Nslip++;
		if_asy->xdev = xdev;

		Slip[xdev].iface = if_asy;
		Slip[xdev].send = asy_send;
		Slip[xdev].get = get_asy;
		Slip[xdev].type = CL_SERIAL_LINE;
		trigchar = FR_END;
		if_asy->proc = asy_rx;
		break;
#endif
#ifdef  AX25
	case AX25_MODE:  /* Set up a SLIP link to use AX.25 */
		setencap(if_asy,"AX25");
		if_asy->ioctl = kiss_ioctl;
		if_asy->raw = kiss_raw;
		if(if_asy->hwaddr == NULLCHAR)
			if_asy->hwaddr = mallocw(AXALEN);
		memcpy(if_asy->hwaddr,Mycall,AXALEN);
		if(Nslip >= SLIP_MAX) {
			tprintf("Too many slip devices\n");
			return -1;
		}
		xdev = Nslip++;
		if_asy->xdev = xdev;

		Slip[xdev].iface = if_asy;
		Slip[xdev].send = asy_send;
		Slip[xdev].get = get_asy;
		Slip[xdev].type = CL_KISS;
		trigchar = FR_END;
		if_asy->proc = asy_rx;
		break;
#endif
#ifdef  NRS
	case NRS_MODE: /* Set up a net/rom serial iface */
		/* no call supplied? */
		setencap(if_asy,"AX25");
		if_asy->ioctl = asy_ioctl;
		if_asy->raw = nrs_raw;
		if_asy->hwaddr = mallocw(AXALEN);
		memcpy(if_asy->hwaddr,Mycall,AXALEN);
		if(Nnrs >= SLIP_MAX) {
			tprintf("Too many nrs devices\n");
			return -1;
		}
		xdev = Nnrs++;
		if_asy->xdev = xdev;
		Nrs[xdev].iface = if_asy;
		Nrs[xdev].send = asy_send;
		Nrs[xdev].get = get_asy;
		trigchar = ETX;
		if_asy->proc = nrs_recv;
		break;
#endif
	default:
		tprintf("Mode %s unknown for interface %s\n",
			argv[3],argv[4]);
		free(if_asy->name);
		free((char *)if_asy);
		Nasy--;
		return -1;
	}
	if_asy->next = Ifaces;
	Ifaces = if_asy;
	if(argc > 8 && *argv[8] == 'c')
		cts = 1;
	else
		cts = 0;
	asy_init(dev,if_asy,argv[1],argv[2],(unsigned)atoi(argv[5]),trigchar,cts);
	asy_speed(dev,atol(argv[7]));
	return 0;
}

