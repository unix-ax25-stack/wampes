# @(#) $Id: Makefile,v 1.43 1999/01/22 21:20:01 deyke Exp $

TARGETS    = convers conversd

all:;        . ../lib/configure.mak; $(MAKE) -i _all
install:;    . ../lib/configure.mak; $(MAKE) -i _install
depend:;     . ../lib/configure.mak; $(MAKE) -i _depend

_all:   $(TARGETS)

convers: convers.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) convers.o $(LIBS) $(SLIBS) -o $@

conversd: conversd.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) conversd.o $(LIBS) $(SLIBS) -o $@

_install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#

_depend:; @mkmf
###
convers.o: ../lib/buildsaddr.h ../lib/configure.h
conversd.o: ../lib/buildsaddr.h ../lib/configure.h ../lib/md5.h \
	../lib/strdup.h
