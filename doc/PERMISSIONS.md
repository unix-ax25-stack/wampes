# Who may write what, and why it decides everything

A WAMPES node usually runs as root.  It needs to: it opens serial ports,
creates tunnel interfaces, and on some systems it logs users in.

That makes one question more important than any other setting in the
configuration: **who else can write the files it reads?**

## The way in

Two of them, and both are meant to be there.

`net.rc` is a list of node commands, and one of those commands is `!`,
which runs a shell command.  So anyone who can edit `net.rc` can run a
program as root - not by exploiting anything, just by using the node as
designed.  They add one line and wait for the next restart.

The command channel on `$TCPDIR/.sockets/netcmd` is the node's own
command line.  Whatever may be typed at the console may be sent there,
`!` included.  The node creates that socket with mode 0700, so only root
can reach it - **provided the directories above it cannot be replaced by
somebody else.**

That second half is the part people miss.  A directory you may write is a
directory whose contents you may rename.  If `/usr/local` belongs to your
account, you do not need any permission on `/usr/local/wampes` at all:
move it aside, put your own there, wait for the restart.

On macOS this is the normal state of affairs.  `/usr/local` belongs to
the account that installed Homebrew and is writable by its group, which
is why `make install` works there without `sudo` - convenient, and
exactly the hole described above.

## The rule

`$TCPDIR`, the directory **above** it, and the files inside must

* belong to **root**, and
* not be writable by group or by others.

The parent is in because a directory you may write is one whose contents
you may rename.  It is also enough: with `/usr/local` in your hands, the
permissions on `/usr/local/wampes` are beside the point, and if
`/usr/local` is sound then so is everything below it that root owns.
There is no reason to walk further up - that would be the node auditing
the system, which is not its job.

A directory with the sticky bit is exempt from the second half.  `/tmp`
is writable by everyone and still nobody can rename another's entry
there.

### What is deliberately not checked

`$TCPDIR/sockets/` and what lives in it.  That directory is published on
purpose: `sockets/ax25` is how libax25 programs reach the node, and its
owner, group and mode are the sysop's decision rather than a security
question.  `0660` group `hams` is the usual choice.  A sysop who does not
want the `hams` group transmitting can make it `0660` group `staff`
instead, or `0606` group `hams`, and either is a considered decision, not
a mistake.  The node itself no longer decides any of it - see *The door is
the directory* below for the two attempts that were made and why both were
wrong.

The socket that *would* matter is the command channel.  It lives in
`.sockets`, and it carries 0600 of its own since 2026-08-15 - until then
nothing set a mode on it at all and those 0700 on the directory were its
entire protection.

`.sockets` is created 0700 and then left alone, like every other
directory here.  700 is the right default because it holds regardless of
what the socket inside says; an admin who wants 750 for a group of sysops
may have it and will keep it.  One who opens it wide is told so at
startup - that one is still checked.

### The door is the directory

`sockets/` is created **0750** and the socket inside it **0666**.  That
looks backwards until you see which one is the gate: who may reach the
service is decided by the group on the directory, and the sysop moves the
whole service from one group to another with a single `chgrp`, without
having to think about socket modes at all.

**0666 and not 0777**, because `connect()` to a unix socket asks for
**write** and never for execute.  Linux says so outright - `af_unix.c`,
`unix_find_bsd()`: `path_permission(&path, MAY_WRITE)`.  Measured on
macOS, a fresh socket for each mode:

    0666, 0600, 0200   connect OK   w set, x clear
    0111               EACCES       x set, w clear
    0466, 0000         EACCES

So the x bit was never read by anything.  It stood there until
2026-08-29 only because 0777 was the plainest way to write "the directory
decides", and nobody had checked which bits get consulted.  For a socket,
wide open is 0666.

    chgrp hams   $TCPDIR/sockets     the hams group may use the node
    chgrp sysops $TCPDIR/sockets     that group instead
    chmod 755    $TCPDIR/sockets     everybody may

