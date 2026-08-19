# INP3: routing by measured time, beside the graph

NET/ROM tells its neighbours about destinations in *quality*, a number
between 0 and 255 that nobody measures.  In WAMPES every direct
neighbour is given the same one, `nr_hfqual`, which defaults to 192 -
a constant standing in for how good a link is.

INP3 replaces the number with a measurement.  It is the Internode
Protocol described in *A New Routing Specification for Packet Radio
Datagram Networks* (Andreas Gal, DB7KG, TheNetNode-Group /
NORD>|<LINK), and it is deliberately drop-in compatible with NET/ROM:
same PID `0xCF`, same layer 4, same node table.  What changes is how a
route is described and how it travels.

* **Described** as a *route time*, in units of 10 ms, up to 599.99 s.
  Our own measured time to a neighbour, the *SNTT*, is added to what he
  reports, and the sum - the *target time* - is what routes are ranked
  by.  Small is good.
* **Travelling** on a numbered AX.25 connection to a named partner, the
  *interlink*, instead of UI frames to everybody on the port.
* **Only when it changes.**  There is no periodic full broadcast.
  Silence means nothing has changed.

None of this replaces NET/ROM here.  Both run at once, and the point of
the design below is that they never have to be converted into one
another except at the moment a frame is written.

## Setting one up

    netrom peer                     list
    netrom peer add <call>
    netrom peer del <call>

That is the whole configuration.  There is no `start inp3`, because
INP3 is not a service: same PID, same node table, same layer 4, same
AX.25 connection.  It is a mode of operation per neighbour, and `start
netrom` starts it with everything else.

**Written without an SSID the entry matches any.**  The callsign an
interlink runs under and the node's own ID need not carry the same one,
so `netrom peer add DB0XYZ` accepts DB0XYZ-4 and DB0XYZ-7 alike, while
`netrom peer add DB0XYZ-4` is exact.  Until we have heard of him at all
we do not call him - the listing then says `no call yet` rather than
`down`, because an entry waiting for its station is not a failing one.

**This is not the route filter and does not overlap with it.**
`netrom peer` says whom we speak to; `netrom filter` says what flows
once somebody speaks to us.  `doc/ROUTE-FILTER.md` makes the same
distinction for the FlexNet poll.

    netrom peer
    Call       SSID   Interlink   INP3   SNTT     His      Last     Known as
    DB0XYZ-4   exact  Connected   <30s   0.21s    0.18s    0.14s    DB0XYZ-4 (neighbour)
    DB0ZZZ-2   exact  Connected   yes    0.03s    -        0.02s    DB0ZZZ-2 (neighbour)
    DB0AAA     any    no call yet -      -        -        -        -

`INP3` is what he said about himself: `yes` for his `$N`, `<30s` for
`$N` with a `$M3000` ceiling, `-` for a partner who has not offered
INP3 at all.  `SNTT` is our measurement of the link, `His` is his
measurement of it, and **a dash under `His` is the whole explanation
for a partner who is up, has agreed, and is being told nothing** - see
below.

## The negotiation is one word

There is no handshake.  Each side sends an `L3RTT` frame, the other
reflects it unchanged, and the round trip halved is the SNTT.  In the
text of that frame stand the flags, and there are exactly two:

| | |
|---|---|
| `$N` | I speak INP3 |
| `$M<n>` | do not tell me about anything slower than this, in 10 ms units |

The specification also mentions a `$I` for the IP option.  It does not
exist in practice; the IP field travels unannounced, so anything
waiting for `$I` waits forever.

Probes go out every 180 seconds, and at once when a link comes up or
when a partner's `$N` is seen for the first time.  Without that last
one, a link that came up in a second would wait three minutes for the
other side's next probe before anything could be announced.

**Nothing is announced until BOTH ends have measured.**  Ours is needed
because every route time we send is built on it.  His is needed because
he adds his own to each of them, and until he has one every route we
name arrives at him short by the length of the link - which makes us
look better than we are, at exactly the moment we know least.

