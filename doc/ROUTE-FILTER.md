# Who may teach us routes, and whom we tell others about

NET/ROM and FlexNet both used to take everyone at their word.  A station
that sent one frame of either protocol became a neighbour or a link
partner on the spot, and from that moment its announcements were ours and
ours were its.  That is the right default for a node whose only
neighbours are link partners.  It is the wrong one as soon as users can
reach the node on the same port - and until now there was no way to say
so.

Nothing here changes what an unconfigured node does.  A node without a
single `filter` line behaves exactly as it did before, on purpose: the
default is the old behaviour, not the safe one, so that no existing
installation changes under its operator.  One line changes the whole
posture, and the rest of this page is about which line.

## What is actually being decided

When a strange station speaks to us, four things are settled.  Three of
them are worth a switch.

| | | |
|---|---|---|
| **A** | do we learn that **he** exists | not a switch - he has to be in the table, or we look for him later and do not find him |
| **B** | do we learn what he says about **others** | the switch: `in` |
| **C** | do **others** hear about him | the switch: `advert` |
| **D** | does **he** hear about others | the switch: `feed` |

The station classes want different combinations:

| | `in` | `advert` | `feed` | |
|---|---|---|---|---|
| link partner | `all` | `yes` | `yes` | the default, as before |
| node at our own site | `all` | `no` | `yes` | his routes are useful; upstream we appear as one system |
| user | `none` | `no` | `yes` | teaches us nothing, may have our table - that is why he called |
| listen only | `all` | `no` | `no` | we learn from him and give nothing back |

**`advert no` is about him and nothing else.**  What lies behind him was
settled by `in`: with `in all` we accepted those routes and they are ours
to pass on, with `in only-him` they never arrived.  So `advert no` leaves
out one entry - his - and nothing more.

**`feed no` is not silence.**  He still learns that *we* are here: FlexNet
greets the link with `FLEX_INIT`, which is our own callsign range, and
NET/ROM still broadcasts on the port, with the identifier and no entries.
That is the difference from switching the port off with `netrom broadcast
disable`, and it is a real one - a neighbour who never hears from us
cannot route to us either.

**Why the three are independent.**  It is tempting to tie D to C, since
hiding a station and not feeding it sound like one wish.  But the two
cases that want `advert no` want opposite things at D: a user is hidden
and may still have our table, which is the whole point of letting him
speak the protocol; a node at our own site is hidden and *must* have our
table, because it is ours and its users route through it.  One switch for
both would break the second case to serve the first.

At NET/ROM `feed` is per **port** only.  Written against a callsign it is
refused, because NET/ROM announces one UI frame for everybody on the port
and a rule that looked as though it worked per station would be a lie.

Set at the console, a FlexNet filter takes effect **at once**, and what
travels is a withdrawal: `doflexnetfilter()` calls `process_changes()`,
so routes that stop being announced go out once with a delay of 0.
Without that they sat until a peer happened to say something or the five
minute poll came round - measured, and long enough to look broken.

## Writing it down

    netrom  filter
    netrom  filter default | port=<name> | <call>
                   [in <mode>] [advert yes|no] [feed yes|no]
    netrom  filter --delete default | port=<name> | <call>

    flexnet filter ...              the same, and configured apart

`in` is `all`, `only-him` or `none`.  With no arguments the rules are
listed.  Naming a station without settings creates a rule that says
nothing, which is the old behaviour - a half-written line does not
quietly tighten anything.

`port=` rather than a bare name, because an interface may perfectly well
be called `hf1` and so may a station; `setcall()` accepts both and there
would be no telling which was meant.  Same `port=` as in `listen`.

The interface is kept **by name**, not as a pointer, for the reason
`portlist_set()` gives: `net.rc` is read from the top and nothing says
the `attach` lines come first, so a filter resolved at configuration time
would quietly miss every port attached after it.

**The most specific rule wins**: the callsign, else the interface, else
`default`, else what the node always did.  That is what makes the two
useful shapes possible - a whole user access port in one line, and a
single partner on a shared axudp line picked out by callsign.

    flexnet filter default    in only-him advert no    # nobody is trusted
    flexnet link add DB0AAA-5                          # except those named
    flexnet filter DB0AAA-5   in all advert yes

    netrom  filter port=useraccess2m in only-him advert no
    netrom  filter port=lan0         in all      advert no

Because working three levels out by hand is exactly what an operator
should not have to do, `flexnet link` and `netrom nodes` show what is **in
force** for each station, not what was written about it.