Guessing is what does not work here, and both directions were tried on
2026-08-15.  Looking a group up by name handed the transmitter to whoever
held that name - and on BSD a new file takes the group of its DIRECTORY
rather than of whoever made it, so "no such group" quietly produced group
`staff`, which on macOS is every local account.  Refusing instead, and
leaving the socket at 0600, produced a node no client could reach and no
daemon could use without running as root.  That is the default which ends
in `chmod -R 777 /usr/local`, and it has been seen in the wild often
enough.

**The group is inherited, and the two systems inherit differently.**  On
BSD and macOS a new directory takes the group of its parent, so
`sockets/` comes up `root:staff` if `$TCPDIR` is - and `staff` is every
local account, so 0750 admits everyone.  On Linux it takes the group of
the process, so `root:root`, and 0750 admits nobody but root.  The same
setting, opposite results.  Either way the answer is one line, and it is
a line somebody should think about once:

    chgrp <group> $TCPDIR/sockets

`make install` does not do it, and does not create a group either.  It
cannot know who is meant to have the radio, and an install that reaches
into the account database is not one anybody should run.

### Connecting and binding are governed by different things

Easy to miss, and it costs an afternoon when it bites:

    connect()   the MODE ON THE SOCKET decides, plus x on the directory
                to get there at all
    bind()      the WRITE BIT ON THE DIRECTORY decides.  The mode on the
                socket has nothing to do with it

A unix socket is created by `bind()` the way a file is created by `open()`:
it is a new entry in a directory, so the directory must be writable by
whoever makes it.  `srw-rw-rw-` says every account may *reach* the node; it
says nothing about who may *start* one.

`bind_socket()` (`lib/rundir.c`) makes that sharper rather than softer.  A
socket left behind by a node that died is still in the way - `bind()` says
EADDRINUSE - so it looks at what is there, refuses to touch anything that is
not a socket, checks that nobody is listening on it, and only then removes
it and binds again.  **Both** the removing and the binding need write
permission on the directory.

**The answer is `chown`, not `chmod`.**  A node running as root needs
nothing at all - root writes whatever the bits say, and `root:hams 0750` is
the ordinary case.  A node under a normal account wants the directory to BELONG
to that account, with the mode unchanged:

    drwxr-x---  thomas:hams  /tcp/sockets

    thomas    owner, may create the socket    -> the node starts
    hams      r-x, may enter and connect      -> the clients get in
    others    nothing

Opening it up instead - 770, let alone 777 - is the worse trade by some way,
and the next section says why: the moment the group may write, any member of
it may delete `ax25` and bind their own socket under that name, and every
client will connect to them with correct permissions and nothing to notice.
Where that really is wanted, several daemons publishing their own sockets in
one place, it is 1770 and the sticky bit is not optional.

Running `make install` as root takes the ownership back (`chown -R root`,
and deliberately so: a root node reads net.rc, where `!` runs a shell
command).
The running node keeps the sockets it already has, so nothing breaks until
the next restart, and then `cnet` cannot reach it.  What it says at that
point is

    cannot listen on unix:/tcp/sockets/ax25: Permission denied

and it says it to syslog, because a node started at boot has no console.
`journalctl -u wampes` is where to look.

### The sticky bit on sockets/

At 750 it makes no difference: only root may create or remove entries.
It matters the moment the directory is opened up for writing - say 1770
so that several daemons can publish their own sockets there - and then it
is not optional.  Without it, any member of that group may delete `ax25`
and bind their own socket under the same name, and every client connects
to them instead, with correct permissions and nothing to notice.

`bind_socket()` does not help.  It keeps the node from clearing away
somebody else's socket; it cannot keep somebody else from clearing away
the node's.

### Saying something else in net.rc

The defaults are a starting point.  net.rc runs after the sockets exist,
so these win:

    axsock                    show owner, group and mode
    axsock group <name>       set the group
    axsock mode <octal>       set the mode

The one that the directory alone cannot express:

    axsock mode 0606          everybody EXCEPT the group on the socket -
                              they may be connected to, and may not
                              connect out

`$TCPDIR` itself stays 755.  Restricting the way in that far up cannot
work as soon as two parties have a legitimate claim, and traversal is not
what protects anything here.  What does need to be tighter than 755 is
any file holding credentials - a property of the file, not of the
directory, and it has to say so itself: 640 or 600, owned by root.

