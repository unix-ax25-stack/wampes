# DAMA slave: speak only when spoken to

On a busy access frequency the users hear the node but not each other.  So
they transmit on top of one another, and the weak stations never get through
at all - the digi's receiver is fine, it is simply hearing three people at
once.  DAMA takes the decision away from the users: the node polls, and a
station transmits only when it has been polled.

Detlef Schmidt, DK4EG, described it in 1989 (TAPR CNC, *"DAMA - A New Method
of Handling Packets?"*).  It needs no new frame types and no new syntax, which
is why it can be added to a working AX.25 without changing anything for the
stations that do not speak it.

**This is the slave half.**  The master is the larger piece and is not built;
`TODO.txt` says what it needs that a KISS line does not give it.

    ifconfig <if> dama off | slave
    ifconfig <if> damatimeout <seconds>          default 120

Two keys rather than one with an optional second word, because `ifconfig`
reads its line as key/value **pairs** - `for(i=2;i<argc-1;i+=2)` in `iface.c`
- and a third word silently becomes the next key.

There is no channel or group setting, and that is not an omission.  Grouping
ports into one DAMA channel is a **master** concern: only the master has a
polling cycle, and only it must serialise the uplink across ports that share a
frequency.  A slave answers polls addressed to it, on whichever port they
arrive, and does not care how the master organises itself.

## What marks a DAMA channel, and what hands us the turn

Getting this wrong costs the connection in either direction - too strict and
we never answer, too loose and we transmit unbidden and get disconnected by a
node that enforces DAMA.  So it is worth being exact, and it is two separate
things:

| | |
|---|---|
| the **DAMA bit** in the **master's** SSID octet | this port is on a DAMA channel |
| **command**, with the **poll bit** set | and this frame hands us the turn |

The DAMA bit is active low on the wire - normally that octet carries `0x60`, a
DAMA master sends `0x40` - and `ntohax25()` has read it into `hdr->ext` since
long before any of this.  The provision was there; nothing used it.

**It is the master's SSID, so on a connection the user placed it arrives in
the UA**, not in the SABM - the SABM is ours.  Only when the node calls the
user does it come in a SABM.

**We latch rather than require it per frame**, and that is a deliberate
choice between the two existing implementations, which differ here:

* TNN wraps its poll test in `if (rxfDA)` (`l2rx.c`) and so needs the bit on
  every polling frame.
* Linux latches the mode at connect (`ax25_dama_on`) and afterwards looks only
  at `command && pf` (`ax25_ds_in.c`).

The paper allows either - *"it would be sufficient to tell the user to switch
to DAMA mode only once, at connect time.  This state would then remain in
effect until disconnect."*  A master may therefore legitimately mark nothing
after the UA, and a slave that demanded the bit per frame would stay silent
against it for ever.  Accepting both costs nothing, because the watchdog
already covers the other direction: a master that stops speaking DAMA
altogether stops feeding it.

Measured, with a master that marks the SABM and nothing afterwards: the slave
still holds its acknowledgement and still answers the poll.

The paper is separately emphatic that the word "poll" does **not** mean the P
bit.  That is a remark about vocabulary, not about the wire: a master that
wants an answer sets P, because that is what ordinary AX.25 does, and both
implementations key off it.

**And where the paper and the practice disagree, we follow the practice.**
DK4EG writes that *"the user will acknowledge I-frames immediately with an
RR#"*.  Neither implementation does: an I frame without the poll bit grants
nothing, the acknowledgement is only noted and goes out on the answer to the
next poll.  The reason is throughput - a master serving several stations in
turn does not want acknowledgements arriving in the gaps, where they collide
with whoever is being served next.

## UI frames are outside all of this

The specification takes datagrams out of the poll discipline explicitly:

> *"In CSMA as well as in a DAMA environment, the UI frames are treated in a
> special way ... Normally UI-frames are never sent from a user to a node, and
> it is not good headwork to make a habit of making UI-frame direct QSOs on
> the input frequency of a node.  However, in contrast to a duplex system it
> is possible to actually do this.  So although the rare UI-frames will reduce
> the throughput to the CSMA value, it will not drop to the much lower ALOHA
> value ... UI-frames originated by the node are no problem since all stations
> receive these frames."*

Discouraged, but permitted, and subject to no permission.  Here that follows
from where the gates sit rather than from any decision: they are all in
`lapb.c` and `lapbtime.c`, which is connected mode.  UI leaves through
`ax_output`/`axui_send` and touches none of them, so beacons, APRS, NET/ROM
broadcasts and IP over UI carry on exactly as before.

## Where the gate sits

The window in which a slave may transmit is **the handling of the polling
frame**, and that turned out to be all the machinery needed.  `lapb_input()`
already ends with a call to `lapb_output()` - *"see if we can send some data,
perhaps piggybacking an ack"* - so a gate that is open exactly while a poll is
being processed lets everything waiting go out at the right moment.  Outside
that window the timers fire into a closed gate, which is the rule the protocol
asks for.

| what would have transmitted | what happens instead |
|---|---|
| `lapb_output()` - the send queue | frames stay on `txq`, the next poll takes them |
| `ax_t2_timeout()` - the delayed ack | stays delayed; the ack rides out on the poll answer |
| `pollthem()` - T3/T4 idle poll | nothing; asking is the master's job |
| `recover()` - T1 retransmission | nothing, and no retry counted - see below |
| `ax_t5_timeout()` - idle disconnect | deferred; the link is idle anyway |
| the close at the end of `lapb_input()` | deferred - the paper says a user waits for the poll before sending DISC |

`resequence()` already split *"polled, answer now"* from *"not polled, start
T2"*, which is the same shape as the kernel's `AX25_COND_ACK_PENDING`.  All
that was missing was for T2 to hold its tongue.

## The silent T1, and where we part company with Linux

A DAMA slave does not retransmit of its own accord.  T1 keeps **running**,
because its expiry is still the measure of how long the master has been
silent, but the expiry itself does nothing: no retransmission, no enquiry, and
**no retry counted**.  The connection is not failing, it is waiting, and
counting the wait as failure is what would eventually kill it.

Linux stays silent too - `ax25_ds_t1_timeout()` in state 3 only increments
`n2count` - but it **does** count, and at N2 it sends DM and disconnects.
There is a second, device-wide watchdog that does the same to every DAMA
connection at once.  Both end in a teardown, and a teardown costs the user
everything above AX.25: the mailbox login, the article he was halfway through,
the lot.  Re-entering is not the AX.25 handshake, it is all of that again.

So when the master stays away, we **fall back to ordinary operation** instead.
T1 then does what it has always done, and the link repairs itself.  TNN takes
the same side: when its slave timeout expires it sets `sendok = 1` and
transmits.  Linux is the outlier here, not us.

The timeout is 120 seconds by default, which is TNN's figure (`damaok = 12000`
in hundredths, `l2rx.c`).  The specification gives no number at all, so there
is nothing to be faithful to; it is settable per port because it is exactly
the kind of number that differs between one digi and the next.  Any
DAMA-marked frame feeds it, not only a poll - again as TNN does.

