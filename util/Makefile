# @(#) $Id: Makefile,v 1.71 1999/01/24 22:25:10 deyke Exp $

TARGETS    = bridge cnet md5 mkhostdb netupdc netupds path qth udbm

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

bridge: bridge.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) bridge.o $(LIBS) $(SLIBS) -o $@

cnet:   cnet.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) cnet.o $(LIBS) $(SLIBS) -o $@

md5:    md5drivr.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) md5drivr.o $(LIBS) $(SLIBS) -o $@

mkhostdb: mkhostdb.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) mkhostdb.o $(LIBS) $(SLIBS) -o $@

netupdc: netupds
	$(RMTARGET)
	ln netupds $@

netupds: netupds.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) netupds.o $(LIBS) $(SLIBS) -o $@

path:   path.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) path.o $(LIBS) $(SLIBS) -o $@

qth:    qth.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) qth.o $(LIBS) $(SLIBS) -lm -o $@

udbm:   udbm.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) udbm.o $(LIBS) $(SLIBS) -o $@

_install: \
	bridge \
	netupdc \
	netupds \
	/usr/local/bin/cnet \
	/usr/local/bin/md5 \
	/usr/local/bin/path \
	/usr/local/bin/qth \
	/usr/local/etc/mkhostdb \
	/usr/local/etc/udbm

/usr/local/bin/cnet: cnet
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/md5: md5
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/etc/mkhostdb: mkhostdb
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/path: path
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/qth: qth
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/etc/udbm: udbm
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#
###
bridge.o: ../lib/buildsaddr.h ../lib/configure.h
cnet.o: ../lib/buildsaddr.h ../lib/configure.h
md5drivr.o: ../lib/md5.h
netupds.o: ../lib/buildsaddr.h ../lib/calc_crc.h ../lib/configure.h \
	../lib/lockfile.h ../lib/md5.h ../lib/strdup.h ../lib/strmatch.h
path.o: ../lib/strdup.h ../lib/configure.h
udbm.o: ../lib/calc_crc.h ../lib/callvalid.h ../lib/configure.h
