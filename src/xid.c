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

/* Read the PI/PL/PV parameter list and remember what concerns us.
 * The GL is trusted in length but bounded by what the frame actually
 * holds - pullup() refuses to walk past the data it has.
 */
void
xid_parse(
struct mbuf **bpp,
struct xid_info *xi)
{
	uint8 b;
	uint gl;
	int left;

	memset(xi,0,sizeof(*xi));
	if(pullup(bpp,&b,1) != 1 || b != XID_FI_GENERAL)
		return;
	if(pullup(bpp,&b,1) != 1 || b != XID_GI_PARAMS)
		return;
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
		switch(pi){
		case XID_PI_OPTIONS:
			/* A PI longer than two octets is legal (ISO
			 * leaves it open); the bits that matter to us are
			 * within the first two.
			 */
			if(pl >= 3){
				uint32 o = pv;

				xi->srej   = (o & XID_OPT_SREJ)   != 0;
				xi->mod128 = (o & XID_OPT_MOD128) != 0;
				xi->mod8   = (o & XID_OPT_MOD8)   != 0;
				xi->segment = (o & XID_OPT_SEGMENT) != 0;
				/* Exactly one of REJ/SREJ must be set
				 * (Figure 4.5); trust the SREJ one.
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
	}
}

/* Build an XID information field carrying the parameters this station
 * can stand behind; see xid.h.  The field is safe a packet length or two
 * above any supported paclen, so a fixed local buffer will do.
 */
void
xid_our_info(
struct iface *ifp,
int mmask,
int srej,
struct mbuf **bpp)
{
	uint8 buf[64];
	int n = 0;
	uint32 opt;
	unsigned window, n1;
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
		| ((mmask == EMMASK) ? XID_OPT_MOD128 : XID_OPT_MOD8);
	/* N1, the I-field length we take, in bits (Figure 4.5).  A port
	 * that has no paclen of its own inherits the node's, and answering
	 * 0 here would announce "I take no information field at all" - which
	 * is how a prober saw N1rx=0 in the trace. */
	n1 = (ifp->paclen ? ifp->paclen : Paclen) * 8;
	/* k, the window we will buffer.  The receive window is what the
	 * spec asks about; we buffer nothing beyond the module, so say so.
	 * Same inheritance as the length: 0 means "take the node's", not "0".
	 */
	window = (mmask == EMMASK) ?
		 (ifp->emaxframe ? ifp->emaxframe : EMaxframe) :
		 (ifp->maxframe  ? ifp->maxframe  : Maxframe);
	if(window == 0 || window > (unsigned)mmask)
		window = (mmask == EMMASK) ? 32 : 4;

	buf[n++] = XID_FI_GENERAL;
	buf[n++] = XID_GI_PARAMS;
	buf[n++] = 0;           /* Group Length, patched below */
	buf[n++] = 0;

	buf[n++] = XID_PI_OPTIONS;
	buf[n++] = 3;
	buf[n++] = (opt >> 16) & 0xff;
	buf[n++] = (opt >> 8) & 0xff;
	buf[n++] = opt & 0xff;

	/* N1 transmit (PI=5) and receive (PI=6), in bits.  A port's paclen
	 * bounds both directions, so the two numbers are the same here -
	 * announcing the send side lets the peer size what it sends us. */
	buf[n++] = XID_PI_IFIELDTX;
	buf[n++] = 2;
	buf[n++] = (n1 >> 8) & 0xff;
	buf[n++] = n1 & 0xff;

	buf[n++] = XID_PI_IFIELDRX;
	buf[n++] = 2;
	buf[n++] = (n1 >> 8) & 0xff;
	buf[n++] = n1 & 0xff;

	/* Window transmit (PI=7) and receive (PI=8), in frames.  Same
	 * window in both directions, as for N1. */
	buf[n++] = XID_PI_WINDOWTX;
	buf[n++] = 2;
	buf[n++] = (window >> 8) & 0xff;
	buf[n++] = window & 0xff;

	buf[n++] = XID_PI_WINDOWRX;
	buf[n++] = 2;
	buf[n++] = (window >> 8) & 0xff;
	buf[n++] = window & 0xff;

	buf[2] = (uint8)((n - 4) >> 8);
	buf[3] = (uint8)((n - 4) & 0xff);

	pushdown(bpp,buf,(uint)n);
}