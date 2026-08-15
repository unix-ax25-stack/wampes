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

#ifndef _TIMER_H
#include "timer.h"
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

/* There are two kinds of command byte, and mistaking one for the other is
 * the trap here.  Bit 7 marks a PRIORITY command, and in one of those the
 * bits 3..5 are a STATE - carrier, receiving, transmitting - and not an
 * opcode.  Bit 7 clear and bit 6 set is an ordinary command, and there the
 * same bits 3..5 do carry an opcode.  Both keep the address in bits 0..2.
 *
 * Reading a priority command as though bits 3..5 were an opcode appears to
 * work, because "carrier present" then looks like a command of its own -
 * until the carrier goes away and its byte, 0x80, matches nothing.
 */
#define SIXP_PRIO_CMD           0x80    /* this is a priority command */
#define SIXP_PRIO_STATE         0x38    /* ... and this is its state field */
#define SIXP_STATE_DCD          0x08    /* carrier present */
#define SIXP_STATE_RX           0x10    /* receiving */
#define SIXP_STATE_TX           0x20    /* transmitting */

#define SIXP_CMD_SEOF           0x40    /* start and end of a frame */
#define SIXP_CMD_TX_ORUN        0x48    /* the TNC lost transmit data */
#define SIXP_CMD_RX_ORUN        0x50    /* ... receive data */
#define SIXP_CMD_RX_BUF_OVL     0x58    /* ... a whole frame */
#define SIXP_CMD_LED            0x60

/* Sum of the data bytes plus the address, over a good frame. */
#define SIXP_CHKSUM             0xFF

/* Long enough for a reassembled frame and its trimmings.  A 6pack TNC will
 * not send more, and a frame that claims to be longer is not one.  Counted
 * out, the largest legitimate one is
 *
 *     1   TxDelay byte, which this layer puts in front
 *    14   destination and source address
 *    56   eight digipeaters
 *     1   control
 *     1   PID
 *   256   information field, N1
 *     1   checksum
 *   ---
 *   330   plain bytes, before the six-bit encoding blows them up by a third
 *
 * so 512 leaves room to spare.  These are PLAIN bytes: the decoder counts
 * what it has recovered, not what arrived on the line, which is the only way
 * the limit means the same thing for an encoded frame as for a bare one.
 *
 * Unlike the guard in slip.c this ceiling does not rise with anything.  It is
 * sized for N1 = 256, and dopaclen() will accept far more than that; see the
 * note in TODO.txt.
 */
#define SIXP_MAX_FRAME  512

struct sixpack {
	struct iface *iface;
	int (*send)(int dev, struct mbuf **bpp);
	int (*get)(int dev, uint8 *buf, int cnt);

	uint8 addr;             /* which TNC on the line: eight may share it */
	uint8 txdelay;          /* the first byte of every data field */

	/* Channel access lives here, not in the TNC - that is the other half
	 * of why 6pack reports the carrier.  See sixpack_tx_try().
	 */
	uint8 persistence;      /* p, out of 255 */
	uint8 slottime;         /* how long to wait before rolling again, in
				 * units of 10 ms, as KISS counts them */
	uint8 duplex;           /* set: transmit at once, ask nothing */
	struct mbuf *txq;       /* encoded frames waiting for a quiet channel */
	struct timer tx_t;

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
int32 sixpack_ioctl(struct iface *ifp, int cmd, int set, int32 val);
void sixpack_recv(void *arg);
int do6pstat(int argc, char *argv[], void *p);

#endif /* _SIXPACK_H */
