# Where WAMPES puts its files

This describes the layout as installed today, and what would have to change to
build a distribution package.  It is not a manual; it is the note that keeps
the next person from having to work all of this out again.

## Today

Everything lives under one root, `TCPDIR`, which `lib/configure` fixes at
build time and which defaults to `/usr/local/wampes`:

    $TCPDIR/bin/      bbs cnet convers md5 path qth
    $TCPDIR/sbin/     net conversd mkhostdb qaddr qname
    $TCPDIR/bbs/      bbs.help bbs_import
    $TCPDIR/sockets/  convers                     0755
    $TCPDIR/.sockets/ netcmd                      0700
    $TCPDIR/          the configuration and the data files, see below

    $TCPDIR/net       -> sbin/net                 compatibility symlinks for
    $TCPDIR/conversd  -> sbin/conversd            older setups

    /usr/local/bin/   wampes-bbs wampes-cnet wampes-convers wampes-path
                      wampes-qth

Four settings steer this, all of them remembered by `lib/configure` once given,
so that `TCPDIR=/somewhere ./configure && make install` really does install
somewhere else:

| variable | what it holds | default |
|---|---|---|
| `TCPDIR` | the root of everything | `/usr/local/wampes` |
| `BINDIR` | user commands | `$TCPDIR/bin` |
| `SBINDIR` | daemons and administrative commands | `$TCPDIR/sbin` |
| `PUBLICBINDIR` | where the `wampes-` symlinks go, empty to skip them | `/usr/local/bin` |

The prefix is for the shared directory only.  Inside `$TCPDIR/sbin` a program
called `net` is unambiguous; in `/usr/local/bin` it is not, and neither are
`path`, `md5` or `import`.  A package that installs into `/usr/bin` would set
`PUBLICBINDIR=` and use the prefixed names throughout.

## The two socket directories

Not an accident of history, but a rights boundary:

* `sockets/`, mode 0755, holds the convers socket, which every user needs.
  The socket itself is chmod'ed 0666 after binding.
* `.sockets/`, mode 0700, holds `netcmd`, which is `net`'s command line -
  whoever can reach it can reconfigure the node.  `cnet` is the client.

`lib/rundir.c` enforces both: `mkdir` alone would leave the mode to the umask,
so `chmod` follows and also repairs a directory that is already there.  The two
are separate functions on purpose.  `create_rundir()` makes the public one and
both daemons call it; `create_admin_rundir()` makes the 0700 one and only `net`
calls it, so that a `conversd` started first and running as somebody else
cannot end up owning the directory that guards the command channel.

## What a packaged install would look like

FHS separates three things that `TCPDIR` currently keeps together.  Every path
derived from it sorts cleanly, the two compatibility symlinks above aside:

| goes to | files |
|---|---|
| `/etc/wampes/` | `net.rc`, `bbs.conf`, `bbsrc`, `convers.conf`, `mail.conf`, `hosts`, `domain.txt`, `domain.local` |
| `/var/lib/wampes/` | `hostaddr`, `hostname`, `arp_data`, `route_data`, `axroute_data`, the three matching `*_tmp`, `locks/` |
| `/run/wampes/` | `sockets/convers`, `.sockets/netcmd` |
| `/usr/share/wampes/` | `bbs/bbs.help` |
| `/usr/bin`, `/usr/sbin` | the programs, already separated by `BINDIR` and `SBINDIR` |

Three more variables would carry that: `SYSCONFDIR`, `LOCALSTATEDIR`,
`RUNDIR`.  The mechanics are in place - the work is the sorting above, and it
is done.

Note the pair `hosts` -> `hostaddr`/`hostname`.  The first is hand-written
configuration, the other two are the databases `mkhostdb` builds from it.  A
package ships the source under `/etc` and the machine generates the databases
under `/var/lib`, which is why `mkhostdb` has to be installed and not just
built.

`/run` is cleared at every boot, and that needs no `tmpfiles.d`: both daemons
create what they need at startup, and `conversd` no longer depends on `net`
having run first.  The same holds for `/var/run` on macOS, which is likewise
emptied at boot.  `/dev/shm` is not a candidate - it is the namespace for
POSIX shared memory and stands at 1777, so any local user could create the
directory first and own it, which for the command channel is exactly wrong.

## Two limits worth knowing

A unix socket path cannot exceed `sun_path`, which is 104 bytes on macOS and
108 on Linux.  `build_sockaddr()` checks this and refuses the address rather
than truncating it; the daemon then reports which address it could not use.
Under `/run/wampes` there is room to spare, but a deeply nested test prefix
will run into it.

Neither daemon removes its socket when it dies, and neither should have to.
`bind_socket()` in `lib/rundir.c` cleans up a leftover when it can prove
nobody is listening, and refuses to touch one when somebody is - so a crash
never blocks the next start, and a second daemon never takes the name away
from a running first.
