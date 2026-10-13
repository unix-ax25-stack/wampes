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
 *   PI=5  I-field length transmit (N1) - what this port will send
 *         (its paclen).
 *   PI=6  I-field length receive (N1) - what we can take (the node's
 *         maximum I-field), which is NOT the same number: a small
 *         sending paclen says nothing about what we can receive.
 *   PI=7  Window size transmit (k) - the window this port will run
 *         (maxframe / emaxframe), our own choice.
 *   PI=8  Window size receive (k) - the window we can buffer, the full
 *         modulo's worth (7 in modulo 8), not the smaller value a
 *         timid sending window would suggest.
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

/* Die groesste I-Field-Laenge, die dieser Stack ANNIMMT, in Oktetten.
 * Das ist NICHT paclen: paclen ist, womit WIR senden.  Empfangsseitig
 * gibt es (noch) keinen eigenen Schalter, und der Stack kann die
 * Standardlaenge 256 - theoretisch mehr, aber da hoert die
 * Kompatibilitaet auf.  Also steht sie hier fest, bis es ein
 * RX-paclen gibt (Thomas, 2026-10-12). */
#define XID_N1RX_BYTES  256

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
 * a peer's bit is read too (xi.segment) and HONORED: if he does not offer
 * it, no segment goes toward him (ax25.c, ax25user.c, peer_segment in
 * lapb.h).  Per 6.6 the procedure is only enabled when both ends are
 * AX.25 2.2 - which is what answering a XID proves.
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
 * items *this* station can stand behind (see the file header).
 *
 * Four numbers, and they are told apart now:
 *   N1tx = this port's paclen (what we send),
 *   N1rx = XID_N1RX_BYTES (what the stack can take), fixed,
 *   kTx  = the operational window (mmask's maxframe/emaxframe),
 *   kRx  = the buffering capability (mod128 ? emaxframe-or-32 : 7).
 * mmask is the modulus whose WINDOW (kTx) the field speaks with.  mod128
 * is the OPTIONS modulus bit, and it is deliberately separate: the bit is
 * a CAPABILITY (what this port can do, eax25 != off).  They differ on an
 * "eax25 accept" port, which can do modulo 128 and must announce it even
 * though it will not open a link with it by itself - a peer that reads
 * mod8 there would never try SABME toward us - and they differ when the
 * far end is learnt to be mod8-only.  kRx follows the CAPABILITY bit, not
 * the operational window: our buffering does not shrink with the peer.
 * An answer in a peer's name (xid_leg_answer) passes that peer's own bit,
 * not ours.  srej tells whether to offer SREJ.
 */
void xid_our_info(struct iface *ifp,int mmask,int mod128,int srej,struct mbuf **bpp);

#endif  /* _XID_H */