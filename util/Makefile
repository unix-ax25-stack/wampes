# @(#) $Id: Makefile,v 1.76 2000/03/04 18:31:16 deyke Exp $

TARGETS    = bridge cnet md5 mkhostdb path qth

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

_all:   $(TARGETS)

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

_install: \
	bridge \
	/usr/local/bin/cnet \
	/usr/local/bin/md5 \
	/usr/local/bin/path \
	/usr/local/bin/qth

/usr/local/bin/cnet: cnet
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/md5: md5
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
	mkhostdb.c
path.o:\
	../lib/configure.h\
	../lib/strdup.h\
	path.c
qth.o:\
	qth.c
