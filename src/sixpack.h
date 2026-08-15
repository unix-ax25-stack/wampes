/* 6PACK: an AX.25 host-to-TNC protocol on a serial line.  See sixpack.c. */

#ifndef _SIXPACK_H
#define _SIXPACK_H

#ifndef _GLOBAL_H
#include "global.h"
#endif

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef _IFACE_H
#include "iface.h"
#endif

/* A data byte is one with the top two bits clear.  Everything else is a
 * command, and there the low three bits are the TNC address.
 */
#define SIXP_IS_DATA(c)         (((c) & 0xC0) == 0)
#define SIXP_ADDR_MASK          0x07
#define SIXP_CMD_MASK           0xF8
#define SIXP_CMD(c)             ((c) & SIXP_CMD_MASK)
#define SIXP_ADDR(c)            ((c) & SIXP_ADDR_MASK)
#define SIXP_MAKE_CMD(op,addr)  ((uint8) ((op) | (addr)))

#define SIXP_CMD_SEOF           0x40    /* start and end of a frame */
#define SIXP_CMD_TX_ORUN        0x48    /* the TNC lost transmit data */
#define SIXP_CMD_RX_ORUN        0x50    /* ... receive data */
#define SIXP_CMD_RX_BUF_OVL     0x58    /* ... a whole frame */
#define SIXP_CMD_LED            0x60
#define SIXP_CMD_DCD            0x88    /* carrier state follows */
#define SIXP_CMD_RX_1           0x90
#define SIXP_CMD_TX_1           0xA0
#define SIXP_CMD_CAL            0xE0
#define SIXP_CMD_ADDR           0xE8

/* Sum of the data bytes plus the address, over a good frame. */
#define SIXP_CHKSUM             0xFF

/* Long enough for a reassembled frame and its trimmings.  A 6pack TNC will
 * not send more, and a frame that claims to be longer is not one.
 */
#define SIXP_MAX_FRAME  512

struct sixpack {
	struct iface *iface;
	int (*send)(int dev, struct mbuf **bpp);
	int (*get)(int dev, uint8 *buf, int cnt);

	uint8 addr;             /* which TNC on the line: eight may share it */
	uint8 txdelay;          /* the first byte of every data field */

	/* Decoder.  Four encoded bytes make three plain ones, so part holds
	 * what is left over between them.
	 */
	uint8 seof;             /* the SEOF that opened the frame - it carries
				 * the address the checksum is computed over */
	int inframe;            /* a SEOF has been seen */
	int cnt;                /* encoded bytes since that SEOF */
	uint rcnt;              /* plain bytes decoded so far */
	uint8 part;
	uint8 rbuf[SIXP_MAX_FRAME];
	struct mbuf *rbp;

	int dcd;                /* carrier, as the TNC reports it */
	long packets;
	long errors;
	long overruns;          /* what the TNC says it lost */
	long unknown;           /* command bytes we do not speak - an extended
				 * TNC such as the PR430 has its own, and they
				 * are not faults */
};

extern struct sixpack Sixpack[];

int sixpack_init(struct iface *ifp);
int sixpack_free(struct iface *ifp);
int sixpack_raw(struct iface *iface, struct mbuf **bpp);
void sixpack_recv(void *arg);
int do6pstat(int argc, char *argv[], void *p);

#endif /* _SIXPACK_H */
