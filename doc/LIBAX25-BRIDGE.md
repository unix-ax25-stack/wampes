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
    -> handover
    -> connect hfb:DL1AAA via DB0BBB,DB0CCC < DL1TST-1
    <- link setup (hfb)...
    <- *** connected to DL1AAA          with a descriptor beside it

`handover` asks for a descriptor instead of letting this connection become
the pipe, and it exists for one reason: the connection belongs to the client,
which made it, so we do not get to choose its type - it is a stream, and
frames arrive packed as full as they will go.  A pair the node makes itself
can carry the boundaries.  The answer and the descriptor travel in one
`sendmsg`, so there is nothing to match up, and the command channel stays a
command channel afterwards.  A node that does not know the word answers
nothing to it and the connection is the session, as it was before.

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

`libax25/wampes.c`, hooked into `axsock.c` at six places.  Which ports it
serves comes out of `wampes.conf`, see below.  The outgoing direction:

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

Where each node listens is named in `wampes.conf`, one node per line, in the
shape `agwpe.conf` has:

    wampes    /usr/local/wampes/sockets/ax25    the node on this machine
    db0aaa    [fd00::5]:8010                    the club node, over IPv6

That file is also what makes an entry a WAMPES entry.  A port belongs to a
WAMPES node when the name before the colon appears there, so no name is
reserved and nothing is guessed from the spelling - and the backend is
therefore chosen **per port**, not per process.  The choice falls at `bind()`,
the first moment the port is known: whoever made the descriptor lets go of it
and the number the application holds does not change.  Kernel, AGWPE and
WAMPES ports are usable side by side in one process, verified with `call`
reaching a WAMPES port and an AGWPE port in turn with no environment variable
set at all.  `AXSOCK_BACKEND` and `WAMPES_SOCKET` remain as overrides for
trying something out.

A name whose node is configured but which has no `axports` entry of its own -
`wampes:70cm` where only `wampes` is listed - is accepted, and the node routes
the call.  It cannot pin the port, because `bind()` hands the library a
callsign and never a port name; entries sharing a callsign cannot be told
apart by one, and `axports` refuses duplicates anyway.  Pinning keeps needing
an entry with a callsign of its own.  A line on standard error says so once
per name, because whoever typed the suffix meant something by it.  The
fallback is asked for by the backend through a hook in `ax25_port_ptr()` and
is not the default: where a suffix selects something, as the AGWPE channel
does, falling back to the base would quietly use the wrong one.

Three ways to get the name wrong, each caught by the side that can judge it:

| written | who refuses | what happens |
|---|---|---|
| `:70cm` - no node at all | libax25 | `call: invalid port setting`, nothing is opened |
| `wampes:gsm` - no such WAMPES port | the node | `ENODEV` at `connect()`, and the caller's own `perror()` shows it |
| `wampes:70cm` with no entry | libax25, today | `invalid port setting` |

The last one is the open question.  libax25 refuses it because
`ax25_config_get_addr()` finds no entry of that name, so the suffix never
reaches the shim.  Making it lazy - one entry `wampes DL9SAU-1`, and
`wampes:70cm` accepted without a line of its own - would have to happen
there, in `ax25_port_ptr()`.

It costs something, and the cost is worth understanding before choosing.
`bind()` hands the shim a *callsign*, never a port name, so the name is
recovered by looking the callsign up backwards - and
`ax25_config_get_port()` returns the **first** entry whose callsign matches
(`axconfig.c:130`), while `ax25_config_load_ports()` rejects duplicate
callsigns outright.  Entries that share one callsign therefore cannot carry a
recoverable suffix.  The honest form of lazy is that it is *accepted* and the
node routes: one virtual interface in front of WAMPES, whose address every
program inherits that did not bind one of its own.  Pinning a port keeps
needing an entry, and that entry keeps needing a callsign of its own.

## Running it

A program **linked against** `libax25` needs nothing at all beyond the two
configuration files:

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

## What a spawned service is told

A `listen ax25 add <call> /path/to/program` runs the program with the call on
its standard input and output, and the circumstances in its environment.  The
first three names are `axspawn`'s and carry exactly the meaning they carry
there, so a script written for one world runs in the other:

