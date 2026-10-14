/* @(#) $Id: xid.c 2026/09/21 thomas $ */

/* XID information field handling.  See xid.h for the layout; see
 * doc/AX25-2.2-GAP.md for the negotiating rules this node chose.
 */
#include "global.h"
#include "mbuf.h"
#include "ax25.h"
#include "iface.h"
#include "lapb.h"
#include "xid.h"

/* Read the subfields' PI/PL/PV parameter lists and remember what concerns
 * us.  The GL is trusted in length but bounded by what the frame actually
 * holds - pullup() refuses to walk past the data it has.
 *
 * The information field is a Format Identifier followed by one or more
 * GI/GL groups (ISO 8885).  We came for the parameter negotiation
 * (GI=0x80); our own extension sits in the private subfield (GI=0xF0) that
 * follows it.  Any other group is stepped over by its GL, which is what
 * "an unrecognized parameter is ignored" means at this level.
 */
void
xid_parse(
struct mbuf **bpp,
struct xid_info *xi)
{
	uint8 b;
	int set_wamp = 0;       /* the "WAMP" set identifier was seen */

	memset(xi,0,sizeof(*xi));
	if(pullup(bpp,&b,1) != 1 || b != XID_FI_GENERAL)
		return;
	for(;;){
		uint gl;
		int left, gi;

		/* No more subfields is a normal end, not an error. */
		if(pullup(bpp,&b,1) != 1)
			return;
		gi = b;
		if(pullup(bpp,&b,1) != 1)
			return;
		gl = (uint)b << 8;
		if(pullup(bpp,&b,1) != 1)
			return;
		gl |= b;
		left = (int)gl;
		while(left > 2){
			uint8 pi, pl;
			uint32 pv = 0;
			int i;

			if(pullup(bpp,&pi,1) != 1)
				return;
			if(pullup(bpp,&pl,1) != 1)
				return;
			left -= 2;
			if(pl > left)
				pl = left;
			for(i = 0;i < pl;i++){
				if(pullup(bpp,&b,1) != 1)
					return;
				pv = (pv << 8) | b;
			}
			left -= pl;
			switch(gi){
			case XID_GI_PARAMS:
				switch(pi){
				case XID_PI_OPTIONS:
					/* A PI longer than two octets is
					 * legal (ISO leaves it open); the
					 * bits that matter to us are within
					 * the first two.
					 */
					if(pl >= 3){
						xi->srej   = (pv & XID_OPT_SREJ)   != 0;
						xi->mod128 = (pv & XID_OPT_MOD128) != 0;
						xi->mod8   = (pv & XID_OPT_MOD8)   != 0;
						xi->segment = (pv & XID_OPT_SEGMENT) != 0;
						/* Exactly one of REJ/SREJ must be
						 * set (Figure 4.5); trust the SREJ
						 * one.
						 */
					}
					break;
				case XID_PI_WINDOWRX:
					xi->window = pv;
					break;
				case XID_PI_IFIELDRX:
					xi->n1rx = pv;
					break;
				default:
					break;
				}
				break;
			case XID_GI_PRIVATE:
				/* PI=0 names the agreement; only under
				 * our set do the bits of PI=1 mean what
				 * we think they mean. */
				switch(pi){
				case XID_PI_PRIV_SETID:
					set_wamp = (pl == XID_PRIV_SETLEN &&
						    pv == XID_PRIV_SETVAL);
					xi->wamp = set_wamp;
					break;
				case XID_PI_PRIV_CAPS:
					if(set_wamp && pl >= 1 &&
					   (pv & XID_PRIV_CAP_HOP2HOP))
						xi->hop2hop = 1;
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		}
		/* A group ends at its GL; a PI+PL the loop left behind is
		 * stepped over so the next GI is read where it really is. */
		while(left-- > 0){
			if(pullup(bpp,&b,1) != 1)
				return;
		}
	}
}

/* Build an XID information field carrying the parameters this station
 * can stand behind; see xid.h.  The field is safe a packet length or two
 * above any supported paclen, so a fixed local buffer will do.
 */
void
xid_our_info(
struct iface *ifp,
int mod128,
int srej,
int hop2hop,
struct mbuf **bpp)
{
	uint8 buf[64];
	int n = 0;
	uint32 opt;
	unsigned ktx, krx, n1tx, n1rx;
	int segmode;

	/* With no interface there is nothing to answer for - an empty
	 * (header-only) XID is what we send then.
	 */
	if(ifp == NULL)
		return;
	/* Der Segmenter/Reassembler wird nur angekuendigt, wenn der Port ihn
	 * auch betreibt.  "segmentation off" darf nicht als "kann segmentieren"
	 * auf den Draht, sonst schickt der Peer Segmente, die dieser Port nicht
	 * zusammensetzt - und umgekehrt.  ON und EXEMPT_L3 koennen es (EXEMPT_L3
	 * nur ausser fuer PID_NO_L3, was fuer die Ankuendigung genuegt).  Der
	 * Wert 0 am Port heisst "wie der Knoten"; siehe iface.h, ax25.h.
	 */
	segmode = ifp->segmentation ? ifp->segmentation : Ax25_segmentation;
	opt = XID_OPT_FIXED
		| ((segmode != AXSEG_OFF) ? XID_OPT_SEGMENT : 0)
		| (srej ? XID_OPT_SREJ : XID_OPT_REJ)
		| (mod128 ? XID_OPT_MOD128 : XID_OPT_MOD8);
	/* N1 TRANSMIT: the I-field length WE send = this port's paclen.  N1
	 * RECEIVE: what we can TAKE = the node's maximum I-field, on its
	 * own.  They are separate parameters (Figure 4.5) that were wrongly
	 * the same number here ("a port's paclen bounds both").  That told
	 * every peer a receive capability we do not have, and it then sent
	 * us less than it had to.  A zero that only follows from inheritance
	 * is not sent as 0: it means "take the node's", not "I take none".
	 */
	n1tx = (ifp->paclen ? ifp->paclen : Paclen) * 8;
	n1rx = XID_N1RX_BYTES * 8;
	/* k TRANSMIT: the window of the CAPABILITY the OPT byte announces.
	 * A port that says "modulo 128" here would run emaxframe on such a
	 * link (ax25_maxframe()), so that is the window it names; a port
	 * that says modulo 8 names maxframe.  The two used to be told apart,
	 * kTx following the OPERATIONAL modulus instead, and an "eax25
	 * accept" port then announced maxframe (7) right beside its
	 * modulo-128 bit - a window it would not run, for accept does not
	 * open a modulo-128 link by itself.  The bit and the window are one
	 * and the same capability and now move together; a peer that learns
	 * our window learns what we will run (Thomas, 2026-10-13).
	 *
	 * k RECEIVE: the window we can BUFFER, which is not the same thing.
	 * A port that runs maxframe 3 still holds the full modulo-8 window
	 * (7); one that runs emaxframe 4 still holds the 32 a 2.2 peer may
	 * use.  The peer reads kRx to learn how many I-frames it may have in
	 * flight toward us.
	 */
	ktx = mod128 ?
		 (ifp->emaxframe ? ifp->emaxframe : EMaxframe) :
		 (ifp->maxframe  ? ifp->maxframe  : Maxframe);
	if(ktx == 0 || ktx > (unsigned)(mod128 ? EMMASK : MMASK))
		ktx = mod128 ? 32 : 4;
	/* kRx follows the SAME capability bit: our buffering does not shrink
	 * because we learnt the far end cannot do modulo 128. */
	if(mod128){
		unsigned e = ifp->emaxframe ? ifp->emaxframe : EMaxframe;

		if(e == 0 || e > (unsigned)EMMASK)
			e = 32;
		/* We can do 63, but 32 is the width any 2.2 peer can be
		 * trusted with.  The capability is therefore the larger of
		 * the two: emaxframe if it is set above 32, else 32. */
		krx = (e > 32) ? e : 32;
	} else
		krx = MMASK;            /* modulo 8 holds 7 frames, no more */

	buf[n++] = XID_FI_GENERAL;
	buf[n++] = XID_GI_PARAMS;
	buf[n++] = 0;           /* Group Length, patched below */
	buf[n++] = 0;

	buf[n++] = XID_PI_OPTIONS;
	buf[n++] = 3;
	buf[n++] = (opt >> 16) & 0xff;
	buf[n++] = (opt >> 8) & 0xff;
	buf[n++] = opt & 0xff;

	/* N1 transmit (PI=5) and receive (PI=6), in bits: our sending
	 * length and our receiving capability, now told apart. */
	buf[n++] = XID_PI_IFIELDTX;
	buf[n++] = 2;
	buf[n++] = (n1tx >> 8) & 0xff;
	buf[n++] = n1tx & 0xff;

	buf[n++] = XID_PI_IFIELDRX;
	buf[n++] = 2;
	buf[n++] = (n1rx >> 8) & 0xff;
	buf[n++] = n1rx & 0xff;

	/* Window transmit (PI=7) and receive (PI=8), in frames: our own
	 * choice and our buffering capability, no longer the same. */
	buf[n++] = XID_PI_WINDOWTX;
	buf[n++] = 2;
	buf[n++] = (ktx >> 8) & 0xff;
	buf[n++] = ktx & 0xff;

	buf[n++] = XID_PI_WINDOWRX;
	buf[n++] = 2;
	buf[n++] = (krx >> 8) & 0xff;
	buf[n++] = krx & 0xff;

	/* The standard group ends here: its GL covers exactly the parameters
	 * above, so it is written before the private subfield follows. */
	buf[2] = (uint8)((n - 4) >> 8);
	buf[3] = (uint8)((n - 4) & 0xff);

	/* UNSER SUBFELD STEHT IMMER DA.  Das Set ist die Auskunft "diese
	 * Station spricht die WAMP-Vereinbarung"; sein blosses Vorhandensein
	 * ist die Aussage, die ein fehlendes Subfeld nicht machen kann - es
	 * hiesse "kann es nicht", und das ist etwas anderes als "kann es und
	 * laesst es gerade aus".  Erst danach sagt die Faehigkeits-Bitmap,
	 * was gerade an ist; hop2hop ist ihr Bit 0, gesetzt genau dann, wenn
	 * diese Station den Hop selbst terminiert und bestaetigt (Digipeat 2,
	 * siehe xid.h und doc/EAX25.md).  Ein klares Bit ist damit eine
	 * Auskunft ("wir kennen die Vereinbarung, tun es jetzt nicht"), keine
	 * Unbekannte.  Das private Subfeld folgt dem 0x80-Block, weil
	 * Subfelder nach GI aufsteigen; sein GL zaehlt nur sein eigenes Feld,
	 * das FI bleibt das eine am Kopf.
	 */
	{
		int i;

		buf[n++] = XID_GI_PRIVATE;
		buf[n++] = 0x00;                /* GL = 9: PI 0 (2+4) + PI 1 (2+1) */
		buf[n++] = 0x09;
		buf[n++] = XID_PI_PRIV_SETID;
		buf[n++] = XID_PRIV_SETLEN;
		for(i = 0;i < (int)XID_PRIV_SETLEN;i++)
			buf[n++] = XID_PRIV_SET[i];
		buf[n++] = XID_PI_PRIV_CAPS;
		buf[n++] = 1;
		buf[n++] = hop2hop ? XID_PRIV_CAP_HOP2HOP : 0;
	}

	pushdown(bpp,buf,(uint)n);
}