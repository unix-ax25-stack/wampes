# @(#) $Id: Makefile,v 1.50 2000/03/04 18:31:08 deyke Exp $

# "make" builds what a WAMPES node needs to run: the daemon, the command
# client, the host database tool, the route display and qth.  "make complete"
# builds everything else as well - the BBS, convers, the extra utilities and
# the tools directory.
#
# The split is deliberate rather than tidy-minded.  The BBS is the largest
# thing here and the one with the most attack surface; a node that does not
# offer a BBS should not have one lying around, built and installed, waiting
# for someone to find a way to reach it.
#
# Installing is a separate step, in two matching sizes: "make install" for the
# node, "make install-complete" for everything.  Each installs what has been
# built, and nothing else, so the modes stay consistent without a second list
# to keep in step.
#
# Building used to install as a side effect, all the way back to the original
# tree, from a time when everything landed in one directory.  Today an install
# writes to $TCPDIR/bin, $TCPDIR/sbin and the shared bin directory as well, and
# a build that does that unasked is a trap - it goes off while you are testing
# a change, in the system you were testing it against.

MINDIRS    = lib \
	     src \
	     util

DIRS       = lib \
	     aos \
	     NeXT \
	     src \
	     convers \
	     util \
	     bbs

all:;   @-chmod 755 cc
	@-for dir in $(MINDIRS); do ( cd $$dir; $(MAKE) -i min ); done

complete:; @-chmod 755 cc
	@-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i all ); done
	@-if [ -d tools ]; then ( cd tools; $(MAKE) -i all ); fi

install: all
	@-for dir in $(MINDIRS); do ( cd $$dir; $(MAKE) -i install ); done
	@-. lib/configure.mak; $(MAKE) -i _hostdb
	@. lib/configure.mak; $(MAKE) _secure

install-complete: complete
	@-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i install ); done
	@-if [ -d tools ]; then ( cd tools; $(MAKE) -i install ); fi
	@-. lib/configure.mak; $(MAKE) -i _hostdb
	@. lib/configure.mak; $(MAKE) _secure

# The node usually runs as root, and it reads net.rc, in which "!" runs a
# shell command.  Anything under TCPDIR that a non-root user may write is
# therefore a way to become root: take the account, edit net.rc, wait.  So
# after installing, take ownership and close the door.  Not run with -i - a
# silent failure here is the case this exists to prevent.
#
# 755 and not 750, deliberately.  The access control belongs on the socket,
# not on the way to it: remote_net.c publishes sockets/ax25 with mode 0660 and
# a group of the sysop's choosing, and that is where the decision is made.
# Locking the directory to one group cannot work anyway as soon as two parties
# have a legitimate claim - users in "hams" and a mailbox running under
# "daemon", say - and it would puzzle anybody who is not deep in Unix
# permissions.  755 also agrees with what lib/rundir.c sets at every start,
# so the two do not fight each other.
#
# Files lose group and world write.  Anything holding credentials wants less
# than that and has to say so itself; see doc/PERMISSIONS.md.
_secure:
	@if [ -z "$(TCPDIR)" ]; then \
		echo "TCPDIR is empty - run make at the top level"; exit 1; fi
	@if [ "`id -u`" != 0 ]; then \
		echo ""; \
		echo "NOT ROOT - ownership and modes left alone."; \
		echo "$(TCPDIR) must belong to root and be writable by nobody else:"; \
		echo "the node runs as root and reads net.rc, where \"!\" runs a shell"; \
		echo "command.  Re-run \"make install\" as root."; \
		exit 0; \
	fi; \
	chown -R root $(TCPDIR) || exit 1; \
	find $(TCPDIR) -type d ! -name .sockets ! -name sockets \
		-exec chmod 755 {} \; ; \
	[ -d $(TCPDIR)/.sockets ] && chmod 700 $(TCPDIR)/.sockets; \
	[ -d $(TCPDIR)/sockets ] && chmod 750 $(TCPDIR)/sockets; \
	find $(TCPDIR) -type f -exec chmod go-w {} \; ; \
	echo "$(TCPDIR): root, 755, nothing below it writable by anyone else"; \
	left=`find $(TCPDIR) ! -user root -print 2>/dev/null | head -5`; \
	if [ -n "$$left" ]; then \
		echo "still not owned by root, and each one is a way in:"; \
		echo "$$left" | sed 's/^/  /'; \
	fi

# TCPDIR comes from lib/configure, the same way the subdirectories get it.
# It used to be set to /tcp here as well, so on a system configured for any
# other directory the database was built where the programs do not read it.
# This writes into TCPDIR, which is why it hangs off install and not off all.
_hostdb:
	@if [ -z "$(TCPDIR)" ]; then \
		echo "TCPDIR is empty - run make at the top level"; exit 1; fi
	@$(MAKE) $(TCPDIR)/hostaddr.pag

$(TCPDIR)/hosts:
	[ -f $(TCPDIR)/hosts ] || touch $(TCPDIR)/hosts

$(TCPDIR)/domain.txt:
	[ -f $(TCPDIR)/domain.txt ] || touch $(TCPDIR)/domain.txt

$(TCPDIR)/hostaddr.pag: $(TCPDIR)/hosts $(TCPDIR)/domain.txt util/mkhostdb
	rm -f $(TCPDIR)/hostaddr.* $(TCPDIR)/hostname.*
	util/mkhostdb >/dev/null 2>&1
	if [ -f $(TCPDIR)/hostaddr.db ]; then ln $(TCPDIR)/hostaddr.db $@; fi

distrib:
	@-rm -f wampes-*.t*; \
	sources=`find \
		*.R \
		ChangeLog \
		Makefile \
		NeXT/*.[ch] \
		NeXT/*.sh \
		NeXT/Makefile \
		NeXT/README.NeXT \
		NeXT/uname-next \
		README \
		aos/*.[ch] \
		aos/Makefile \
		bbs/*.[ch] \
		bbs/*.py \
		bbs/Makefile \
		bbs/bbs.help \
		cc \
		convers/*.[ch] \
		convers/Makefile \
		doc/?*.* \
		examples/?*.* \
		lib/*.[ch] \
		lib/Makefile \
		lib/configure \
		src/*.[ch] \
		src/Makefile \
		src/linux_include/*/*.h \
		util/*.[ch] \
		util/Makefile \
		! -name configure.h -print`; \
	tar cf wampes-latest.tar $$sources; \
	version=`awk -F- '/.#.WAMPES-/ {print substr($$2,1,6)}' < src/version.c`; \
	ln wampes-latest.tar wampes-$$version.tar; \
	ln README wampes-$$version.txt; \
	gzip -9 < wampes-latest.tar > wampes-$$version.tar.gz; \
	if [ `hostname` = deyke1.fc.hp.com ]; then \
		gpg --detach-sign --force-v3-sigs --armor wampes-$$version.tar.gz; \
	fi

clean:; @-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i clean ); done
	@-if [ -d tools ]; then  ( cd tools; $(MAKE) -i clean ); fi
	@-rm -f wampes-??????.t*
	@-find . -name .pure -exec rm {} \;
	@-find . -name .trimtime -exec rm {} \;
###