## Zero means gone, and that is a trap

A withdrawal is a route time of 0.  Over axudp or the internet a link
is faster than the 10 ms the protocol can express, so an honestly
rounded measurement would be 0 - and a very fast link would announce
itself as dead.  Four things prevent it, all of them worth keeping:

* the round trip is measured as `elapsed + 2` before halving, so it is
  never less than 1;
* smoothing clamps its input and its result to at least 1;
* a partner's own entry is not measured but set to 1 by definition;
* a zero on the way out is written as the horizon, 60000, and never as
  a literal zero.

The last one matters for reading traces: **a withdrawal on the wire
looks like 600.00 s**, not like 0.

## Two metrics, and where they meet

WAMPES computes NET/ROM as a **link state**: a nodes broadcast names
both a destination and its best neighbour, `broadcast_recv()` builds
edges from that, and `calculate_all()` multiplies qualities along a
graph.  INP3 is **distance vector** - a routing information packet
names a destination, a time and a hop count, and no intermediate node
at all, so no edge can be built from one.

TNN does not have this problem, because it converts everything to time
as it comes **in** and keeps a single table.  We keep both metrics, and
convert only where a frame is **written**.  That gives us one node list
with two way-tables beside it, and exactly three places where the two
worlds touch:

| where | what is asked | what comes out |
|---|---|---|
| forwarding a datagram | which next hop | the partner, or the graph's neighbour |
| a routing information frame | which time | the INP3 time, or `(256-q)*10` from the graph |
| a nodes broadcast | which quality | `255-tt/10` from INP3, or the graph's quality |

All three ask the same function, and that is not tidiness: worked out
separately they drift apart, and a node that advertises one way while
forwarding down another is worse than one that does neither.

**Which of the two wins is parameter 28.**

    netrom parms 28 1      INP3 first (default)
    netrom parms 28 0      INP3 only where the graph has nothing

The default is not enthusiasm.  We measure nothing in the graph -
`nr_hfqual` is a constant every neighbour is given - so its figure is
an assumption where INP3's is a measurement.  Turning it off buys back
a problem: comparing the two means converting between them, and the
quality scale covers 0.1 s to 25.5 s where INP3 reaches 599.99 s.

There is no third case, and in particular **no fall back on AX.25
routes**: a destination with no way is discarded, exactly as before.
Not to be confused with reaching the *neighbour*, which certainly does
use them - that is delivery at layer 2, not routing.

## What a node knows, and what a way knows

`DB0XYZ` is the same node however we heard of him.  His alias, his IP
and the options he carries belong to **him**; only "how fast, through
whom" belongs to the **way**.  So there is one node table, and beside
each node one cell per partner:

    netrom nodes db0ddd-9
    Node       Ident   Neighbor   Level  Quality  In        Adv  INP3
    DB0DDD-9   DDDNOD               999        0  all       yes  15.03s
      INP3 via DB0XYZ-4   route 15.00s  +link 0.03s  4 hops  age 27s  told nothing
      INP3 to  DB0ZZZ-2   no way of his own, told 15.04s
      INP3 ip  44.130.1.5/28
      INP3 opt 7 byte carried through

Each cell holds **both directions**: what he tells us, and what we last
told him.  The second half is not bookkeeping - a protocol that sends
only changes cannot send anything without it.

`told nothing` on the partner a route came from is poison reverse
working: the way we would use runs through him, so seen from where he
stands we are not a way at all.  The specification permits *alternate
reverse* instead, offering him our second-best way; we do poison
reverse.

**The link is the lifetime.**  There is no ageing timer on an INP3
route, and there must not be: silence means "unchanged", so a timeout
would throw away perfectly good routes.  What ends a way is his
withdrawal, or the link going down, or `netrom peer del` - and a new
connection starts everything again from nothing, because nothing agreed
on the old one still holds.

