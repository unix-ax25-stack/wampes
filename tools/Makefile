# @(#) $Id: Makefile,v 1.39 1999/01/22 21:21:39 deyke Exp $

TARGETS    = \
	     check_seteugid \
	     connect \
	     connect2 \
	     filter \
	     import \
	     makeiprt \
	     mm2html \
	     ms2mm \
	     udpbridge

all:;        . ../lib/configure.mak; $(MAKE) -i _all
install:;    . ../lib/configure.mak; $(MAKE) -i _install
depend:;     . ../lib/configure.mak; $(MAKE) -i _depend

_all:   $(TARGETS)

check_seteugid: check_seteugid.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) check_seteugid.o $(LIBS) $(SLIBS) -o $@
	mk chown root $@
	mk chmod 4555 $@

connect: connect.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) connect.o $(LIBS) $(SLIBS) -o $@

connect2: connect2.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) connect2.o $(LIBS) $(SLIBS) -o $@

filter: filter.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) filter.o $(LIBS) $(SLIBS) -o $@

import: import.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) import.o $(LIBS) $(SLIBS) -o $@

makeiprt: makeiprt.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) makeiprt.o $(LIBS) $(SLIBS) -o $@

mm2html: mm2html.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) mm2html.o $(LIBS) $(SLIBS) -o $@

ms2mm:  ms2mm.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) ms2mm.o $(LIBS) $(SLIBS) -o $@

udpbridge: udpbridge.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) udpbridge.o $(LIBS) $(SLIBS) -o $@

_install: \
	/usr/local/bin/import

/usr/local/bin/import: import
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#

_depend:; @mkmf
###
check_seteugid.o: ../lib/seteugid.h
connect.o: ../lib/configure.h
hs.o: ../lib/buildsaddr.h