## For a station nobody entered, `only-him` and `none` are the same thing

Worth knowing before writing a longer rule than necessary.  A FlexNet peer
becomes a *destination* in exactly one place - `recv_rprt()`, the answer to
**our** poll - and we no longer poll peers nobody entered.  No poll, no
`FLEX_RPRT`, no destination entry: he appears in nothing we announce, with
or without `advert no`.

So the whole user case is one word:

    flexnet filter port=useraccess2m in none

He stays a peer, we speak to him, **he gets our table** - which is why he
connected in the first place.  What `in none` stops is the one thing that
was actually happening: his routes entering ours.  Measured, with him not
entered and a real link partner listening:

| `net.rc` | the partner hears | he hears |
|---|---|---|
| *(nothing)* | `DB0QRS d645; DB0XYZ d623` | `DL1ABC-7-7 d14` |
| `in only-him` | nothing | `DL1ABC-7-7 d14` |
| `in only-him advert no` | nothing | `DL1ABC-7-7 d14` |
| `in none` | nothing | `DL1ABC-7-7 d14` |

The two settings part company only for a peer from `flexnet link add`,
which is polled and therefore does become a destination:

* `only-him` - reachable through us, but his knowledge is not taken.
* `advert no` - taken and used, but no other node hears of him.  This is
  the node-at-our-own-site case, and at NET/ROM it is every neighbour,
  because there the link that makes him known is also the link that makes
  him reachable.

The price, and it is the intended one: a station that is not a destination
is not reachable **through us** either - `update_axroute()` builds no AX.25
route for him.  "We do not carry our users upstream" is exactly that
sentence.  A user who should be reachable has to be entered with `flexnet
link add`, and is then polled like any partner.

## The two protocols are not symmetric, and it is their doing

**FlexNet announces per partner** - `send_rout(pp)` - so everything above
can be done per station.  Withdrawal travels too: a destination that
becomes hidden is announced once with delay 0, which is "link down", and
then the loop falls quiet by itself.  Skipping it instead would leave
whatever we last said standing at the far end until it aged out.

**NET/ROM announces per broadcast entry**, one UI frame for everybody on
the port.  There is therefore nothing per station to stop, and the
question "do we broadcast on this port at all" is its own switch:

    netrom broadcast                    list, numbered from 1
    netrom broadcast disable <n>
    netrom broadcast enable <n>

Entries are switched off, not deleted - an entry that vanishes is one the
operator cannot see any more.  They are numbered by position and appended
in the order they are written, so entry 1 is the first such line in
`net.rc` and the numbers do not move when another is added.  This is what
makes the node broadcast interval, which is one number for the whole
node, answerable per port: learn nodes over axudp without filling an HF
access with broadcasts of our own.

**The back door in the NET/ROM broadcast**, which is easy to miss: every
entry carries not only the destination but the **best neighbour** we use
for it, and `broadcast_recv()` at the far end creates a node for whatever
stands in that field.  A hidden neighbour would therefore become known
anyway, through the entries that reach past him.  For destinations behind
an `advert no` neighbour we put **our own callsign** there instead.  To
the outside we are then the last hop - which is precisely "appear as one
system".  FlexNet has no such field in `FLEX_ROUT`, so the question does
not arise there.

## What this costs, said plainly

**`in none` at NET/ROM is not the same as `in none` at FlexNet.**  In
FlexNet a peer stays a peer: we answer him, he may have our table, he is
simply not a destination of ours.  In NET/ROM the link to a neighbour is
not only how we learn about him, it is how we **reach** him -
`calculate_all()` routes by it - so refusing it means refusing to speak
NET/ROM with him at all.  That is a legitimate thing to ask for.  It is
not what the word says elsewhere.  **For a user port use `in only-him`.**

**`in only-him` at NET/ROM also stops the return path being learned from
transit frames.**  A frame passing through builds the way back
(`route_packet()`), and that is the second way a station teaches us about
others.  On a user access port that is the point.  On a link that carries
transit it is not - a connection routed through such a neighbour has no
return path and dies.  FlexNet does not have this problem: it is a pure
L3 path protocol with no L4 of its own, so not learning a route makes a
destination unreachable but breaks no connection.

## The FlexNet round trip measurement

