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

When a strange station speaks to us, four things are settled.  Only two
of them are worth a switch.

| | | |
|---|---|---|
| **A** | do we learn that **he** exists | not a switch - he has to be in the table, or we look for him later and do not find him |
| **B** | do we learn what he says about **others** | the switch: `in` |
| **C** | do **others** hear about him | the switch: `advert` |
| **D** | does **he** hear about others | not a switch - see below |

The four kinds of station want different pairs:

| | `in` | `advert` | |
|---|---|---|---|
| link partner | `all` | `yes` | the default, as before |
| node at our own site | `all` | `no` | his routes are useful; upstream we want to appear as one system |
| user | `only-him` | `no` | reachable, teaches us nothing, and no node list elsewhere carries him |
| nothing at all | `none` | `no` | |

**`advert no` is about him and nothing else.**  What lies behind him was
settled by `in`: with `in all` we accepted those routes and they are ours
to pass on, with `in only-him` they never arrived.  So `advert no` leaves
out one entry - his - and nothing more.

**Why D is not tied to C**, although hiding a station and not feeding it
look like the same wish: the two cases that want `advert no` want
opposite things there.  A user is hidden and may still have our table -
that is the whole point of letting him speak the protocol at all.  A node
at our own site is hidden and *must* have our table, because it is ours
and its users route through it.  One switch for both would break the
second case to serve the first.  Whether we speak to a station at all is
a question of access, not of filtering, and it is not decided here.

## Writing it down

    netrom  filter
    netrom  filter default | port=<name> | <call>  [in <mode>] [advert yes|no]
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

## Not built, and why it is written down here

* **A third level of access** - "may not speak the protocol at all" as
  distinct from "speaks it but teaches us nothing".  `in none` comes
  close and is not the same thing: the frames are still parsed and the
  peer still exists.
* **A `feed` switch**, saying what a station hears from us.  Everyone we
  speak to is told, as before.  See "Why D is not tied to C" - the switch
  would be needed only for a station that may speak the protocol but not
  have the table, and no case for that has come up.
* **Per-interface node broadcast intervals.**  `nr_bdcstint` and
  `nr_minobs` remain one number for the whole node; only *whether* we
  broadcast is per entry.
