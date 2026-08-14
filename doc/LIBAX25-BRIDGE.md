# Reaching the AX.25 world through WAMPES

The AX.25 stack left the Linux kernel in 7.1.  Programs written against
`libax25` - `call`, `ax25d`, mailboxes, DX clusters - still exist and still
expect `socket(AF_AX25, …)` to work.  WAMPES has a complete AX.25 stack in
user space and can lend it to them.

This describes how, what is built, and what is not.  It is not a manual; it
is the note that keeps the next person from having to work all of this out
again.

## Why a socket per connection

With AGWPE one TCP connection carries every session, so the shim in `libax25`
has to demultiplex: a reader thread, a dispatch table, a buffer per session,
and every `read()` and `write()` passing through all of it.  That is also
where `ax25netd` gives up - *"Cannot keep up; drop the client."*

WAMPES offers one socket per connection.  The descriptor the application
holds simply becomes the connection: after `connect()` succeeds, `read()`,
`write()`, `poll()`, `select()` and `close()` go straight to the kernel with
nothing in between.  There is no demultiplexer to write, no thread, and the
back pressure takes care of itself - when the socket buffer fills, WAMPES
stops reading and the AX.25 side sends RNR, which is what RNR is for.

The shim therefore shrinks to a translator for four calls: `socket`, `bind`,
`connect` and `setsockopt`.

## The conversation

The service socket is `$TCPDIR/sockets/ax25` (mode 0660, group `hams` where
that group exists), and the same service is reachable over TCP on 127.0.0.1
and ::1 after `start axtcp [<port>]`, default 8010, which is off unless
`net.rc` asks for it.

It speaks lines until the link stands and raw bytes afterwards:

    -> binary
    -> connect hfb:DL1AAA via DB0BBB,DB0CCC < DL1TST-1
    <- link setup (hfb)...
    <- *** connected to DL1AAA

Every line that does not begin with `***` is progress and may be ignored or
shown to the operator.  Exactly one line begins with `***`, and it is the
answer.  End of file before it means the link never came up and WAMPES gave
up retrying.

The refusals carry a reason, which is the whole point of them:

| line | errno the shim reports |
|---|---|
| `*** link failure with X - no route` | `EHOSTUNREACH` |
| `*** link failure with X - busy` | `EADDRINUSE` |
| `*** link failure with X - nomem` | `ENOBUFS` |
| `*** link failure - invalid call "…"` | `EINVAL` |
| end of file, no verdict | `ETIMEDOUT` |

`binary` comes first because an AX.25 socket is what the kernel gave, and the
kernel converted nothing.  Without it the service socket would translate line
endings, which is right for a human on a terminal and wrong for a program.

## What the shim does

`libax25/wampes.c`, hooked into `axsock.c` at four places.  Selected with
`AXSOCK_BACKEND=wampes`.

* `socket(AF_AX25, …)` returns a placeholder descriptor - an unbound unix
  socket.  It has to be a real descriptor because the application gets the
  number now and the connection only exists later.
* `bind()` carries two different things in one address.  `sax25_call` is the
  source callsign, what `call -s` sets and what WAMPES is told with `<`.  The
  first digipeater slot is not a digipeater at all: `libax25` puts the
  callsign of the `axports` entry there, and that is how the port is named.
  `ax25_config_get_port()` turns it back into the entry name.
* `connect()` holds the conversation above and then `dup2()`s the real socket
  onto the number the application already has.  From there nothing of ours is
  in the way.
* `setsockopt(SOL_AX25, …)` answers success and changes nothing.  The channel
  parameters belong to the node's interface configuration; a program that
  checks the return value must not be told its window size was refused.

One trap, found the hard way: the family must be read from
`sax25_family`, not from `addr->sa_family`.  `sockaddr_ax25` carries the
Linux layout, and on BSD and macOS the generic `sockaddr` begins with
`sa_len`, so the family byte is somewhere else entirely.

## Naming ports

One `axports` entry per WAMPES **interface**, not per node:

    wampes:hfb   DL9SAU-2   9600   256   2   WAMPES port hfb

The part before the colon says which node.  It is resolved on the Linux side
and never reaches WAMPES.  The part after it says which of the node's ports
to leave by and travels as the prefix WAMPES already understands
(`connect hfb:DL1AAA`) - the same one an operator types at an RMNC or XNET.

