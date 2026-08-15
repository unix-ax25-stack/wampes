# MTU, paclen and the AX.25 segmenter

Choosing an MTU for an AX.25 interface looks like arithmetic - `paclen`
times `maxframe`, minus the headers - and it is not, because three
different mechanisms decide what actually goes on the air.  This note
records how they interact, and what was measured rather than assumed.

## What paclen bounds

`paclen` bounds the AX.25 **information field**, which is N1 in the
specification.  It does not include the PID: an AX.25 I frame is

    address (14 or more) | control (1) | PID (1) | information | FCS

and the PID sits in front of the information field.  Changing the PID
from `0xCC` (IP) to `0x08` (segment) is a change of header, on the same
level as `0xCF` for NET/ROM - it costs no payload.

Measured, with `paclen 256` and a KISS port:

| what was sent | frame | breakdown |
|---|---|---|
| `ping 1400`, segmented | 272 | 14 addr + 1 ctrl + 1 PID `0x08` + **256 info** |
| `ping 236`, one frame  | 272 | 14 addr + 1 ctrl + 1 PID `0xCC` + **256 info** |
| `ping 200`, one frame  | 236 | 14 addr + 1 ctrl + 1 PID `0xCC` + 220 info |

Both paths land exactly on N1.  Nothing overshoots.

## The segmenter

`axui_send()` compresses (VJ, if built in), pushes the real PID on front,
and calls `segmenter(bpp, axp->paclen)`.  So the segmenter only runs when
the **MTU is larger than paclen**; with the two equal, which is the usual
configuration, it never runs at all.

Each segment carries PID `0x08`, and the first byte of its information
field is a counter: how many segments still follow, with bit 7
(`SEG_FIRST`) set on the first one.  The original PID is not repeated in
the segment header - it is the first byte of the reassembled payload, so
it travels inside the first segment's data.

Two bytes are easily confused here, and both are accounted for:

| byte | where | counts toward N1 | paid for by |
|---|---|---|---|
| PID `0x08` | header, before the information field | no | the `+1` in `len <= ssize+1` |
| segment counter | first byte **of** the information field | yes | the `ssize -= 1` |

The `+1` - the "grace factor" in the comment - is therefore not about the
counter.  It only decides *whether* to segment, and it exists because a
datagram of exactly `paclen` bytes still fits: the PID it carries is not
part of the information field.  Once segmenting starts, the counter is
payload and the `-= 1` pays for it.

`ssize -= 1` is correct and has been since 2016 (`0ecbf55`).  Before
that, from 1993 on, it read `ssize -= 2`, which produced information
fields of 255 bytes - one short of N1.

The reassembler in `procdata()` imposes no size limit of its own and
accepts a full 256, and `ax25subr.c` frees a partial reassembly when the
control block goes away.  There is no matching limit that would have to
be raised.

## VJ header compression

With `AX25_VJCOMP` the compressed TCP/IP header *is* the beginning of the
payload, so it counts toward the 256 - but it replaces some 40 bytes of
IP and TCP header with a handful, so the datagram reaching the segmenter
is already smaller.  Which form it is travels in the PID: `0x06`
compressed, `0x07` uncompressed, `0xCC` plain IP.  That costs nothing,
being header.

This weakens the usual argument that a small MTU wastes capacity on
headers.  For TCP it largely does not.

## The window is earned, not configured

`ax25 maxframe 7` is a ceiling, not a starting value:

    ax25subr.c   axp->maxframe = 1;              /* every link starts at 1 */
    lapb.c:542   if(axp->maxframe < Maxframe) axp->maxframe++;   /* per ack */
    lapbtime.c   if(axp->maxframe > 1) axp->maxframe--;          /* per T1 */

Slow start, additive increase, additive decrease.  On a clean link the
window climbs to the configured maximum; on a lossy one it sits near 1.
So "one datagram fits in one window" is a best-case property.  A peer
that never acknowledges holds the window at 1 - which is exactly what a
test harness does, and it is not a fault.

## IP over NET/ROM has none of this

NET/ROM carries IP too, and differently enough to be worth stating.  There
is no PID inside NET/ROM - the whole layer travels in AX.25 frames with
PID `0xCF`.  Instead there is one protocol number, `NRPROTO_IP` (`0x0c`),
and `nr_send()` writes it into both of the first two bytes of the
transport header, with layer 4 opcode `0` (`NR4OPPID`, protocol extension
to the network layer):

    L3   7 source node | 7 destination node | 1 TTL     = 15
    L4   0x0c | 0x0c | 0 | 0 | 0                        =  5
    IP datagram                                         <= 236

