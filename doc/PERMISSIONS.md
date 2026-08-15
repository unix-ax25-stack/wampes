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
a mistake.  The node sets `0660` group `hams` on every start where that
group exists, and says so when it does not.

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

`sockets/` is created **0750** and the socket inside it **0777**.  That
looks backwards until you see which one is the gate: who may reach the
service is decided by the group on the directory, and the sysop moves the
whole service from one group to another with a single `chgrp`, without
having to think about socket modes at all.

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

    axsock mode 0707          everybody EXCEPT the group on the socket -
                              they may be connected to, and may not
                              connect out

`$TCPDIR` itself stays 755.  Restricting the way in that far up cannot
work as soon as two parties have a legitimate claim, and traversal is not
what protects anything here.  What does need to be tighter than 755 is
any file holding credentials - a property of the file, not of the
directory, and it has to say so itself: 640 or 600, owned by root.

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