## What that admission is worth

Everything above decides **who gets in**.  This is the other half, and a
sysop should read it before being generous with the group: what a client
on `sockets/ax25` may then do.  It was asked (Thomas, 2026-08-28) as three
questions - can another local user inject, read along, or take over an
inbound session - and the three answers are different.

### Sending: any callsign, and that is deliberate

    connect DB0FHN --mycall DL9SAU-7      ax25subr.c, opts->ownsource
    connect DB0FHN < DL9SAU-7             the same thing, written short
    datagram                              the TNC2 header comes out of the
                                          DATA, not out of the command

There is no check that the callsign is yours, on either path, and there
cannot usefully be one - the node has no idea which local account belongs
to which amateur.  So:

**Reaching the service socket is permission to transmit under any
callsign.**

That is the sentence to weigh when picking the group.  It is not an
oversight - `axsock mode`/`axsock group` exist precisely so the decision
can be made - but it is the consequence, and callsign misuse is not a
small matter on the air.  `axsock tcp-listen` on 127.0.0.1 hands the same
permission to **every** local account; it is off unless net.rc says
otherwise, and that is why.

### Taking over an inbound session: no

`axlisten_client_claim()` has three gates:

    a port callsign                 refused - "belongs to a port"
    no "... client" entry in net.rc  refused - "is not open for clients"
    already claimed by someone       refused - "is already taken"

The configured entry is the **sysop's permission** and the claim is only
the client stepping into it, so nobody can listen for a callsign the
sysop did not open.  A second client is told no rather than quietly
shadowing the first.

Two things this does *not* do, and both are worth knowing:

* **It is first come, first served, with no identity check.**  Whoever
  claims first holds it.  Two users who may reach the socket race for the
  same callsign at startup.
* **A claim falls free when its holder goes.**  That is right - a crashed
  client must not leave a dead entry behind - but it means anyone else may
  then take it.  Which is why every way a client can die now names itself
  in the log; see the entry in `TODO.txt` about a user being thrown off
  when somebody else signs on.

### Reading along: no

* UI frames go to a client only when the destination equals the callsign
  it claimed (`axlisten_ui_deliver()`).  There is no promiscuous feed.
* Every connection is handed over as its own `socketpair`, one half passed
  by `SCM_RIGHTS`.  Clients share the control channel, never the data.
* The monitor is not on this socket at all.  `console` and `command` live
  only on the command channel in `.sockets`, which is 0700 on the
  directory and 0600 on the socket.

## What the node does about it

At startup, and only when running as root, it looks at `$TCPDIR`, at the
directory above it, at `.sockets`, `sbin`, `bin` and at the startup file,
and prints what is wrong:

    PERMISSIONS - this node runs as root:
      /usr/local belongs to uid 501, not to root
      /usr/local/wampes/net.rc is writable by group 20
      Anyone who can write those can run commands as root, through
      /usr/local/wampes/net.rc or through the command channel.

**It does not repair anything, on purpose.**  A node that quietly fixed
its own permissions would teach its operator to stop looking - and the
one case it failed to notice would be the one that mattered.  The check
is a second pair of eyes, not a guarantee.

Started as an ordinary user it says nothing.  There is no root to steal
then, and the operator is running it out of a directory of their own.

## Setting it right

`make install` as root does this, and prints what it did:

    chown -R root  $TCPDIR
    chmod 755      $TCPDIR and its directories
    chmod 700      $TCPDIR/.sockets
    chmod go-w     every file below it

By hand, including the part above `$TCPDIR` that `make install` will not
touch because it is not ours to take:

    sudo chown root /usr/local
    sudo chmod go-w /usr/local

Check it afterwards the way the node does - if this prints anything, read
it:

    find /usr/local /usr/local/wampes /usr/local/wampes/.sockets \
         -maxdepth 0 \( ! -user root -o -perm -g+w -o -perm -o+w \) -print

## The other programs

`conversd` and the mailbox run under the same conditions and read their
own configuration, so the same rule applies to their files.  They do not
yet make the check themselves; `make install` covers their files under
`$TCPDIR` all the same.
