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

    ifconfig <if> dama off | slave [timeout <seconds>]      default 120
    ifconfig <if> arp on | off                             see below

The timeout belongs to the role and not beside it (Thomas), and it took a
second look to make that possible: `ifconfig` reads its line as key/value
**pairs** - `for(i=2;i<argc-1;i+=2)` in `iface.c` - so a third word silently
became the next key, which is why this was two commands at first.  There is
already a mechanism for a setting that reads more than one value, and `pid`
uses it: `if_wants_rest()`.  `dama` now uses it too, with the one consequence
that comes with it - **it has to be the last setting on the line**, because
everything after it is its own.

The timeout means what only a slave has: how long a master may be silent
before we stop following him.  A master's own timers - the round, the wait
for the polled station - are different quantities and will get their own
words rather than borrow this one.

**120 seconds, for two reasons.**  TNN uses the same two minutes (`damaok =
12000` in hundredths, `l2rx.c`), and the specification names no figure at
all.  The better reason is the channel: a master polls its stations in turn,
and sixteen stations at two to five seconds each make a round a minute long
(Thomas).  A shorter watchdog would declare a master lost who is merely
working through his list.

**`dama slave` switches ARP requests off on that port**, and says so.  A
request is a broadcast to QST and costs the channel; the answer is to enter
the partners with `arp add <ip> ax25 <call>`, and then none is needed.  Only
the *asking* is affected - an incoming request is still answered.  `dama off`
puts it back **if we were the ones who switched it off**; an `arp off` the
sysop gave himself is left alone.  `ifconfig <if> verbose` shows the state in
the AX.25 block, where it belongs: it is a property of the port, and on one
that runs no DAMA it would otherwise be invisible.

There is no channel or group setting, and that is not an omission.  Grouping
ports into one DAMA channel is a **master** concern: only the master has a
polling cycle, and only it must serialise the uplink across ports that share a
frequency.  A slave answers polls addressed to it, on whichever port they
arrive, and does not care how the master organises itself.

## The permission belongs to the station, not to the connection

Both references say so.  TNN's `sendok` is per **port** and `l2dama.c` rotates
over the links of a station with `zael/indx`; its manual states the
consequence - *"USER mit Multiconnect kommen gegenueber USERN mit nur einer
Verbindung zum Knoten nicht oefters an die Reihe"*.  One station, one turn in
the cycle, its links sharing it.

A window per connection looks right while there is only one connection on the
port.  What exposes it is **digipeating through the master**: there the master
is not an endpoint of our connection at all, so it cannot poll "that link" -
it can only give the station its turn.  `ntohax25()` reads the DAMA bit from
`hdr->source` and never from a digipeater field, so nothing about such a frame
identifies the master either.

So the window is opened by a poll, and every link **under the callsign that
was polled** is served in it.  All of them, not one in rotation: TNN's slave
empties every link when the gate opens (`l2dama.c`, `for (lnkpoi = ...) {
damatx(); xmit_damail(); }`) and Linux does the same in
`ax25_ds_enquiry_response()`.  Fairness between one station's several
connections is the master's business, exercised by how often it polls that
station; a slave rationing itself as well would ration a turn that was
already rationed.

**Under the callsign, not on the port**, and this is where both references
would mislead us.  What a master gives a turn to is a **callsign**: several
links of one callsign are multiconnect, which it knows about and counts as one
station.  A link under a *different* callsign is a different station to it,
and one of those transmitting on somebody else's poll is precisely the
unrequested transmission that gets counted and eventually disconnected on a
node that enforces DAMA.

The references get away with "the port" because there a device carries one
address - Linux compares `ax25o->ax25_dev` and nothing else.  WAMPES answers
on many callsigns per interface; that is what `listen` is for.  Measured both
ways, with the master polling TEST-1:

| | what happens |
|---|---|
| TEST-1 and TEST-2, two callsigns of ours | only TEST-1 transmits |
| two links, both under TEST-1 | both transmit - one station, one turn |

**Only the master's polls count.**  The port remembers the callsign of the
station whose frames carried the DAMA bit, and a command with the poll bit
from anybody else is not a poll.  Without that, any neighbour would hand us
the channel - the opposite of the point, since on a DAMA channel exactly one
station decides who transmits.

**And we mark our own frames.**  On a port declared `dama slave`, our source
SSID carries the bit too.  That is what makes digipeating work at all: when a
user connects to DL1AAA *through* the master rather than connecting to the
master and working onwards, the master is only a digipeater for that link and
has nothing else to tell it we are a DAMA station.  A master that tracks every
connection hop by hop - which is what WAMPES does - can then put us in its
polling list.  Marked because the sysop declared the channel, not because a
master happened to be heard: only the first is a statement we may make about
ourselves.

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
| `recover()` - T1 retransmission | nothing on the air; noted for the next poll - see below |
| `ax_t5_timeout()` - idle disconnect | deferred; the link is idle anyway |
| the close at the end of `lapb_input()` | deferred - the paper says a user waits for the poll before sending DISC |