Per interface rather than per node because `axports` refuses duplicate
callsigns, and every WAMPES interface has a callsign of its own anyway.  The
two fit together without changing the file format.

Where the node listens comes from `WAMPES_SOCKET` for now, a path for a unix
socket or `host:port` for TCP.  A `wampes.conf` in the shape of `agwpe.conf`
belongs here once a second node is in play.

## Running it

A program **linked against** `libax25` needs nothing beyond the environment:

    AXSOCK_BACKEND=wampes WAMPES_SOCKET=/usr/local/wampes/sockets/ax25 \
        call -r -s DL1TST-1 wampes:hfb DL1AAA DB0BBB DB0CCC

`AXSOCK_DEBUG=1` prints the conversation.

A program that talks to kernel AX.25 **directly**, without ever calling a
`libax25` function, can still be made to use this - the shim overrides the
libc entry points (`socket`, `bind`, `connect`, `listen`, `accept`, `close`,
`write`, `setsockopt`) as ordinary strong symbols, so it is enough to get the
library loaded first.  No recompiling:

    Linux:   LD_PRELOAD=/usr/local/lib/libax25.so.0 program

    macOS:   DYLD_INSERT_LIBRARIES=/usr/local/lib/libax25.0.dylib \
             DYLD_FORCE_FLAT_NAMESPACE=1 program

The second variable on macOS is not optional: Mach-O binds two-level, so
every symbol remembers which library it came from and inserting the library
alone changes nothing.  ELF has a flat namespace and needs no equivalent.

What this cannot reach, on either system:

* setuid and setgid programs - the loader drops the variables in secure
  execution mode.  Run them as root without the setuid bit instead.
* statically linked programs - there is nothing to override.
* a program calling `syscall(SYS_socket, …)` directly.
* on macOS additionally anything protected by SIP, which strips every
  `DYLD_*` variable.

It works process-wide, but that costs nothing but a function call and a list
lookup: everything that is not `AF_AX25` falls through to the real call, so a
program can serve telnet over IPv4 or IPv6 and AX.25 side by side in one
process, which is exactly what a mailbox does.

## Verified

Between two WAMPES nodes joined by axudp, 2026-08-14:

    call -r -s DL1TST-1 wampes DL9SAU-11
        -> connect DL9SAU-11 < DL1TST-1
        <- *** connected to DL9SAU-11
        bbs>du sagtest: hallo aus call

A prompt with no line ending arrives immediately - nothing on the path
assembles lines, in either direction, which is what a shell or a mailbox
prompt depends on.

    call -r -s DL1TST-1 wampes:hfb DL1AAA DB0BBB DB0CCC
        -> connect hfb:DL1AAA via DB0BBB,DB0CCC < DL1TST-1

and on the node:

    Conn pend    DL1TST-1->DL1AAA via DB0BBB,DB0CCC

Source from `-s`, both digipeaters in the path, and the port chosen by the
name of the `axports` entry.

## Not built yet

* **The incoming direction.**  `ax25d` and anything else that listens needs
  more than this.  The plan, decided but unwritten: a fourth target kind
  `client` in `listen ax25 add`, which is the sysop's authorisation, plus a
  `listen` verb on the service socket that fails without one.  Per incoming
  call WAMPES makes a `socketpair`, hands one end to the existing
  `axpipe_open()` and passes the other with `SCM_RIGHTS` together with the
  trace line in **one** `sendmsg()`, so there is nothing to correlate.  The
  registration lives as long as the control connection, so nothing is left
  behind when a client dies.  `accept()` then is a `recvmsg()`, and `poll()`
  on the listening descriptor works natively because that descriptor is the
  control connection.
* **Frame boundaries on this path.**  The service socket is a byte stream.
  Terminal traffic and text services do not care; FBB's compressed forwarding
  does, because an uncompressed block ends where the frame ends.  Inside
  WAMPES the boundary now survives all the way to the pipe - the receive
  queue holds frames rather than bytes, and `SOCK_SEQPACKET` is used wherever
  the system has it on `AF_UNIX` (Linux does, macOS does not).  What is
  missing is a way for the shim to be handed such a socket outgoing; the
  descriptor-passing route above would give it, since the descriptor WAMPES
  passes is one it made itself.
* **Datagrams inbound.**  `datagram` sends UI frames; nothing pushes received
  ones back, so `recvfrom()` has no source yet.
* **`wampes.conf`.**  One node per line, in the shape of `agwpe.conf`.
