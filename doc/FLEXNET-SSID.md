# Which SSIDs we are, as FlexNet sees them

FlexNet does not know stations, it knows **a callsign and a range of SSIDs**.
`DB0AVH 0-3` and `DB0AVH 4-15` are two entries in every FlexNet routing table,
with their own delays, because they are two systems at one site: the first is
the RMNC, the second the XNET, and the XNET passes the SSIDs above 4 on
internally.  `DB0BLO` is split the same way, which makes it a convention
rather than a local habit.

The range travels in two halves that never meet in one frame:

| | |
|---|---|
| **start** | the callsign the link runs under - the AX.25 source address |
| **stop** | one number, sent once in `FLEX_INIT` when the link comes up |

There is no room for anything else: `FLEX_INIT` is six octets - `'0'`, the
stop as `'0'+n`, two spaces, `' '+1`, CR - and a callsign does not fit.  XNET
writes the two halves down in one place each:

    my call db0blo-4          the start
    ro fl pa ssid 15          the stop

so the range belongs to the **node**.  Every link announces the same one.

## What WAMPES did instead

The range belonged to the **link**.  `axroute()` stamps the chosen
interface's callsign over the source (`build_path()`), and `send_init()` put
that same SSID in as the stop.  So a node whose ports carry callsigns of
their own announced a station per port, each covering exactly itself.
Measured, two ports as `DL9SAU-4` and `DL9SAU-6` with a login listener on
`-8`:

    DL9SAU-4-4      on the first link
    DL9SAU-6-6      on the second
    -8              announced nowhere

Two stations to everybody else, and neither range covers the mailbox.  A
configuration that never sets `ifconfig <if> linkaddress` did not have the
fault, because then every port inherits `Mycall` and every link runs under
the same callsign - which is why this went unnoticed.

## Writing it down

    flexnet ssid                      what we serve, and what we announce
    flexnet ssid <start>-<stop>       set both, node-wide
    flexnet ssid none                 back to the old per-port behaviour

Unset changes nothing, as everywhere else here.

    flexnet ssid 4-15

opens every FlexNet link of ours as `<mycall>-4` and sends stop 15.  The
callsign base is `Mycall`'s: one node, one callsign - a different base would
be a station we do not answer to.

**The range is a declaration, not a promise.**  DB0BLO announces `4-15` while
`-6` and `-7` exist nowhere at all; a connect to them arrives and finds
nothing, and not even a DM comes back.  That is normal FlexNet, and it is why
the range cannot be derived from what happens to be configured - only the
sysop knows which block is his.  The other half of the reason is that ranges
for one callsign **never overlap** in the network: a value we computed
ourselves could run into a neighbouring system's block at the same site.

What the display is for is the other direction - showing what the declaration
leaves out:

    flexnet ssid
    SSIDs of DL9SAU this node answers to: 4, 6, 8
    Announced range: DL9SAU-4-15
    Peers:
      DB0AAA-5     to him we are DL9SAU-4-15
      DB0BBB-5     to him we are DL9SAU-4-15

The served SSIDs come from one walk over all sixteen, asking
`ismyax25addr()` for the ports and `axlisten_active()` for the listeners -
not by reaching into either list, so the display cannot fall out of step with
them.  The result is printed as maximal runs.

**Unset is shown as the range it produces**, not as "none":

    Announced range: not set, so each port supplies both ends from its own
                     callsign: DL9SAU-6-6, DL9SAU-4-4
      2 ranges for one node - to everybody else that is 2 stations
      1 served SSID outside what we announce - not reachable by FlexNet routing

Otherwise a node whose only port is `-0` would read "none" before and
`DL9SAU-0-0` after `flexnet ssid 0-0`, as though something had changed, when
the two are the same announcement.  And because the remarks hang on the
*declaration* rather than on existing links, they are there while the node is
being configured, which is when they are read.

The two remarks are deliberately of different weight.  *"n served SSIDs
outside what we announce"* states a fact - a range with gaps is ordinary.
*"n ranges for one node"* is a fault, because to everybody else that is n
stations.

## Two things it does not do, and why

**It does not touch links that reach us.**  A partner who calls us reaches
whichever callsign he dialled, and the answer must carry that or he will not
recognise it - so the start he derives is his choice.  In FlexNet the case
does not arise: a partner is configured with the one callsign the node uses
(`rou flex add 3 igateb`), so a link arriving anywhere else is a
misconfiguration at his end.  The display marks such a link with *"he called
this callsign"* rather than hiding it.

**It does not re-announce to links that are already up.**  Sending
`FLEX_INIT` again would look tidier and is a trap: `recv_init()` at the far
end calls `clear_all_via_peer()`, so he forgets every route he learned from
us - while our own `send_rout()` only ever sends *changes*, so we would not
tell him again.  A range that arrives when the link next comes up is better
than a table that quietly empties.

## What is still unknown

The three octets after the stop.  We write `' '`, `' '`, `' '+1` and read
none of them; `flexnet_dump()` decodes only the first field too.  FlexNet
codes small numbers as `' '+value` elsewhere - the hop count in the query
packet is `chr - ' '` - so they read as **0, 0, 1**, plausibly two reserved
fields and a protocol version.  It does not affect the range, since a
callsign could not fit there in any case, but it means we always claim
version 1 and would not notice if that ever changed.

`TODO.txt` carries the traces this was built from, and what they settled.