| | |
|---|---|
| `AXCALL=DL1TST-1` | the calling station, with its SSID |
| `CALL=dl1tst` | the same, lower case and without it - the shape a unix account name takes |
| `PROTOCOL=AX.25` | the address family; `NET/ROM` for an L4 session |
| `AX25_DEST=DL9SAU-14` | the callsign that was called |
| `AX25_PID=text` | or `0xcf`, the protocol id the service was configured for |
| `AX25_PATH=DB0BBB,DB0CCC` | the digipeaters the call came by |
| `NETROM_NODE=DB0AAA` | instead of the three `AX25_` ones, on a NET/ROM session |
| `PATH=/usr/local/bin:/usr/bin:/bin` | ours, not the node's |

Our own names spell the protocol out, which is what tells `AX25_DEST` and
`NETROM_NODE` apart when either could be meant.  `AXCALL` keeps axspawn's
spelling even though `AXSRC` would pair better with `AX25_DEST` - it is
established, and a name that is already in scripts is worth more than a tidy
pair.

And that is the whole environment.  The node uses `execve()` with a list it
builds, not `execv()`, so a service run for a caller on the air inherits
nothing of what the sysop had in the shell that started the node.  Verified
by pointing a listener at `/usr/bin/env` with a marker variable set in the
node's own environment: seven names arrive and the marker is not among them.

## ax25d, and the descriptor a child inherits

`ax25d` accepts a call and hands it to a program on descriptor 0.  The
program then asks `getpeername(0)` who is calling - `axspawn` does exactly
that before it picks a unix account, and so do `rsuplnk` and `ttylinkd`.
Over a socketpair there is nothing to ask: the kernel answers `AF_UNIX`.

It fails safely, which is worth knowing: `axspawn` switches on
`sax25_family`, so a unix socket lands in its `default` arm and it refuses
with *"peer is not an AX.25, NET/ROM or Rose socket"* rather than inventing a
callsign.  A refusal, but still a refusal.

So the parent says who is there, in one variable:

    AXSOCK_INHERIT=<fd> <local> <peer>

and the shim reads it when it loads, registers the descriptor with both
addresses and answers for it.  **No child has to be changed.**  The variable
is removed from the environment once read, because what is true of this
process is not true of anything it starts in turn.

`ax25d` builds that environment rather than passing its own - a service run
for a caller on the air has no business inheriting what the daemon was
started with, which is why its `execve()` passed `NULL` to begin with.  One
exception, and it is necessary: the variable that says *which library we are*
is passed on when it is set, because a station running the shim by preloading
it would otherwise lose it at exactly this edge and the child would find no
AX.25 at all.  Only the names of the platform it was built for - `LD_PRELOAD`
and `LD_LIBRARY_PATH`, or the `DYLD_` ones on macOS - since the other set
would be noise in the environment of every service.  That omission caught
this very test out before it was fixed.

Verified through the whole chain - a caller on one node, the call carried
over AX.25 to another, `ax25d` accepting it through the WAMPES backend, and
the child reporting `getpeername(0): DL1TST-1` and
`getsockname(0): DL9SAU-13` with nothing else in its environment at all.

The remaining step is a real `axspawn`, which wants a unix account database
and therefore a Linux machine; the mechanism it depends on is the one above
and is done.

## Not built yet

* **The two environment variables should go.**  `AXSOCK_BACKEND` and
  `WAMPES_SOCKET` are overrides that nothing needs any more: the files decide.
  They are useful for trying something out and harmless while the bridge is
  young, but an override that outlives its reason turns into a way of
  configuring things twice, and then into a bug report about the file being
  ignored.  Drop them once the files have been in use for a while.
* **Datagrams inbound.**  `datagram` sends UI frames; nothing pushes received
  ones back, so `recvfrom()` has no source yet.
* **A non-blocking `connect()`** returns when the link is up or refused, not
  `EINPROGRESS`.  Neither `call` nor `ax25d` asks for one.
* **The claim is always pid text.**  A program cannot ask to be given some
  other protocol id, although WAMPES would allow it.
