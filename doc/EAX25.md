# Modulo-128, and who gets to decide

AX.25 counts frames modulo 8, so at most seven may be outstanding.  On a
slow channel that costs nothing, because the time spent transmitting
dwarfs the pause for the acknowledgement.  On a fast one it is most of the
difference.  Measured here, same 528 894 bytes over the same rig with a
100 ms round trip simulated:

| | window | time | throughput |
| --- | --- | --- | --- |
| modulo-8 | 7 (the cap) | 30.8 s | 17 149 B/s |
| modulo-128, `emaxframe 32` | 32, reached and steady | 8.4 s | 63 335 B/s |
| modulo-128, `emaxframe 63` | 50, and 63 only at the very end | 6.6 s | 80 486 B/s |

WAMPES could read modulo-128 for twenty years - `SABME`, `EMMASK`, the
decoding in `ax25dump.c`, the `SSID_EAX25` bit in `ax25hdr.c` - and never
speak it.  `lapb.c` was modulo-8 throughout.

## The one thing to know before turning it up

The window does not start at its cap, it grows by **one per round trip**,
and only when a timed frame is acknowledged without having been
retransmitted.  Reaching window *W* therefore costs

    W (W + 1) / 2 frames

which is 528 frames to reach 32 and **2016 to reach 63**.  At 1k2 a frame
of `paclen` 256 takes 1.84 s on the air, so that is 16 minutes and **62
minutes** of continuous transmission respectively, spent before the
setting takes effect at all.

And at 1k2 it buys almost nothing even then.  Taking about 0.5 s for the
turnaround, the share of time actually spent sending is:

| channel | window 7 | window 32 | gain |
| --- | --- | --- | --- |
| 1k2 | 96 % | 99 % | +3 % |
| 9k6 | 76 % | 94 % | +23 % |
| 19k2 | 62 % | 88 % | +43 % |

So modulo-128 is for the fast interlink, and `emaxframe 63` is for a fast
interlink carrying long transfers.  The default is 32, which is what the
Linux kernel uses as `AX25_DEF_EWINDOW`.

The other half of the trade is loss.  AX.25 has no SREJ - `SREJ` does not
appear anywhere in this source - so a single lost frame costs a
retransmission of the whole outstanding window.  Measured at 3 % loss and
a 100 ms round trip, the advantage falls from 2.26x to **1.28x**.  A big
window on a lossy channel is not a gain.

