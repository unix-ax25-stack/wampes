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
# "make install" installs what has been built, and nothing else, so the two
# modes stay consistent without a second list to keep in step.

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
	@-for dir in $(MINDIRS); do ( cd $$dir; $(MAKE) -i min install ); done
	@-. lib/configure.mak; $(MAKE) -i _hostdb

complete:; @-chmod 755 cc
	@-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i all install ); done
	@-. lib/configure.mak; $(MAKE) -i _hostdb
	@-if [ -d tools ]; then ( cd tools; $(MAKE) -i all install ); fi

# TCPDIR comes from lib/configure, the same way the subdirectories get it.
# It used to be set to /tcp here as well, so on a system configured for any
# other directory the database was built where the programs do not read it.
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
