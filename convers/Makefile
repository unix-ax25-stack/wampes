# @(#) $Id: Makefile,v 1.47 2005/03/11 14:36:09 dl9sau Exp $

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

_install: $(BINDIR)/convers $(SBINDIR)/conversd
	@sh ../lib/publiclink "$(BINDIR)" "$(PUBLICBINDIR)" convers

$(BINDIR)/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

# conversd is a daemon, so it belongs in sbin, under its plain name - inside
# $(TCPDIR) it is unambiguous.  The symlink at the old place keeps existing
# start scripts working.
$(SBINDIR)/conversd: conversd
	$(MKDIR)
	$(RMTARGET)
	cp $? $@
	rm -f $(TCPDIR)/conversd
	ln -s sbin/conversd $(TCPDIR)/conversd



clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
convers.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	convers.c
conversd.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	../lib/md5.h\
	../lib/strdup.h\
	conversd.c