`resequence()` already split *"polled, answer now"* from *"not polled, start
T2"*, which is the same shape as the kernel's `AX25_COND_ACK_PENDING`.  All
that was missing was for T2 to hold its tongue.

**The rule that falls out of that list**, and it is worth stating rather than
leaving implicit in which call sites are gated:

> An immediate answer to a command with the poll bit goes out at once.
> Anything we start ourselves waits for the poll.

So a UA answering somebody's SABM leaves immediately, even when that somebody
is not the master, while the acknowledgement for the data he then sends is
held until the master's next poll.  Measured, with a third station connecting
while a master runs the channel:

     1.0s  DL1XXX -> SABM+P          (not the master)
     1.0s         <- UA P/F          at once
     2.0s  DL1XXX -> I 'hallo'       no poll
     2.0s         (nothing back)     the acknowledgement is held
     2.6s  DB0AAA-5 -> RR+P          the master polls
     2.6s         <- to DL1XXX: I N(S)=0 N(R)=1    the ack rides out here

It has to be this way round.  The paper puts the connect handshake in CSMA
explicitly, and gating the answer has a failure mode with no way out: a
station with no link of its own to the master is never polled, so it could
never answer an incoming connect at all - unreachable, for ever.

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

## The discipline is per link, not per port

A station may call us on a DAMA port and never set the bit.  That link then
has nothing to do with the master, and gating it by the port would stall a
contact that nobody is managing - until the watchdog let go and put it back a
moment later.

So the discipline follows the **link**: a marked frame arriving on a
connection puts that connection under it, and one that nobody ever marked is
left alone.  That is also what the paper says, and the sentence is the same
one that settled the latching question - *"it would be sufficient to tell the
user to switch to DAMA mode only once, at connect time.  **This state would
then remain in effect until disconnect.**"*  The kernel keeps the flag in the
control block for the same reason (`AX25_COND_DAMA_MODE`), and the flag is
cleared when the link ends, so a reused control block earns its discipline
again rather than inheriting it.

Measured on a port with a master running: an unmarked caller is answered and
served at once, while the master's link is still held until its poll.

**Our own marking stays on every link of the port**, because it says "we speak
DAMA", not "this connection is DAMA" - that is the whole point of it, telling
a master that only repeats for us.  The consequence is worth knowing: any
station that reads the bit will take us for a master.  A DAMA-capable user TNC
that calls us will gate itself and wait for polls we never send, until its own
watchdog gives up.  On a DAMA channel that is the right outcome - see the next
section - but it means `dama slave` on a port also says "direct contacts here
are not expected to work".

## Two slaves that connect to each other lock each other out

Because we mark our own frames, another WAMPES slave reads them as "a master
is here" and gates itself - and we do the same with its frames.  Neither ever
polls the other, so both wait until the watchdog runs out, transmit, and put
each other straight back into DAMA mode.

That is **deliberate, and it is the wanted behaviour**: on a DAMA channel
potential slaves do not talk to each other.  If they must - the digi is off
and the contact matters - they say so:

    ifconfig <if> dama off

One line, rather than a switch nobody would understand.  Two DAMA masters on
one frequency, which is what a marking slave amounts to, would be the worse
answer.

XNET arrives at the same place from the other side.  Its `ds` parameter -
*"allow DAMA slave mode"* - exists because *"Der Slave-Mode wird
vollautomatisch beim Verbindungsaufbau zu einem Master aktiviert.  **Bei Digis
ist diese automatische Aktivierung des Slave-Modes nicht erwuenscht.**"*  A
node that is itself infrastructure should not be pushed into the slave role by
somebody else's DAMA bit, which is why the default here is `off` and
`dama slave` is a permission rather than a description.

**And nothing rescues such a station from outside.**  A master polls the links
it *has*; it has none with a callsign that only talks to a third party, and an
RR on a connection that does not exist would come back as DM or FRMR.  There
is no way for a master to adopt a station it is not part of.

## Retransmission happens in the poll window

The first version suppressed T1's retransmission and stopped there.  On a
clean channel that is invisible.  Measured on one with 30% loss in both
directions, it was not: after the first lost frame the node answered every
poll with a bare RR and its data never moved again.  **A DAMA slave that only
stays silent never retransmits anything.**

The fix has the same shape as everything else here - the timer notes, the poll
delivers.  When T1 expires with frames still outstanding, that is not merely
waiting: the master has had a whole T1 to acknowledge them and has not, so
they were lost.  The expiry marks them for retransmission and counts a retry;
the retransmission itself happens in the window the next poll opens.

With nothing outstanding, nothing is counted.  That distinction - a link that
is failing versus one that is waiting to be asked - is the whole reason we do
not die where Linux does.

The paper wants T1 above the poll interval for exactly this reason, and WAMPES
gets there by itself: `srt` is measured over send-to-acknowledge, which under
DAMA spans a full cycle, so T1 grows to fit the channel.

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

