# @(#) $Id: Makefile,v 1.45 1999/01/27 18:45:37 deyke Exp $

TARGETS    = convers conversd

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

convers: convers.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) convers.o $(LIBS) $(SLIBS) -o $@

conversd: conversd.o $(LIBS)
	$(RMTARGET)
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
###
convers.o: ../lib/buildsaddr.h ../lib/configure.h
conversd.o: ../lib/buildsaddr.h ../lib/configure.h ../lib/md5.h \
	../lib/strdup.h