FlexNet measures the delay to a partner with a **201 byte** poll packet
every five minutes and times the answer.  Until now that went to every
station that had ever sent us a 0xCE frame.  Measured, against a station
that sent exactly one such frame:

    0.0s  UA                                     our connection is up
    1.0s  I PID=0xce len=6    '01  !\r'          FLEX_INIT from us
    1.0s  I PID=0xce len=201  '2      ...'       FLEX_POLL
  301.0s  DISC                                   we throw him out

The DISC at the end is `polltimer_expired()` deleting a peer that did not
answer a poll he never asked for - and with it goes the user's
connection.

**The poll now goes to peers from `flexnet link add` and to nobody
else.**  Not to a filter setting: whether somebody is a link partner is
already written down, and it is the same question.  Two consequences
follow and are handled:

* A peer nobody entered is no longer deleted by an unanswered poll,
  because there is no poll.  His **link** is what says he is there: when
  the AX.25 connection is gone, so is he.  Without that he would stay in
  the list for ever and `setaxp()` would **call him** every five minutes.
* His delay is unknown, and his delay is what we add to everything he
  tells us about.  Left at zero he would look like the fastest link we
  have, so a route through a user would beat the route through a
  partner.  He is given `DEFAULTDELAY` at creation - the value the
  protocol itself uses for "no idea", the one `recv_poll()` sends and
  `recv_rprt()` ignores on the way in.  An unmeasured link is simply an
  unattractive one.

  It belongs at creation and **not** in `recv_rout()`, which is where it
  was tried first: `recv_rprt()` rescales what it has already stored by
  the *difference* in `pp->delay`, so a value added anywhere else stays
  in the table for ever.  Measured, with the guess in the wrong place: a
  delay of 623 became 637 on the first poll answer instead of 37.

`flexnet link add` on a peer that turned up by itself starts the link
over - greeting and first poll - rather than just setting the flag, so
the guess and everything scaled with it are dropped.

NET/ROM has no such probe.  It answers `L3RTT` frames (`route_packet()`)
and never originates one; the round trip figure there is the per-hop
times summed along the path, not a measurement of ours.

## Measured

A node with two FlexNet partners on one axudp line.  `DB0AAA-5`
announces `DB0XYZ-3-7` and `DB0QRS-0-0`; the question is what `DL1ABC-7`
is told.

| `net.rc` | what DL1ABC-7 hears |
|---|---|
| *(nothing)* | `DB0AAA-5-5 delay 14; DB0QRS-0-0 delay 59; DB0XYZ-3-7 delay 37` |
| `flexnet filter DB0AAA-5 advert no` | `DB0QRS-0-0 delay 59; DB0XYZ-3-7 delay 37` |
| `flexnet filter DB0AAA-5 in only-him` | `DB0AAA-5-5 delay 14` |
| `flexnet filter DB0AAA-5 in only-him advert no` | nothing |
| `flexnet filter DB0AAA-5 in none` | nothing |

The same node with NET/ROM, one neighbour `DB0AAA-5` announcing
`DB0XYZ-3` and `DB0QRS-0`, and what our own nodes broadcast then carries:

| `net.rc` | our broadcast |
|---|---|
| *(nothing)* | `DB0AAA-5 via DB0AAA-5 q=192`, `DB0QRS-0 via DB0AAA-5 q=112`, `DB0XYZ-3 via DB0AAA-5 q=135` |
| `netrom filter DB0AAA-5 advert no` | `DB0QRS-0 via DL9SAU-1 q=112`, `DB0XYZ-3 via DL9SAU-1 q=135` |
| `netrom filter DB0AAA-5 in only-him` | `DB0AAA-5 via DB0AAA-5 q=192` |
| `netrom filter DB0AAA-5 in none` | empty |
| `netrom broadcast disable 1` | no broadcast at all |

The second row is the one to look at: `DB0AAA-5` is gone **and** the
neighbour field now names us.

Tools: `testtools/flexmulti.py` plays several FlexNet stations on one
socket, `testtools/nrpeer.py` a NET/ROM neighbour.  Several simulated
stations have to share one socket, by the way, and that is not
convenience: `uhnp.c` remembers the UDP source port **per host**, so two
of them on 127.0.0.1 with different ports take each other's frames.

## What `advert no` does not do, and it matters

Hiding a station from our announcements works only until traffic flows.
`route_packet()` learns the **source node** out of every frame passing
through - that is what builds the return path of an L4 session, and it is
normally right.  For a hidden user node it is not: once he sends anything,
every node along the path has an entry for him, with a quality above zero,
and announces him in **its** broadcast.  He is hidden from our node list
and from nobody else's.