## Configuration

    ifconfig <iface> eax25 off | accept | caller | always   (default: accept)
    ifconfig <iface> paclen | maxframe | emaxframe <n>      (0 = node's own)
    ax25 emaxframe <1..63>                                  (default: 32)

`accept` - the default, and it changes nothing for anyone: we answer a
SABME when one arrives and never send one.  Probing costs 19 s against a
peer that ignores SABME - once per station, but that is not a cost to hand
to every installation by surprise.  An unconfigured node behaves as it
always did, which is the same rule `doc/ROUTE-FILTER.md` follows.

It has one cost, and it is the one Thomas named: an operator who does not
read this page never learns the feature is there.  So the node says so.
The first time a modulo-128 call is heard on a port that does not ask for
one, it reports it - with the packet header, so it says who was heard and
over which path:

    EAX25 heard on ax0. Consider "ifconfig ax0 eax25 caller".
      Packet: DB0AAA-5->TEST-1

It goes to the console **and** to the log, because neither reaches
everyone on its own: `logmsg()` returns at once when no `log` file is
configured, and the console is nothing on a node started without one.
What survives in either case is the `E` beside the station in
`ax25 route list` - the pull half of the same answer, for the operator who
goes looking rather than watching.

Once per **port** and per run, not per station, and never on a port
already set to ask.  The advice names the port, so repeating it for every
caller adds nothing - and on a user access carrying many stations, or when
the far side retries its SABMEs through us as a digi, it would be a
nuisance rather than a hint.

It is reported on `off` as well, not only on `accept`: the operator said
no once, and what he is hearing now may be a partner that has since been
replaced.  That turns the conservative default into one
that advertises itself, from what the node actually heard rather than from
documentation.

**An interlink therefore wants `caller` at least**, because we open those
links ourselves - `netrom links ... permanent`, `flexnet link add` - and
under `accept` we never ask.  That is also the right way to think about
such a link: it carries NET/ROM *and* FlexNet *and* IP *and* plain text,
so its modulus is a property of the **link**, not of any one session.
`always` on an exclusive interlink is not an aggressive setting, it is
simply the statement "this link runs modulo-128".

`off` - never.  An incoming SABME is answered with DM, and we never send
one.  For an interlink whose partner is known not to speak it, so that not
a single probe is wasted.  It is not a way to save bandwidth: the extra
cost of modulo-128 is one octet per I and S frame, 0.4 % at `paclen` 256.

`caller` - ask once when the connect starts here, and when relaying, do
what the caller asked for: **a caller who asked for plain AX.25 is carried
onward as plain AX.25**.  Not because asymmetry is
harmful in itself - we terminate and acknowledge hop by hop, so the two
halves are independent anyway - but because of where the control sits.  If
we upgraded his link on the leg beyond us and that leg then misbehaved, he
could do nothing about it: he already used the most conservative thing he
has.  Downgrading is always safe, upgrading is not.  A connect that starts
here has no caller to follow and tries modulo-128 once.

`always` - upgrades him anyway.  This is for an exclusive interlink at a
higher bit rate, where the wider window is worth most and the operator
knows the partner.  **Do not put it on a port whose routes run through
digipeaters**: our fallback timing scales with the digi count of *our*
path while the caller's patience scales with *his*, and if ours is the
longer one he gives up before we have fallen back.  Same class of
configuration error as `advert no` on a node with several uplinks.

Where that begins to bite is arithmetic, not judgement.  Three probes cost
`T1 (1 + 1.25 + 1.5625)` with `T1 = 5 s x (1 + 2 x digis)` counted on *our*
onward path, against a caller who spends ten retries on his:

| digis on our onward leg | our three probes | a caller with no digi gives up after |
| --- | --- | --- |
| 0 | 19 s | 166 s |
| 2 | 95 s | 166 s |
| 4 | 171 s | 166 s |
| 8 | 324 s | 166 s |

So one or two digipeaters are survivable and four are not.  It is also the
one place where `always` differs from `caller` in kind rather than in
degree: under `caller` a plain caller is never probed at all, so the
question does not arise.

`paclen`, `maxframe` and `emaxframe` can now be set **per port** as well,
with 0 meaning "use the node's".  They are properties of the channel, and
a node with a 1k2 user access and a 19k2 interlink wants two different
answers.  Whatever is set, the driver's own limit still wins: `struct
iface` has `framemax`, the driver declares it at attach, `ifmtu()` refuses
to be configured past it and `ax25_apply_iface_limits()` clamps the packet
length to it.  6pack declares 510 - `SIXP_MAX_FRAME` bounds the decoded
frame, and `sixpack_encode()` used to drop what it could not hold without
a word.  The NET/ROM pseudo-interface declares `NR4MAXINFO`, so
`ifconfig netrom mtu 1500` is now answered rather than obeyed.

## Falling back

A station that cannot do modulo-128 answers the SABME with DM - or, and
this is the older and more common kind, drops it without a word.  Both
have to work, and the second only over the clock.

* **DM or FRMR**: fall back at once.  A DM to the plain SABM that follows
  then means what a DM normally means - no service.
* **Silence**: fall back after **three** probes.

Three, and not the ten that N2 allows, because of the arithmetic on the
other side.  With `T1init` 5000 and the 1.25 backoff in `recover()`, three
probes take about 19 s, while the caller waiting on us gives up after 166 s
(and a Linux peer only after ~550 s, because the kernel falls back only
when its *whole* N2 cycle is spent - see `ax25_std_timer.c`).

Falling back restarts the retransmission timer but **not** the retry
count.  The probes proved that he does not speak modulo-128; they said
nothing about how long the path is, so carrying their stretched T1 into
the plain attempt would punish the connection for the wrong reason.
Leaving the count alone means the probes are paid out of the tries the
caller was already going to spend: three SABMEs and the seven SABMs that
remain come to about **94 s**, where ten SABMs alone take 166 s.  Trying
modulo-128 and failing therefore ends *sooner* than not trying.

## What is remembered, and what is not

One place: `eax25` in the AX.25 route entry, three-valued - not tried,
can, cannot.  Three and not two, because "cannot" has to be told apart
from "never asked": with two, every connect probes again, and the station
that answers nothing costs the full probe each time.

It is learned from **our own traffic only**.  The EAX bit a station sets
in its SSID says it is willing, not that it works, and TNN's answer -
`EAXMODE 1`, "by MHEARD" - would need a small state machine per station to
be sure (SABME, UA, data both ways, confirmed).  The route entry knows the
same thing more cheaply, because it records the outcome rather than the
claim.

What counts as evidence:

* UA to our SABME, or a SABME **from** him - he can.  The second needs no
  probe at all and is what undoes a "cannot" the moment his end is fixed.
* DM or FRMR on a modulo-128 attempt - he cannot.
* Silence, **and then a UA to the plain SABM** - he cannot.  Silence alone
  is not enough: it is equally consistent with a station that is simply
  away, and marking an absent neighbour as incapable would stick to him
  for as long as his route lives.

It is deliberately **not** written to `axroute_data`.  A restart is
exactly when asking again is right, because the far end may have grown new
hardware meanwhile, and one probe is all it costs.  In memory it ages with
the route: `axroute_savefile()` frees entries untouched for
`AXROUTE_HOLDTIME`, which is 24.8 days, and it runs every ten minutes.

`ax25 route list` shows it as `E` (can) or `e` (cannot) next to `P` and
`J`.

## Measured

Rig: `testtools/eaxpeer.py`, which plays the far end in four ways -
calling us with SABME or SABM, and answering ours with UA, DM or silence.

| | result |
| --- | --- |
| peer calls with SABME | UA, modulo-128, 108 894 bytes in 426 frames, exact |
| 528 894 bytes | 2066 frames, 16 wraps of the sequence number, byte-exact |
| window over a long transfer | 31, then 32, and steady there |
| peer answers DM | SABME → DM → SABM → UA, no delay at all |
| peer stays silent | SABMEs at 2.5 / 6.8 / 13.1 s, SABM at 20.9 s |
| the same via one digi (T1 15 s) | SABMEs at 2.5 / 16.8 / 35.6 s, SABM at 59.0 s |
| second connect after a "cannot" | straight to SABM, no probe |
| `eax25 off`, outgoing | SABM at once, never a probe |
| `eax25 off`, incoming SABME | answered with DM |
| plain AX.25, 20 % loss | unchanged - 13 893 bytes in 55 frames |
| `-fsanitize=undefined` over all of it | no findings |

The sanitizer matters here more than usual: modulo-128 pushes sequence
numbers to 127 and wraps them, which is the arithmetic that had just been
found broken in `timer.c` and `tcpin.c`.  Two `char` variables holding
control fields were widened to `int` for the same reason - at modulo-8 the
value never exceeded 0xEE and survived only because the mask below cut the
sign extension off again.

## `always`, and the relay it needs

`always` only ever shows itself on a link we open on someone **else's**
behalf, so proving it needs the node in the address field as a digipeater.
WAMPES does not repeat a connected frame: it terminates both halves and
calls onward itself (`Digipeat == 2` in `ax25.c`), carrying the caller's
callsign as the source of the second leg, so the far end sees *him via us*.
`testtools/eaxpeer.py --via` builds that path and `--zielstation` plays the
far end on a socket of its own - not for convenience but because `axip`
remembers the UDP source port per host and interface, so two stations on
127.0.0.1 sharing an interface take each other's frames.

Rig: caller on `ax0` (`eax25 accept`), far end reached over `ax1`, one node
between them.

| `ax1` | caller arrives with | second leg goes out as | the two halves |
| --- | --- | --- | --- |
| `caller` | SABM | SABM | modulo-8, modulo-8 |
| `caller` | SABME | SABME | modulo-128, modulo-128 |
| `always` | SABM | **SABME** | modulo-8, **modulo-128** |

Through the upgraded relay: 200 frames of 256 bytes, 51 200 bytes, counting
straight through with `N(S)` past 127 and round again.  `ax25 status` shows
`Mod 128` and `Unack 0/32` on the second leg beside `Mod 8` on the first,
and `E` appears at the far end in `ax25 route list`.  The fallbacks hold
there too:

| far end | what happens | the caller |
| --- | --- | --- |
| answers DM | SABME → DM → SABM → UA, no delay at all | connected at once |
| stays silent | SABME, then the plain SABM at 19 s | connected at 20.2 s |
| after either | `e` in the route table | next call goes straight to SABM |

**One probe, not three, on a relayed leg** - and this is worth knowing
before reading the table above as a contradiction.  `recover()` does not
retransmit for a link whose peer is still disconnected: it restarts T1 and
gives up after three expiries, because the retry that matters there is the
caller's.  What sends a second SABME is his next SABM, which runs
`build_path()` and `sendctl()` again while `routing_changes < 3`.  So a
caller who never retries costs exactly one SABME and 19 s of quiet; a
WAMPES-like caller at T1 15 s produces two, at 0 and 15 s, with the
fallback at 20 s either way.  That is one station's patience being spent
instead of two, which is the intent - but it does mean a lost SABME on a
real channel is not made good by us.

**The monitor still reads modulo-8.**  `ax25dump.c` decodes a one-octet
control field and cannot do better from the frame alone: nothing in an I
frame says which modulus its link runs on.  On a modulo-128 link the trace
therefore shows `N(S)` wrapping at 8, a `P` that is really bit 4 of `N(S)`,
and `pid=0x0` because the PID sits one octet further on.  The control block
knows (`axp->mmask`), so a dump that looked the link up could get it right;
today it does not, and `ax25 status` - which prints the modulus per link -
is the display to trust.

## What a larger packet length does to the rest

Asked because `ax25 paclen` accepts up to 32767 and always did.  Measured
at 1024 over axip: 108 894 bytes in 107 frames of 1024, 75 097 B/s against
63 335 at 256.  Nothing else moves:

* **NET/ROM nodes broadcast** caps itself in `send_broadcast()` -
  `if ((bp->cnt = p - bp->data) > 258 - NRRTDESTLEN)` closes the frame and
  starts another.  258 octets, hard-coded, `paclen` never enters into it.
* **FlexNet** the same, at `LENROUT` = 256: `flexnet.c:460` breaks a ROUT
  and begins a new frame at `LENROUT - 14`.  `FLEX_POLL` is 201 by
  definition.  Measured at 256 and at 1024 - identical.
* **NET/ROM frames** cannot exceed 256 either way: the L3 and L4 headers
  are fixed and the information field is `NR4MAXINFO` = 236.
* **IP** fragments on the interface MTU, which is a separate number.
* **Segmentation** is only ever reached from `ax25.c:147`, IP over AX.25
  in connected mode, and a *larger* `paclen` means *fewer* segments - the
  seven-bit counter gets further away, not closer.  Measured on a
  modulo-128 link with MTU 1500: a 1421-octet datagram arrived as 6
  segments at `paclen` 256 and as 3 at a per-port `paclen` 512,
  reassembled to the byte both times.

So a larger packet length is the safe direction.  The dangerous one is a
*small* `paclen` with a large MTU - at 12 or less an ordinary 1500-octet
datagram needs more than the 127 segments the counter can express, which
`segmenter()` already refuses rather than truncating.