## How much is said, and how often

Changes go out every 10 seconds, which is the maximum delay the
specification allows itself for bad news.  Everything is said again
once an hour, as a safety net under a protocol that otherwise only
speaks when something moves.

| | |
|---|---|
| withdrawal | at once |
| **worsening** | **at once, and pessimistically** |
| improvement | only if at least half again, at least 100 ms, at least half the link time, and the link is not busy |
| first mention | only below half his ceiling |

The second line is the interesting one.  A worsening is reported
immediately, but inflated by an eighth of itself plus half the link
time.  The damping then costs nothing further: the inflated figure is
what we remember having said, so every further small worsening up to it
is no longer a worsening and needs no frame.  If the inflated figure
passes his `$M`, the destination is withdrawn instead.

A first mention below *half* the ceiling is hysteresis of the same
kind: named at the very edge, the next slight worsening would have to
take it back again.

## The IP field, and the way to IPv6

A routing information packet may carry optional fields, each one a
length byte, a type byte and its data - **and the length counts itself
and the type byte**.  The specification calls the IP option "5 byte",
meaning its payload; on the air it is 7.  An implementation built from
the document alone gets this wrong and is understood by nobody.

    type 0x00   alias, ASCII
    type 0x01   4 byte IPv4, network order, plus 1 byte prefix length

**Options we do not know are carried through unchanged**, with their
own length byte, and that is the whole extension mechanism this
protocol has.  A field type neither we nor the node in the middle
understands still reaches the far end - so IPv6 needs 16 bytes, a
prefix length and a type number, and no negotiation with anybody.

What we do *not* yet do is act on a learned IP: it is stored, shown and
passed on, but no ARP entry and no IP route is made from it.

## Interaction with the classic broadcast

Our own nodes broadcast carries INP3 destinations too, converted at
that edge, with **our own callsign** in the best-neighbour field.  The
partner it really runs through speaks a protocol this neighbour does
not, so naming him would be an offer nobody there could take up; for
that broadcast we are the last hop, and that is the truth rather than a
polite fiction.

In the other direction, **a UI nodes broadcast from a partner is
dropped while his `$N` stands**.  Otherwise he teaches us the same
destinations twice - once measured over the interlink, once guessed in
the broadcast - and the worse source overwrites the better whenever it
happens to arrive.  His broadcast *before* the `$N` is still taken,
which is right: until then we do not know he is a partner.

TNN does not do this, and does not need to, because an INP link is
usually a port of its own.  With several partners on one axudp line it
is needed.

One thing this does not solve, and it is worth knowing: our broadcast
is one UI frame for everybody on the port, so there is no per-partner
suppression in it.  An INP3 destination is therefore announced to the
very partner it came from.  Between two WAMPES nodes the drop above
closes that; against a foreign node the entry is a genuine way through
us, and his to weigh.

## Trust

The specification calls it a *net of trust*: a receiver accepts what a
partner sends with minimal checking.  There is no authentication in the
protocol at all, and it assumes interlinks run between infrastructure
that has been agreed on.  That is why `netrom peer` exists as a list
you write rather than something learned: **a routing information frame
from a station that is not on it is discarded**, because an interlink
is agreed and not assumed.

## Sources

The protocol: *A New Routing Specification for Packet Radio Datagram
Networks*, Andreas Gal (DB7KG), TheNetNode-Group / NORD>|<LINK
Braunschweig, ca. 1997.  RTT algorithm by Peter Guelzow (DB2OS),
RIF/RIP format by Joachim Scherer (DL1GJI).

The behaviour was checked against TheNetNode, whose sources are under
the ALAS licence and are therefore a **reference for behaviour and not
a source of code** - everything here is written from scratch.  Where
this page states a constant or a threshold, it was read off TNN and
then measured against our own implementation, because the specification
does not fix them.