## The bug the measurement found

The watchdog is a comparison, not a timer.  That was deliberate - "every
question that matters is asked from a timer that is already running" - and it
was **wrong**, which only showed up under test: a slave with a held
acknowledgement sat silent long past its timeout.

Measured, and then read: `ackours()` stops T1 the moment nothing is
outstanding, T2 had already fired once and been dropped by the gate, T3 is
`900000` ms away and T5 `3600000`.  **No timer was left alive to notice that
the master had gone.**  A gate that returns without rearming its timer
switches off the very thing that would have opened it again.

So the held branches now keep a clock running: `recover()` and `pollthem()`
rearm their own timers, and everything that holds something back - a pending
acknowledgement, a full send queue, a disconnect that wants to happen - calls
`dama_wait()`, which makes sure T1 is running.  A link with nothing to say
still needs no timer, because it has nothing to be woken for.

## Measured

A simulated master (`testtools/damamaster.py`) against the node, with the same
script run twice.  What the slave says, and when:

| | `dama slave` | `dama off` |
|---|---|---|
| SABM+P (DAMA marked) | `UA P/F` | `UA P/F` |
| I frame, **no** poll | *nothing* | `0x21 N(R)=1` after 0.3 s (T2) |
| RR+P - the poll | `0x31 P/F N(R)=1` | `0x31 P/F N(R)=1` |
| I frame, **no** poll | *nothing*, and nothing for 5 s more | `0x41 N(R)=2` after 0.3 s |
| RR+P - the poll | `0x51 P/F N(R)=2` | `0x51 P/F N(R)=2` |

The held acknowledgement is not lost - it is carried by the answer to the next
poll, which is what the `N(R)` in those replies shows.

And the fallback, with `damatimeout 5`, the master falling silent after one I
frame at 1.6 s:

    3.6s   nothing
    6.9s   0x31 P/F N(R)=1      <- watchdog expired, T1 opened the gate
   13.2s   0x31 P/F N(R)=1      <- ordinary T1 retry, as before DAMA

## Not built

The master.  See `TODO.txt`: it needs to know when its own transmission ended,
which is carrier detect - 6pack reports it, KISS does not - or, on a full
duplex port, a bit rate to compute it from.  `ifconfig <if> dama master` says
so rather than pretending.
