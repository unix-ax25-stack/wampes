/* @(#) $Id: xid.h 2026/09/21 thomas $ */

/* AX.25 2.2 XID - the Exchange Identification frame (4.3.3.7), and the
 * parameter negotiation that rides in its information field.  The frame
 * itself is an unnumbered one; the information field is ISO 8885
 * "general-purpose XID": Format Identifier, Group Identifier, Group
 * Length, and then PI/PL/PV triples in ascending PI order.
 *
 * XID is the way AX.25 2.2 stations tell each other what they can do
 * before the link is established.  This implementation reads the items
 * that are relevant to it and answers with the ones it can stand behind:
 *
 *   PI=3  HDLC Optional Functions - the SREJ bit is what makes a peer
 *         eligible for our SREJ instead of REJ (and what sets our
 *         peer_srej flag); the modulo bits say what the peer can follow
 *         on a link WE open.  The answer to our own probe is honored
 *         there: modulo-128 offered turns the following SABM into a
 *         SABME (lapb_input(), the neg_wait branch).  The probe itself
 *         forces nothing - see "does not change the modulus".  Bit 22
 *         announces the segmenter/reassembler, which this node does
 *         implement (segmenter(), PID 0x08).
 *   PI=5  I-field length transmit (N1) - what we can send; the same
 *         number as PI=6 here, because a port's paclen bounds both.
 *   PI=6  I-field length receive (N1) - what we will take.
 *   PI=7  Window size transmit (k) - the same as PI=8 here.
 *   PI=8  Window size receive (k) - what we will buffer.
 *
 * The peer's PI=6/PI=8 are its RECEIVE capabilities, so they bound what
 * WE may put on the link: they are read (xi.n1rx, xi.window) and clamped
 * to for that one link (ax25_apply_peer_xid) - ours to honor, not his to
 * obey, which is why no monitor can tell whether we did.  Nothing about
 * them is remembered per route: a XID seen through a digipeater describes
 * the far end, not the nexthop (doc/AX25-2.2-GAP.md).
 *
 * Everything else (PI=2 classes of procedures, PI=9 acknowledge timer,
 * PI=10 retries, unknown PIs) is skipped: per 4.3.3.7 an unrecognized
 * parameter is ignored and an absent one retains its current or default
 * value.
 */

#ifndef _XID_H
#define _XID_H

#include "mbuf.h"
#include "iface.h"

/* The general-purpose XID information field (4.3.3.7). */
#define XID_FI_GENERAL  0x82    /* Format Identifier */
#define XID_GI_PARAMS   0x80    /* Group Identifier, parameter negotiation */

/* Parameter identifiers, Figure 4.5.  Die Nummern stehen so in der Norm
 * (AX.25 2.2 Rev 4, Fig. 4.5, Seite 32): 5 ist die I-Field-Laenge Tx, 6 die
 * Rx, 7 die Fenstergroesse k Tx, 8 die Rx.  Hier stand PI=7 faelschlich als
 * "transmit length" - das ist PI=5.  Kein Drahtverhalten aendert sich daran,
 * weil wir PI=7 nirgends senden; nur der Monitor beschriftete PI=7 falsch.
 */
#define XID_PI_CLASSES  2       /* Classes of procedures (duplex) */
#define XID_PI_OPTIONS  3       /* HDLC Optional Functions */
#define XID_PI_IFIELDTX 5       /* Max I-field length, transmit (bits) */
#define XID_PI_IFIELDRX 6       /* Max I-field length, receive (bits) */
#define XID_PI_WINDOWTX 7       /* Window size k, transmit (frames) */
#define XID_PI_WINDOWRX 8       /* Window size k, receive (frames) */
#define XID_PI_ACKTIME  9       /* Wait for acknowledge, T1 (msec) */
#define XID_PI_RETRIES  10      /* Retry count */

/* Bits of the HDLC Optional Functions value (Figure 4.5).  It is a
 * three-octet value per ISO 8885; bits 7, 13, 15 and 17 are always 1,
 * bits 1 and 2 select REJ or SREJ (exactly one must be set), bits 10/11
 * select the modulus, bit 22 asks for the segmenter/reassembler.
 */
#define XID_OPT_REJ     0x000002        /* bit 1:  implicit reject */
#define XID_OPT_SREJ    0x000004        /* bit 2:  selective reject */
#define XID_OPT_MOD8    0x000400        /* bit 10: modulo 8 */
#define XID_OPT_MOD128  0x000800        /* bit 11: modulo 128 */
#define XID_OPT_FIXED   0x02a080        /* bits 7,13,15,17, always 1 */
/* Bit 22: segmenter/reassembler.  This node implements it (segmenter(),
 * PID 0x08, lapb.c), so it sets the bit in its own optional functions;
 * a peer's bit is read too (xi.segment).  Per 6.6 the procedure is only
 * enabled when both ends are AX.25 2.2 - which is what answering a XID
 * proves.
 */
#define XID_OPT_SEGMENT 0x400000        /* bit 22: segmenter/reassembler */

/* What a peer told us about itself in an XID. */
struct xid_info {
	int srej;               /* PI=3: he selects SREJ */
	int mod128;             /* PI=3: he offers modulo 128 */
	int mod8;               /* PI=3: he offers modulo 8 */
	int segment;            /* PI=3: he offers segmenter/reassembler */
	unsigned n1rx;          /* PI=6: I-field length he receives (bits) */
	unsigned window;        /* PI=8: frames k he will buffer */
};

/* Parse an XID information field off the head of *bpp (consuming it,
 * like everything else in this code base), filling *xi from the
 * parameters it recognizes.  A malformed or non-general field is read
 * defensively and simply yields an empty *xi.
 */
void xid_parse(struct mbuf **bpp,struct xid_info *xi);

/* Build our own XID information field onto *bpp - empty, or with the
 * items *this* station can stand behind (see the file header).  mmask is
 * the modulus an answer speaks in (the link's, or MMASK when answering
 * something with no link yet).  srej tells whether to offer SREJ -
 * always true here, kept in the signature for the negotiated gate.
 */
void xid_our_info(struct iface *ifp,int mmask,int srej,struct mbuf **bpp);

#endif  /* _XID_H */