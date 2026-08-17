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

    ifconfig <iface> eax25 off | caller | always      (default: caller)
    ax25 emaxframe <1..63>                            (default: 32)

`off` - never.  An incoming SABME is answered with DM, and we never send
one.  For an interlink whose partner is known not to speak it, so that not
a single probe is wasted.  It is not a way to save bandwidth: the extra
cost of modulo-128 is one octet per I and S frame, 0.4 % at `paclen` 256.

`caller` - the default, and the whole rule is: **a caller who asked for
plain AX.25 is carried onward as plain AX.25**.  Not because asymmetry is
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

Both `maxframe` and `emaxframe`, like `paclen`, are one number for the
whole node.  A node with a 1k2 user access and a 19k2 interlink wants two
different answers and cannot have them; that is older than this work and
is noted in `TODO.txt`.

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

`always` is built but **not** measured: proving it needs a rig that
digipeats through us, which `eaxpeer.py` does not do yet.
