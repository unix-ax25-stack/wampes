/* @(#) $Id: n8250.h,v 1.9 1996/08/12 18:51:17 deyke Exp $ */

/* Various I/O definitions specific to asynch I/O */
#ifndef _N8250_H
#define _N8250_H

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

#ifndef _TIMER_H
#include "timer.h"
#endif

/* Modem-flow options for a serial port: the legacy 8250 "c" (CTS handshake)
 * and "r" (carrier-gated, aka RLSD/CD) attach flags.  Made real by asy_up():
 * it applies them on every open, so a watchdog reopen restores them - the
 * plain open()/TCSETS default is that neither is active.
 */
#define ASY_F_CTS       0x01    /* c: CRTSCTS, the driver gates the line on CTS */
#define ASY_F_RLSD      0x02    /* r: clear CLOCAL, receive only with carrier  */

/* Asynch controller control block */
struct asy {
	struct iface *iface;

	int fd;                 /* File descriptor */

	struct mbuf *sndq;      /* Transmit queue */

	unsigned addr;          /* Base I/O address */
	int vec;                /* Interrupt vector */
	long speed;             /* Line speed in bits per second */

	int flow;               /* ASY_F_* bits, set at attach, applied at open */

	long rxints;            /* receive interrupts */
	long txints;            /* transmit interrupts */
	long rxchar;            /* Received characters */
	long txchar;            /* Transmitted characters */
	long rxhiwat;           /* High water mark on hardware rx fifo */

	/* How much has been handed to this device since it was set up, and how
	 * much of it went out the door: txqueued counts every byte as asy_send()
	 * appends it to sndq, txchar counts what write() actually accepted.
	 * With a queue that survives a reset the two diverge only while data is
	 * parked, but a device that swallows bytes on the way down (or data
	 * dropped by PARAM_DOWN/detach) leaves txqueued permanently larger.
	 */
	unsigned long txqueued; /* Total bytes ever appended to sndq */
	unsigned long wdreset;  /* Times the watchdog or an I/O error reset this port */

	/* The serial device node, provided by asy_attach() which splits the
	 * "device|label" form of the attach argument.  iface->name then holds
	 * only the label, so a watchdog reopen must open this path instead of
	 * trying to build one from the name.  Freed on detach.
	 */
	char *devfile;

	/* Transmit progress watchdog.  Some USB-CDC equipment (a Kenwood
	 * TH-D75 among it) stops draining its endpoint after a burst, which
	 * leaves select() never reporting the port writable again and sndq
	 * growing forever.  After ASY_WD_RESET checks without a byte moving,
	 * the port is torn down and reopened - the reopen resets the device,
	 * and asy_up() re-asserts DTR and RTS on its way up, which is what
	 * such a backend needs to start talking again.
	 */
	struct timer wd;
	long lasttx;            /* txchar as of the last watchdog check */
	unsigned wdstall;       /* consecutive checks without any progress */
	unsigned wdreopen;      /* ticks of DTR-low phase in a port reset */

	/* Consecutive read() answers of ENXIO.  A USB-CDC backend (Kenwood
	 * TH-D75 and friends) can answer every read with ENXIO while the chip
	 * is busy on the USB bus; the descriptor then stays readable for
	 * select() and the line would spin at full speed.  The first ENXIO
	 * withdraws read interest (off_read) and the one-second watchdog re-arms
	 * it, so a stubborn chip gets exactly one fresh read per second and the
	 * streak is visible in asystat as "enxio".
	 */
	unsigned rxenx;         /* consecutive ENXIO reads */
};

extern struct asy Asy[];

#endif  /* _N8250_H */