The two zeroes are where a circuit would keep its sequence numbers.  They
are empty because this is not a circuit: the destination *node* callsign
is resolved through `arp_lookup(ARP_NETROM, …)`, `send_l3_packet()` builds
a level 3 header, and `route_packet()` sends it across the network by the
routing table, each hop decrementing the TTL.  It reaches the destination
node, not merely the neighbour - but with no sequence numbers, no
acknowledgement, no retransmission and no flow control.  Loss is TCP's
problem, end to end.

There is no segmenter here; `segmenter()` is called from `axui_send()`
only.  What takes its place is `Nr_iface->mtu = NR4MAXINFO` (236), and
that number is chosen, not arbitrary: **236 + 20 bytes of NET/ROM headers
is 256**, so a full datagram over NET/ROM fills an AX.25 information
field exactly to N1.  Anything larger is fragmented by IP before
`nr_send()` sees it, and over NET/ROM there is no alternative to that.

On receipt the node learns the mapping by itself: it reads the IP source
address out of the datagram and enters it into the ARP table against the
NET/ROM source callsign.

So IP over NET/ROM takes the worse half of each mechanism: it fragments,
and it has no retransmission below TCP to make up for it.

One more thing about NET/ROM is easy to miss and worth stating, because
it silently overrides a setting the operator made.  A NET/ROM frame does
not go out as an IP datagram routed to an interface - `netrom.c` builds
it and hands it straight to `send_ax25(axp, bpp, -1)`, and that `-1` is
the branch which enqueues the packet whole:

    } else {
            enqueue(&axp->txq,bpp);
    }

No chopping, no paclen.  So a full NET/ROM frame goes out with an
information field of 15 + 5 + 236 = 256 bytes **whatever paclen is set
to**.  The interface MTU never enters into it either.  Two things follow:
running NET/ROM means both ends must accept a 256 byte information field,
and an operator who lowers `paclen` for a poor channel does not lower it
for NET/ROM traffic.  Making `NR4MAXINFO` follow `paclen` would fix that,
at the price of touching the NET/ROM circuit chunking as well; it is
noted in TODO.txt rather than done.

## Why segmentation beats fragmentation, and where it is unavailable

The two are often spoken of in one breath, and they are not alike.

IP fragmentation really does take the datagram apart.  Each fragment
carries its own 20 byte IP header with an offset and the MF flag; only
the first one holds the TCP header.  Reassembly happens at the **final
destination**, so every piece has to survive the whole path, and there is
no way to ask for a missing one - IP has no such mechanism.  The
destination's reassembly timer expires, everything collected is thrown
away, and TCP retransmits the entire segment, which is then fragmented
again from scratch.  Six pieces that arrived have spent their airtime for
nothing.

AX.25 segmentation does not touch the datagram.  It stays one IP
datagram; the link layer chops the byte sequence for **one hop** and the
neighbour puts it back together before anything is handed up.  A lost
segment is retransmitted by LAPB on that hop, on a T1 timer, while the
window keeps moving.  Each hop picks its own chunk size from its own
`paclen`, so a narrow link imposes nothing on the next one.  Nothing is
rewritten and no transport state is held - this is not a proxy, it works
below IP.

    IP fragmentation:  [IP|TCP|data1] [IP|data2] … [IP|data7]
                       reassembled at the far end, 7 x 20 bytes of header

    AX.25 segments:    [0x08|6|IP|TCP|data…] [0x08|5|data…] …
                       reassembled by the neighbour, 1 counter byte each

Segmentation exists **only in connected mode**.  The UI branch of
`axui_send()` returns before the segmenter is reached, and an incoming UI
frame is dispatched by PID through the `Axlink` table, which has no entry
for `0x08` - such a frame would be freed without comment.  That is not an
oversight: `procdata()` requires the segments strictly in order and
without gaps, and discards the whole reassembly at the first deviation.
Only LAPB guarantees that.  On a UI link the sole mechanism is IP
fragmentation, with nothing underneath it to repeat a loss.

## So which MTU

The MTU does not decide what goes on the air; `paclen` does.  What the
MTU decides is **who splits the datagram**, and that is enforced by IP
before AX.25 ever sees it (`iproute.c`):

    if(ip.length <= iface->mtu)     -> send
    else if(ip.flags.df)            -> ICMP fragmentation needed, carrying
                                       icmp_args.mtu = iface->mtu
    else                            -> IP fragmentation

So Path MTU Discovery works; a host behind the node learns the right size
by itself.

