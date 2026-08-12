# @(#) $Id: Makefile,v 1.76 2000/03/04 18:31:16 deyke Exp $

TARGETS    = bridge cnet md5 mkhostdb path qth
MINTARGETS = cnet mkhostdb path

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
min:;        @. ../lib/configure.mak; $(MAKE) -i _min
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

_min:   $(MINTARGETS)

bridge: bridge.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) bridge.o $(LIBS) $(SLIBS) -o $@

cnet:   cnet.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) cnet.o $(LIBS) $(SLIBS) -o $@

md5:    md5drivr.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) md5drivr.o $(LIBS) $(SLIBS) -o $@

mkhostdb: mkhostdb.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) mkhostdb.o $(LIBS) $(SLIBS) -o $@

path:   path.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) path.o $(LIBS) $(SLIBS) -o $@

qth:    qth.o $(LIBS)
	$(RMTARGET)
	$(LD) $(LDFLAGS) qth.o $(LIBS) $(SLIBS) -lm -o $@

# Only what has been built.  "make" builds MINTARGETS, "make complete"
# builds everything, and this rule must not drag the rest in through a
# dependency - which is why it is a loop and not a list of targets.
_install:
	@for p in cnet md5 path qth; do \
		[ -f $$p ] || continue; \
		if [ ! -d "$(BINDIR)" ]; then mkdir -p "$(BINDIR)"; fi; \
		echo "cp $$p $(BINDIR)/$$p"; \
		rm -f "$(BINDIR)/$$p"; \
		cp $$p "$(BINDIR)/$$p"; \
	done
	@# mkhostdb was never installed at all.  It did not need to be: the
	@# whole tree used to live at $(TCPDIR), so doc/intro.txt could speak
	@# of /tcp/util/mkhostdb and the top-level Makefile could call
	@# util/mkhostdb from the build directory and mean the same file.
	@# qaddr and qname are the same program under other names; it picks
	@# its mode from argv[0].
	@if [ -f mkhostdb ]; then \
		if [ ! -d "$(SBINDIR)" ]; then mkdir -p "$(SBINDIR)"; fi; \
		echo "cp mkhostdb $(SBINDIR)/mkhostdb"; \
		rm -f "$(SBINDIR)/mkhostdb"; \
		cp mkhostdb "$(SBINDIR)/mkhostdb"; \
		rm -f "$(SBINDIR)/qaddr" "$(SBINDIR)/qname"; \
		ln "$(SBINDIR)/mkhostdb" "$(SBINDIR)/qaddr"; \
		ln "$(SBINDIR)/mkhostdb" "$(SBINDIR)/qname"; \
	fi
	@# Last, so that the copies above are already in place - publiclink
	@# skips a name whose target does not exist.
	@sh ../lib/publiclink "$(BINDIR)" "$(PUBLICBINDIR)" cnet path qth

clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
bridge.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	bridge.c
cnet.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	cnet.c
md5drivr.o:\
	../lib/md5.h\
	md5drivr.c
mkhostdb.o:\
	../lib/configure.h\
	../lib/hostdb.h\
	mkhostdb.c
path.o:\
	../lib/configure.h\
	../lib/strdup.h\
	path.c
qth.o:\
	qth.c
