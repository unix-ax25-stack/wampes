# @(#) $Id: Makefile,v 1.60 2000/01/15 18:34:09 deyke Exp $

TARGETS    = bbs bbs_import

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

bbs:    bbs.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) bbs.o $(LIBS) $(SLIBS) -o $@

bbs_import: bbs_import.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) bbs_import.o $(LIBS) $(SLIBS) -o $@

_install: $(BINDIR)/bbs $(TCPDIR)/bbs/bbs.help $(TCPDIR)/bbs/bbs_import
	@sh ../lib/publiclink "$(BINDIR)" "$(PUBLICBINDIR)" bbs

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

$(BINDIR)/bbs: bbs
	$(MKDIR)
	$(RMTARGET)
	cp $? $@
	@if [ -n "$(BBSGROUP)" ]; then \
		echo "chgrp $(BBSGROUP) $@ && chmod 2555 $@"; \
		chgrp $(BBSGROUP) $@ && chmod 2555 $@; \
	else \
		echo "chmod 555 $@"; \
		chmod 555 $@; \
	fi
	@# BBS is a second name for the same program.  On a case insensitive
	@# filesystem it already is the same file, so linking it would fail -
	@# and the "rm -f BBS" this rule used to do before copying deleted the
	@# bbs binary itself there.  Only link when it is really missing.
	@if [ ! $(BINDIR)/BBS -ef $@ ]; then \
		rm -f $(BINDIR)/BBS; \
		echo "ln $@ $(BINDIR)/BBS"; \
		ln $@ $(BINDIR)/BBS; \
	fi

# bbs.help used to sit in /usr/local/lib, a directory that has nothing else
# of ours in it.  It belongs with the rest of what the BBS needs.
$(TCPDIR)/bbs/bbs.help: bbs.help
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

# Called from a .forward, never by hand, so it belongs with the BBS and not
# among the commands.  It was /usr/local/bin/import before.
$(TCPDIR)/bbs/bbs_import: bbs_import
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
bbs_import.o:\
	bbs_import.c
bbs.o:\
	../lib/buildsaddr.h\
	../lib/callvalid.h\
	../lib/configure.h\
	../lib/lockfile.h\
	../lib/seteugid.h\
	../lib/strdup.h\
	../lib/strmatch.h\
	bbs.c
