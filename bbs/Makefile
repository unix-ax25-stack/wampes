# @(#) $Id: Makefile,v 1.59 1999/01/27 18:46:52 deyke Exp $

TARGETS    = bbs

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

bbs:    bbs.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) bbs.o $(LIBS) $(SLIBS) -o $@

_install: /usr/local/bin/bbs /usr/local/lib/bbs.help

/usr/local/bin/bbs: bbs
	$(MKDIR)
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp $? $@
	chmod 4555 $@
	ln $@ /usr/local/bin/BBS

/usr/local/lib/bbs.help: bbs.help
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
bbs.o: ../lib/buildsaddr.h ../lib/callvalid.h ../lib/configure.h \
	../lib/lockfile.h ../lib/seteugid.h ../lib/strdup.h ../lib/strmatch.h
