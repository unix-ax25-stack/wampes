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
 *
 * A second GROUP IDENTIFIER, 0xF0, carries our own extension (the private
 * parameter negotiation subfield; doc/EAX25.md).  Its PI=0 names the set
 * ("WAMP") and PI=1 bit 0 says hop2hop-ack: this station, as a nexthop,
 * terminates the hop and acknowledges it.  Subfields beyond the first are
 * walked by GI order; one we do not know is stepped over by its GL.
 */

#ifndef _XID_H
#define _XID_H

#include "mbuf.h"
#include "iface.h"

/* The general-purpose XID information field (4.3.3.7). */
#define XID_FI_GENERAL  0x82    /* Format Identifier */
#define XID_GI_PARAMS   0x80    /* Group Identifier, parameter negotiation */

/* The private parameter negotiation subfield (ISO/IEC 8885; ISO/IEC 13239
 * calls it the user-defined parameters subfield, ITU-T V.42 gives the same
 * 0xF0 to its own).  It is the place the standards leave to an agreement
 * between the stations, which is where our extension lives; "PI=0 is the
 * set identifier" is V.42's convention and it is what tells the two sides
 * they mean the same thing (doc/EAX25.md, "A parameter of our own").
 */
#define XID_GI_PRIVATE   0xF0   /* Group Identifier, private parameters */
#define XID_PI_PRIV_SETID 0     /* our parameter set identifier (ASCII) */
#define XID_PI_PRIV_CAPS  1     /* our capability bits */
#define XID_PRIV_SET     "WAMP" /* the set: the umbrella for our extensions */
#define XID_PRIV_SETLEN  (sizeof(XID_PRIV_SET) - 1)
/* The PV of PI=0, derived from XID_PRIV_SET so the two cannot drift apart:
 * the encoder writes the string, the parser compares the number. */
#define XID_PRIV_SETVAL  ((uint32)XID_PRIV_SET[0] << 24 | \
			  (uint32)XID_PRIV_SET[1] << 16 | \
			  (uint32)XID_PRIV_SET[2] << 8  | \
			  (uint32)XID_PRIV_SET[3])

/* Bits of the private capability value (PI=1).  One bit per extension, in
 * order; nothing grows by inventing a new PI each time.
 */
#define XID_PRIV_CAP_HOP2HOP 0x01   /* bit 0: we acknowledge hop by hop */


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
	/* The "WAMP" set identifier was seen: he speaks our agreement.
	 * Independent of the capability bits - a set with hop2hop clear
	 * still says he understands the subfield at all, which is exactly
	 * what xid_teach_hop2hop() needs to know before it answers a
	 * neighbour that forgot us. */
	int wamp;               /* GI=F0 PI=0: he speaks our set */
	int hop2hop;            /* GI=F0 PI=1: he acks hop by hop (as neapthop) */
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
 * Four numbers, and they are told apart:
 *   N1tx = this port's paclen (what we send),
 *   N1rx = XID_N1RX_BYTES (what the stack can take), fixed,
 *   kTx  = the window of the OPTIONS modulus bit (emaxframe beside
 *          modulo 128, maxframe beside modulo 8),
 *   kRx  = the buffering capability (mod128 ? emaxframe-or-32 : 7).
 * The modulus bit and kTx are one capability and move together: a port
 * that announces modulo 128 names the window it would run on such a link
 * (ax25_maxframe()), so an "eax25 accept" port does not put maxframe
 * beside its modulo-128 bit.  kRx follows the same bit, for our buffering
 * does not shrink with the peer.  Every value here describes the ARM the
 * frame leaves by, the port that puts it on the wire: an answer a relay
 * writes in a peer's name (xid_leg_answer) therefore takes its numbers from
 * OUR port towards the caller, not from the station behind us.  srej tells
 * whether to offer SREJ.
 *
 * The private subfield (GI=0xF0, doc/EAX25.md) is ALWAYS written: its set
 * identifier says this station speaks the WAMP agreement, and an absent
 * subfield cannot - it can only say "does not speak it".  Inside it,
 * hop2hop is bit 0 of the capability bitmap and says that THIS station, as
 * a nexthop, terminates the hop and acknowledges it.  It too is a statement
 * about the station that puts the frame on the wire, taken from the node's
 * own s&f-digipeat capability (Digipeat == 2) and never from what a peer
 * said; a clear bit is "speaks it, does not do it now", so the capability
 * value is 0 then, not absent.
 */
void xid_our_info(struct iface *ifp,int mod128,int srej,int hop2hop,
	struct mbuf **bpp);

#endif  /* _XID_H */