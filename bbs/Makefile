# @(#) $Id: Makefile,v 1.60 2000/01/15 18:34:09 deyke Exp $

TARGETS    = bbs

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

bbs:    bbs.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) bbs.o $(LIBS) $(SLIBS) -o $@

_install: /usr/local/bin/bbs /usr/local/lib/bbs.help

# bbs used to be installed setuid root.  It needs no root: it is started by
# the user who connected, and the only thing it cannot reach as that user are
# the three shared spool directories.  A group owning those covers it, and
# then no part of bbs runs with privileges at all.  See README.bbs.
#
# Set up the directories once, as root:
#	groupadd hams
#	chgrp -R hams $(TCPDIR)/locks NEWS_DIR UUCP_DIR
#	chmod -R g+w  $(TCPDIR)/locks NEWS_DIR UUCP_DIR
#	chmod 2775    $(TCPDIR)/locks NEWS_DIR UUCP_DIR
#
# and name the group when installing, so that the setgid bit survives - the
# rule below replaces the file, which loses group and mode every time:
#	make install BBSGROUP=hams
#
# NEWS_DIR and UUCP_DIR are whatever lib/configure found; see lib/configure.h.
# Store and forward - bbs -f system [-e] - checks getuid() and still has to be
# run as root, as it always did.

BBSGROUP   =

/usr/local/bin/bbs: bbs
	$(MKDIR)
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp $? $@
	@if [ -n "$(BBSGROUP)" ]; then \
		echo "chgrp $(BBSGROUP) $@ && chmod 2555 $@"; \
		chgrp $(BBSGROUP) $@ && chmod 2555 $@; \
	else \
		echo "chmod 555 $@"; \
		chmod 555 $@; \
	fi
	ln $@ /usr/local/bin/BBS

/usr/local/lib/bbs.help: bbs.help
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
bbs.o:\
	../lib/buildsaddr.h\
	../lib/callvalid.h\
	../lib/configure.h\
	../lib/lockfile.h\
	../lib/seteugid.h\
	../lib/strdup.h\
	../lib/strmatch.h\
	bbs.c