That leaves a real preference for connected-mode links: **an MTU large
enough that IP does not fragment**, and let the segmenter do the
chopping.  A lost IP fragment costs the whole datagram and an end-to-end
retransmission; a lost segment is retransmitted by LAPB from neighbour to
neighbour.  This is the opposite of the old reflex to keep the MTU small,
and the reflex is not wrong - it comes from UI operation and from
`paclen`, which is the knob that really touches the air.

Two things bound the choice from the other side:

* **Datagram mode.**  In `axui_send()` the type of service decides per
  datagram: low delay, broadcast, or a datagram-mode interface without
  the reliability bit take a **UI frame**, which is never segmented and
  never retransmitted.  On such a port the MTU *is* the frame size on the
  air.  Since the same port still opens a connection for traffic with the
  reliability bit set, the MTU there must be sized for the UI case.
* **Channel quality.**  1500 bytes at 1200 baud is more than twelve
  seconds of transmission, and with the window collapsed to 1 a datagram
  of that size costs one round trip per segment.

A defensible pair of settings, then: `paclen` chosen for the channel, and
the MTU either equal to it - one frame per datagram, no segmentation, no
counter byte - or large enough to keep IP from fragmenting transit
traffic.  The values in between buy nothing.

## Settings, and which mechanism each one selects

The segmenter runs when **MTU > paclen**, and only in connected mode.
That single sentence decides everything below.

**A KISS TNC, connected mode - segmentation.**  The recommended shape for
a link that carries TCP.

    ax25 paclen 256
    ax25 maxframe 7
    attach asy 0 0 kissi tnc0 2048 1500 9600

Datagrams up to 1500 travel whole; the link chops them into information
fields of exactly 256 and the far station puts them back together.  IP
never fragments, so no loss is ever amplified to the whole datagram.  The
price is the acknowledgement turnaround, which at `maxframe 7` is one RR
per seven frames - and, honestly, the ack *time* rather than the ack
bytes.  Extended AX.25 would make that smaller still; WAMPES cannot do it
(see below).

**A KISS TNC, datagram mode - no segmentation, ever.**

    ax25 paclen 256
    attach asy 0 0 kissui tnc0 2048 256 9600

Keep the MTU at or below `paclen`, because on this port the MTU *is* the
frame size on the air.  Anything larger is fragmented by IP and travels
with no retransmission underneath it at all.  Note that even here traffic
with the reliability TOS bit still opens a connection and segments, so
size the MTU for the UI case, which is the smaller one.

**NET/ROM.**  Nothing to choose: `Nr_iface->mtu` is fixed at 236 in the
code, and everything above that is fragmented by IP.

**With VJ.**  Nothing to configure, but worth knowing when picking the
numbers: a compressed TCP/IP header is typically 3 to 5 bytes instead of
40, so the header cost of a small MTU is largely recovered, and the
segmenter sees an already smaller datagram.

**Extended AX.25 is not available.**  `SABME` and `EMMASK` are defined
and `ax25dump.c` can decode both, and `ax25hdr.c` reads the `SSID_EAX25`
and `SSID_DAMA` bits - but `lapb.c` implements neither, and
`domaxframe()` clamps the window to 1..7.  WAMPES can read extended
AX.25, not speak it.

### How large, and how small

The segment counter is seven bits - bit 7 is `SEG_FIRST` - so a datagram
can be split into at most 128 pieces.  With `ssize = paclen - 1` bytes in
each, the largest datagram the segmenter can express is

    segments = 1 + D / (paclen - 1)        integer division, D = the datagram
    D_max    = 128 * (paclen - 1) - 1

| paclen | D_max | segments for a 1420 byte datagram |
|---|---|---|
| 256 | 32639 | 6 |
| 32 | 3967 | 45 |
| 16 | 1919 | 95 |
| 13 | 1535 | 119 - just fits |
| 12 | 1407 | 130 - refused |
| 8 | 895 | 203 - refused |

So with an MTU of 1500 the boundary sits at `paclen 13`, and nobody is
going to set `paclen 17`.  The useful way round to read the table is the
other one: at `paclen 256` even a 9000 byte jumbo frame is only 36
segments, so the limit never comes near a real configuration.  Below
`paclen 13` the segmenter refuses the datagram outright rather than
sending something the far end would reassemble wrongly.

At the other end, how small may the MTU be?  IP fragments with

    fragsize = (iface->mtu - ip_len) & 0xfff8

and `iface->mtu` is unsigned, which gives three regions - all measured:

