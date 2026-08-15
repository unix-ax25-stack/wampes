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

Everything on the way to `$TCPDIR`, and everything inside it, must

* belong to **root**, and
* not be writable by group or by others.

A directory with the sticky bit is exempt from the second half - `/tmp`
is writable by everyone and still nobody can rename another's entry
there.

One thing is deliberately group-writable and must stay that way:
`$TCPDIR/sockets/ax25`, mode 0660, group `hams`.  That is the service
socket libax25 programs connect to, and its permissions are the whole
access control for it.  The node sets it on every start; if the group
does not exist it says so and leaves the socket to root alone.

That is why `$TCPDIR` should be `root:hams` and mode 750 rather than
`root:wheel`: a member of `hams` has to be able to traverse into
`sockets/`, or the socket's own permissions are worth nothing.

## What the node does about it

At startup, and only when running as root, it looks at the path and at
the directories and files it will read, and prints what is wrong:

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
    chgrp -R hams  $TCPDIR          # if that group exists
    chmod 750      $TCPDIR and its directories
    chmod 700      $TCPDIR/.sockets
    chmod go-w     every file below it

By hand, including the part above `$TCPDIR` that `make install` will not
touch because it is not ours to take:

    sudo chown root /usr/local
    sudo chmod go-w /usr/local

Check it afterwards the way the node does - if this prints anything, read
it:

    find / /usr /usr/local /usr/local/wampes -maxdepth 0 \
         \( ! -user root -o -perm -g+w -o -perm -o+w \) -print

## The other programs

`conversd` and the mailbox run under the same conditions and read their
own configuration, so the same rule applies to their files.  They do not
yet make the check themselves; `make install` covers their files under
`$TCPDIR` all the same.
