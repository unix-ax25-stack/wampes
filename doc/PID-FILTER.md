# Which protocols may cross a port

`netrom filter` and `flexnet filter` answer *whose routes do we take* and *who
hears about whom*.  They work **inside** a protocol: the frame is parsed, the
peer exists, we go on talking to him, and only what he says is disregarded.

There was no way to say the blunter thing - that a protocol has no business on
a port at all.  That is this gate.

    ifconfig <iface> pid                       what is blocked here
    ifconfig <iface> pid in|out block <protocol>...
    ifconfig <iface> pid in|out allow <protocol>...
    ifconfig <iface> pid in|out none           allow everything again

`<protocol>` is a name or a number - `netrom`, `ip`, `text`, `0xcf`, `207`.
`ax25 pid-info` lists the names; they are the same ones `pid=` takes on a
`listen` line and `--pid` on a connect.  Nothing is blocked until somebody
says so, so a port nobody configured carries what it always carried.

    ifconfig useraccess2m pid in  block netrom flexnet
    ifconfig useraccess2m pid out block netrom flexnet

## Why per protocol id and not per protocol

Because there is exactly one place where an AX.25 frame is handed to a
protocol:

    struct axlink Axlink[] = {              config.c
            { PID_IP, axip }, { PID_ARP, axarp },
            { PID_FLEXNET, flexnet_input }, { PID_NETROM, axnr }, ...

used by `ax_recv()` for UI frames and by `lapb.c` for the connected case.  A
gate in front of that lookup covers both kinds of frame in one grip, so "no
NET/ROM from this port" and "no IP from this port, neither in UI nor in I" are
the same sentence.

Segmented traffic is caught by its real protocol and not by `segment`: on the
way in the datagram is reassembled first and the gate sees the inner id, on
the way out the gate sits in `send_ax25()` **before** the segmenter, which is
the last place where the protocol is still known by name.

## What it is not about: traffic we repeat

The gate is about frames that are **ours** - addressed to us, or sent by us.
A frame that is not addressed to us is digipeated in `ax_recv()` before
anybody has looked at a protocol id; there is no pid at that point, only an
address to advance.  Making it apply to repeated traffic too would be a
separate switch and is not built - see `TODO.txt`.

## The two depths, and the order they act in

They are two rungs of one ladder rather than two keys:

| | |
|---|---|
| `filter ... in none` | the frame is parsed, the peer exists, we speak to him - we only learn nothing from him |
| `pid ... block` | the frame does not reach the protocol; there is no peer and no state |

The route filter can also name a **single callsign**, which a port gate
cannot, so neither replaces the other.

**Order matters when both are set**: the gate acts *before* the frame is
parsed, the filter *after*.  Look at the gate first, or you will hunt for the
fault in a filter while the gate has already dropped the frame.  `trace` shows
a blocked frame either way - tracing happens in the driver, below both.

## Where the gate sits

| direction | place | what it catches |
|---|---|---|
| in | `ax_recv()`, UI branch | UI frames addressed to us or multicast |
| in | `handleit()` in `lapb.c` | everything on a connection, after reassembly |
| out | `ax_send_ui()`, `ax_output()` | our UI frames, IP and ARP among them |
| out | `send_ax25()` | our connected traffic, before segmentation |
| out | `broadcast_to()` in `netrom.c` | the NET/ROM nodes broadcast |

The last one is there because it is the one send in the node that does not go
through `ax_send_ui()`: `alloc_broadcast_packet()` writes the UI byte and the
protocol id into the buffer itself and the frame goes straight to the driver.
Without it `pid out block netrom` would have stopped everything but the nodes
broadcast.

`send_ax25()` is asked twice, because it is called two ways.  With a protocol
id as an argument the question is plain.  With `-1` the id is already on the
front of each frame - NET/ROM pushes it on itself, and so does a relayed leg -
and then the gate reads it out of the data, frame by frame, since a chain need
not carry the same protocol throughout.

## Measured

A node with one NET/ROM neighbour, `DL1ABC-7`, announcing `DB0XYZ-3` and
`DB0QRS-0`.  `testtools/nrpeer.py` plays the neighbour and prints what comes
back.

| `net.rc` | what the neighbour hears | what the node learns |
|---|---|---|
| *(nothing)* | identifier + 3 entries | all 3 |
| `ifconfig axip pid in block netrom` | identifier, no entries | nothing |
| `ifconfig axip pid out block netrom` | **nothing at all** | all 3 |
| `netrom filter port=axip feed no` | identifier, no entries | all 3 |

The second row's empty broadcast is a consequence, not the gate: we learned
nothing, so we have nothing to announce.

The same three rungs at FlexNet, with `testtools/flexpeer.py` as a second peer
watching what we tell him:

| `net.rc` | what `DL1ABC-7` hears |
|---|---|
| *(nothing)* | `FLEX_INIT`, then `FLEX_ROUT` with three destinations |
| `flexnet filter port=axip feed no` | `FLEX_INIT` and nothing else |
| `ifconfig axip pid out block flexnet` | **nothing at all** |

And IP, which takes the third of the outgoing paths - `ax_output()`, not
`send_ax25()`.  `ping 44.0.0.2` over an axip port, six seconds:

| `net.rc` | on the wire |
|---|---|
| *(nothing)* | five frames, `pid=0xcc` |
| `ifconfig axip pid out block ip` | nothing |

The middle row of the FlexNet table is the "we are here, but our nodes are
not yours" case: the
neighbour learns our callsign range and can reach us, and hears nothing about
what lies behind us.  In the last row the node also loses `DB0AAA-5` from its
own destination list, and that is right rather than odd - a FlexNet peer
becomes a destination through the answer to **our** poll, and the poll could
not go out.

## Two things worth knowing before switching something off

**`text` inbound closes the node to users on that port.**  0xf0 is what a
plain connection carries, so blocking it in leaves the link connecting and
then silent.  That is a legitimate thing to ask for on a pure interlink; it is
not what most people mean by "no protocols here".

**Blocking outbound is silence, not a refusal.**  The frame is dropped where
it would have been sent.  The far end sees nothing, which at NET/ROM is the
same as us being gone; at FlexNet a partner who hears nothing may decide the
path is dead and drop it.  Where the wish is "he should still know we are
here", the answer is `feed no` in the route filter, one rung up.