And the fallback, with `dama slave timeout 5` (it was `damatimeout 5` when
this was measured, before the two were folded together), the master falling
silent after one I
frame at 1.6 s:

    3.6s   nothing
   11.9s   0x31 P/F N(R)=1      <- watchdog expired, T1 opened the gate
   13.2s   0x31 P/F N(R)=1      <- ordinary T1 retry, as before DAMA

(`secclock()` counts whole seconds, so which T1 period notices the expiry
depends on where the tick falls - the behaviour is the same either way.)

Two connections over one DAMA port, `TEST-1` polled and `TEST-2` never polled
by the master at all:

     2.0s  -> RR+P (Poll 0)
     2.0s  <- TEST-1: RR P/F, then I N(S)=0
     2.0s  <- TEST-2: I N(S)=0          <- the rotation, on a link never polled

And with 30% loss both ways, the retransmission arriving in a later window:

     2.0s  -> RR+P (Poll 0)
     2.0s  <- TEST-1: I N(S)=0 '1\r2\r3\r...'   (lost on the way)
     4.0s  -> RR+P (Poll 1)      <- RR only, T1 running silently
    12.0s  -> RR+P (Poll 5)
    12.0s  <- TEST-1: I N(S)=0 '1\r2\r3\r...'   <- same frame, resent
    20.1s  <- TEST-1: I N(S)=1                  <- and onwards

Every transmission by the node in that run sits on a poll that reached it.
Tool: `testtools/damachan.py`, which plays several stations on one socket and
can drop frames in either direction.

## What it costs

Measured on a loopback with a poll every 2.0, 0.5 and 0.1 seconds, against
the same node with DAMA off:

| | payload | I frames | S frames | total |
|---|---|---|---|---|
| off, poll 2.0s | 13893 B | 55 | 22 | **77** |
| on, poll 2.0s | 12544 B | 49 | 11 | **60** |
| on, poll 0.5s | 13893 B | 55 | 36 | **91** |
| on, poll 0.1s | 13893 B | 55 | 102 | **157** |

The payload rate barely moves, but that is an artefact: the bottleneck here is
the program producing the data and a UDP loopback with no air time.  These
numbers cannot say what DAMA costs on the air.

What they do show is the ratio, and on HF that is the currency, because every
frame costs a keyup and a TxDelay: at a tenth-second poll the same payload
takes **more than twice the frames**.  Each poll costs one RR from us and one
poll from the master.

So on an idle channel DAMA costs, and the poll interval is the knob - polling
fast burns air on RRs, polling slowly adds latency and caps a station at about
`(frames per poll x paclen) / cycle`.  On a loaded channel with hidden
stations it is the other way round, which is the paper's whole claim: *"the
throughput will increase continuously up to its maximum.  There is no foldback
effect like that which occurs using CSMA where at a special limit (above ca
60%) the throughput is actually reduced."*  Latency of one station traded for
throughput of the channel.

**The supervisory frame is not avoidable, and working out why settled a
question that had been open here.**  It looks redundant: we answer the poll
with an RR and then send I frames whose `N(R)` acknowledges anyway, and the
paper says as much - *"having the correct count on the sent I-frame serves the
same purpose as an ACK"*.  But that sentence is about **acknowledgement**, not
about answering a poll, and the two are not the same thing.

In AX.25 an I frame is **always a command**.  Only supervisory and U frames
can be responses, so only they can carry F.  The asymmetry follows:

* A **master initiates** the poll transaction, so it may put the P bit on an I
  frame - data and turn-giving in one.  TNN does exactly that: `damatx()`
  generates only I frames, and the caller falls through to a poll just `if
  (damatx() == FALSE)` - *"Keine Info zum senden gefunden, also Poll senden!"*
* A **slave responds**, and no I frame can be a response.  The supervisory
  frame is the only thing that can close the poll; the I frames go out beside
  it as commands.

Both references do it that way round.  Linux's `ax25_ds_enquiry_response()`
calls `ax25_std_enquiry_response()` first and only then `ax25_kick()`.  So the
RR is not a wasted frame - it is the answer, and the data is separate by
construction.

Two things fell out of reading that function, and both are worth having:

* **It walks every other connection on the same device** and kicks those too,
  which is the same conclusion TNN's slave reaches by a different route.  Two
  independent implementations agreeing that the window belongs to the station
  is as much confirmation as this is going to get.
* **It requeues unacknowledged frames on every poll** -
  `ax25_requeue_frames()` before the kick - where we only resend after T1 has
  said something was lost.  Linux recovers faster and sends more; we send less
  and recover a T1 later.  Both defensible, and the difference is written down
  here so that whoever meets it at a real digi knows it was a choice.

## Not built

The master.  See `TODO.txt`: it needs to know when its own transmission ended,
which is carrier detect - 6pack reports it, KISS does not - or, on a full
duplex port, a bit rate to compute it from.  `ifconfig <if> dama master` says
so rather than pretending.