The answer is **node proxying**: rewrite his callsign in the NET/ROM
header to ours on the way through, so his call never appears outside.  For
IP that is stateless - the IP payload is not touched at all, and IP
addresses carry the return identity by themselves.  For L4 it needs a
table, because a NET/ROM circuit is named `(node, index, id)` and index
and id only mean anything inside one node.

It also turns a cost into a feature: `in only-him` and `in none` switch
off that same transit learning here, so a session routed **through** such
a neighbour has no way back and dies.  With proxying it would not.

**For IP this is built.**  A datagram merely passing through from a node
we do not announce is no longer forwarded; `route_packet()` hands it to
`nr_ip_deliver()`, the same acceptance path a datagram addressed to us
takes.  From there `ip_route()` sends it onward as our own traffic, and
`nr_send()` writes `mynode->call` into the source.  Nothing is rewritten,
because the frame that leaves us is one we built - and so there is nothing
to remember either.  Only the sender is hidden: a datagram from a node
further out carries its own source and is forwarded as before, which is
the rule `advert no` already follows in the broadcasts.

Accepting it also fixes the return path.  The pairing of IP address and
node was previously noted only for datagrams addressed to us, so a node
that merely forwarded never learned who lived where.  Measured, with
`netrom filter db0aaa-5 advert no`:

|                | Onward to DL1BBB          | Return                      |
| -------------- | ------------------------- | --------------------------- |
| without filter | source **DB0AAA-5**, TTL 15 | fails - ICMP from our own address |
| `advert no`    | source **DL9SAU-1**, TTL 16 | reaches DB0AAA-5, source DL9SAU-1 |

`testtools/nrip.py` drives both directions.  Note the TTL: 15 is a
forwarded frame, 16 a fresh one.

**For L4 it is built too**, by terminating rather than rewriting.  A
connect request from a hidden node is taken in the transit path, and a
second circuit is opened to where it was going; the two are spliced.  Our
own connect request fills both places that carry the origin - the L3
source and the node field at offset 13 - with `mynode->call`, because we
are its origin, so there is nothing to rewrite and nothing to forget.  The
**user** callsign is carried across unchanged, so the far end sees
`user @ our-node`, indistinguishable from an ordinary user connect through
us.  Answers to the caller go out signed as the node he asked for: he
believes he is talking to it and has no circuit for anyone else.

The pairing is the two circuits, held by a pointer each - no table, and no
expiry of its own to get wrong.  What it costs is a second window and a
second set of buffers, and having to push back; data moves only as far as
the other side has room, and the rest waits for its send upcall.

Measured, three sessions at once, payload of 768 bytes covering all 256
byte values:

|                | Connect request at DL1BBB                      |
| -------------- | ---------------------------------------------- |
| without filter | source `DB0AAA-5`, node field `DB0AAA-5`, TTL 15 |
| `advert no`    | source `DL9SAU-1`, node field `DL9SAU-1`, TTL 16 |

The caller's answers came from `DL1BBB` in both cases.  All three sessions
kept their own user callsign and their own data - re-segmented to
`NR4MAXINFO` on the way and reassembled byte-identical - and after the
disconnect the circuit table was empty.  A node *behind* the hidden one is
not touched: a request whose L3 source was `DB0XYZ-3` went on unchanged
with TTL 15.  An unreachable target is refused with `CONAK+CHOKE` rather
than left to time out.  `testtools/nrproxy.py` drives all of it.

The one thing to know is in `TODO.txt`: proxying assumes the hidden node
always reaches its target through us.  With several interlinks the same
session sent through a different neighbour arrives at the far end with
numbers it has no circuit for and stops - no damage, but a silent one.
`advert no` on a node with more than one uplink is a configuration error.

## Not built, and why it is written down here

* **A third level of access** - "may not speak the protocol at all" as
  distinct from "speaks it but teaches us nothing".  `in none` comes
  close and is not the same thing: the frames are still parsed and the
  peer still exists.
* **Per-interface node broadcast intervals.**  `nr_bdcstint` and
  `nr_minobs` remain one number for the whole node; only *whether* we
  broadcast is per entry.
* **A reduced broadcast per entry.**  At NET/ROM the choice on a user port
  is all or nothing: broadcast there and he learns everything, or do not
  and he learns nothing.  Announcing only part of the table per broadcast
  entry is the natural next step and is not built.  INP3 will raise the
  same question from the other side - it runs connected, like FlexNet, so
  there it is the per-peer path that applies again.