| MTU | fragsize | what happens |
|---|---|---|
| 28 and up | 8 and up | fragments normally |
| 20 to 27 | 0 | `dup_p()` returns NULL, `ipFragFails++`, nothing is sent |
| below 20 | underflows | one malformed fragment goes out, then it gives up |

So 28 is the smallest MTU the arithmetic survives, and below 20 a
datagram with a nonsensical length field reaches the air before the
attempt fails.  RFC 791 requires every link to carry 68 octets anyway, so
`mtu_ok()` in `iface.c` now refuses anything smaller, on both paths that
accept a number from the operator - `attach asy` and `ifconfig <if> mtu`.

One thing the MTU must **not** decide is what may be received.  The frame
length guard in `slip.c` and `nrs.c` used to be `mtu + 256`, which ties
the receive limit to a transmit-side, IP-specific number: an operator who
sets the MTU to 236 so that IP over NET/ROM works still wants to hear a
neighbour who fills a 256 byte information field.  Measured at MTU 68: a
327 byte frame was discarded, and a maximal one - eight digipeaters and a
full information field, 329 bytes - never arrived either.  The guard now
takes the larger of `mtu + 256` and the longest frame a neighbour may
legitimately send.  The information field is 256, but the frame around it
is not:

      1   KISS type byte
     14   destination and source address
     56   eight digipeaters
      1   control
      1   PID
    256   information field, N1
      2   CRC, on a port running SMACK or FlexNet
    ---
    331   bytes on the wire, before SLIP escaping

With that, the same 327 and 329 byte frames arrive and a 347 byte one is
still refused.

The 6pack driver has no such coupling to begin with - `sixpack.c` never
reads `iface->mtu`.  Its limit is the fixed `SIXP_MAX_FRAME` of 512, and
the largest legitimate frame there is 1 TxDelay + 14 addresses + 56
digipeaters + 1 control + 1 PID + 256 information + 1 checksum = 330
plain bytes, so it has room to spare.  It differs from the SLIP guard in
one way worth knowing: it does not rise with anything.  While N1 is 256
that makes no difference, but `dopaclen()` accepts far larger values, and
a peer using one would get through over KISS and not over 6pack.  Nothing
to do about it until N1 is actually negotiated - which is the EAX25
question.

All three decoders count **plain** bytes rather than what arrived on the
line: `slip_decode()` returns early on `FR_ESC` without counting,
`nrs_decode()` does the same for `DLE`, and the 6pack decoder increments
only where a plain byte is recovered.  That is what makes the numbers
above mean the same thing for an escaped frame as for a bare one - a
331 byte frame full of `C0` may be 664 bytes on the wire and still
passes.  Those 664 are what the TNC has to buffer, not us.

### What IPv6 would demand

WAMPES has no IPv6, but the numbers are worth knowing before that
changes, and they are not small.  RFC 8200 section 5 sets the minimum
link MTU at **1280 octets**, and adds the sentence that matters here: on
any link that cannot convey a 1280 octet packet in one piece,
link-specific fragmentation and reassembly must be provided at a layer
below IPv6.

That is precisely what the segmenter is.  IPv6 over AX.25 would not
merely benefit from it - the segmenter is the condition under which such
a link is allowed to exist at all, because IPv6 routers must not
fragment; an undersized link produces ICMPv6 Packet Too Big instead.

Two consequences follow:

* `paclen` would have to be **12 or more**, so that 1280 fits within the
  128 expressible segments: `128 * 11 - 1 = 1407`, while paclen 11 gives
  1279 - one short.
* **Datagram mode could not carry IPv6 at all.**  UI frames are never
  segmented, so the link would have to convey 1280 octets in one frame,
  and nobody runs `paclen 1280` on the air.  IPv6 over AX.25 means
  connected mode.

### Telling them apart in a trace

    AX25: … pid=0x08           segmented; the next byte is the counter
    IP: … id 4711 offs 216     fragmented; `ipdump.c` prints id and offset
                               whenever offset or the MF flag is set

If neither appears, the datagram fitted in one frame.

## Where this came from

Measured on 2026-08-15 against a KISS TNC played by a script, with the
node driven through a pseudo terminal because it discards its output when
run headless.  Three traps cost time and are worth repeating:

* A trace file was block buffered, so it looked empty while the node ran
  and was lost when it was killed.  It is line buffered now.
* The KISS reader in the test script did not undo the SLIP escaping, so
  every escaped `C0` or `DB` inflated the measured length.
* `attach asy ... ax25ui` frames KISS but decodes plain AX.25, so the
  port transmits correctly and receives nothing.  Use `kissui` or
  `kissi`.
