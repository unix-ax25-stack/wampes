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

The shim therefore shrinks to a translator for the calls that name an AX.25
address or claim one - `socket`, `bind`, `connect`, `listen`, `accept`,
`setsockopt` - and a hook in `close` to forget a descriptor.  Nothing on the
data path.

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

`libax25/wampes.c`, hooked into `axsock.c` at six places.  Selected with
`AXSOCK_BACKEND=wampes`.  The outgoing direction:

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

## The incoming direction

A listener needs a second thing: permission.  Outgoing, whoever may reach the
service socket may already use the transmitter, and the unix mode on the
socket is the whole access rule.  Incoming, a client asks to be *given* calls
to a callsign, and handing out the node's own login or a neighbour's mailbox
to whoever asks first would be wrong.

So the sysop's line is the permission and the client's claim is made against
it:

    net.rc:          listen ax25 add [pid=<n>] DL9SAU-13 client
    service socket:  listen DL9SAU-13 [pid=<n>]
                       *** listening on DL9SAU-13 pid 0xf0
                       *** DL9SAU-13 is not open for clients
                       *** DL9SAU-13 is already taken
                       *** DB0AAA-1 belongs to a port

A callsign nobody configured cannot be claimed at all, which is the rule in
one sentence.  The claim lives exactly as long as the connection it was made
on: a client that dies leaves nothing behind, and the next one can claim the
entry without the sysop doing anything.

Per incoming call WAMPES makes a `socketpair`, gives one end to the same
`axpipe_open()` that serves a spawned program, and sends the other with
`SCM_RIGHTS` - together with the trace line, in **one** `sendmsg()`:

    hfa DL1TST-1,DB0BBB > DL9SAU-13

Together deliberately.  A descriptor arriving on its own would have to be
matched against a line arriving separately, and there is no key to match them
with.  This way the client's `accept()` is one `recvmsg()`.

The `sendmsg()` uses `MSG_DONTWAIT`: the node's scheduler is cooperative, so
a client that has stopped calling `accept()` must not be able to stop it.  A
full buffer is treated like any other failure to hand the call on.

On the shim side `listen()` sends the claim over an ordinary service
connection, and that connection *becomes* the listening descriptor.  `poll()`
and `select()` on it therefore work with nothing of ours involved - it is
readable exactly when a call is waiting, so `ax25d`'s event loop needs no
adjustment.  `accept()` is the `recvmsg()`, and the calling station goes into
the address the caller gets, which is what a peer address means.

The refusals are the ones a TCP server knows, which is not a coincidence:

| line | errno |
|---|---|
| `already taken` | `EADDRINUSE` |
| `not open for clients` | `EACCES` |
| `belongs to a port` | `EADDRNOTAVAIL` |

They surface at `listen()` and not at `bind()`, where a TCP server would meet
them.  `bind()` cannot ask: the same call names the source of an outgoing
connection, and claiming a callsign for every socket that names one would be
wrong.  Only `listen()` says what the socket is for.

**One claim, many calls.**  `already taken` refuses a second *process*, not a
second connection.  Incoming, `lapb.c` keys on the calling station, so every
caller gets a control block, a socketpair and a descriptor of its own; the
one registered client accepts them all, exactly as a TCP server does after
one `listen()`.  A mailbox with several users at once needs nothing extra.

Verified, 2026-08-14, with a listener doing nothing but socket/bind/listen/
accept:

    wampes: -> listen DL9SAU-13
    wampes: <- *** listening on DL9SAU-13 pid 0xf0
    accept 1: from DL1TST-1  (fd 4)
    accept 2: from DL1TST-2  (fd 4)

both callers connected at the same time and each answered under its own
callsign, while a second listener on DL9SAU-13 got
`listen: Address already in use`.

## Not built yet

* **Configuration.**  Still two environment variables: `AXSOCK_BACKEND=wampes`
  chooses the backend for the whole process and `WAMPES_SOCKET` says where
  the node listens.  Both should go.  The backend belongs at the port, the
  way the AGWPE ports already do it - an `axports` entry named `wampes:hfb`
  says which backend it wants by its own name, and then kernel, AGWPE and
  WAMPES ports can be used side by side in one process.  `axsock.c` says as
  much in the comment above `axsock_backend_now()`, which calls the variable
  a stop-gap.  Where each node listens belongs in a `wampes.conf`, one node
  per line, in the shape of `agwpe.conf`.
* **Frame boundaries outgoing.**  The service socket is a byte stream.
  Terminal traffic and text services do not care; FBB's compressed forwarding
  does, because an uncompressed block ends where the frame ends.  Inside
  WAMPES the boundary now survives all the way to the pipe - the receive
  queue holds frames rather than bytes, and `SOCK_SEQPACKET` is used wherever
  the system has it on `AF_UNIX` (Linux does, macOS does not).  The incoming
  direction already gets it for free, because the descriptor WAMPES passes is
  one it made itself.  Outgoing wants the same treatment: a `connect` that
  hands back a descriptor instead of turning the command connection into the
  pipe.
* **Datagrams inbound.**  `datagram` sends UI frames; nothing pushes received
  ones back, so `recvfrom()` has no source yet.
* **`getsockname()` and `getpeername()`** are not answered for WAMPES
  descriptors.  Nothing tested has needed them; a program that asks gets
  whatever the underlying unix socket says, which is not an AX.25 address.
* **A non-blocking `connect()`** returns when the link is up or refused, not
  `EINPROGRESS`.  Neither `call` nor `ax25d` asks for one.
* **The claim is always pid text.**  A program cannot ask to be given some
  other protocol id, although WAMPES would allow it.
