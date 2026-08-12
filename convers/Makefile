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

_install: $(BINDIR)/convers $(SBINDIR)/wampes-conversd publiclinks

$(BINDIR)/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

# conversd is a daemon, so it belongs in sbin.  The symlink at the old place
# keeps existing start scripts working; doc/wampes.mm has spoken of
# $(TCPDIR)/conversd for thirty years.
$(SBINDIR)/wampes-conversd: conversd
	$(MKDIR)
	$(RMTARGET)
	cp $? $@
	rm -f $(TCPDIR)/conversd
	ln -s sbin/wampes-conversd $(TCPDIR)/conversd

publiclinks:
	@sh ../lib/publiclink "$(BINDIR)" "$(PUBLICBINDIR)" convers

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
