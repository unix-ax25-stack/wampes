# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.62 1996/05/28 15:58:00 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
LIBS       = ../lib/libutil.a
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi
RMTARGET   = @if [ -f $@ ]; then rm -f $@ || mv -f $@ $@~; fi
SLIBS      =

TARGETS    = bridge cnet md5 mkhostdb netupdc netupds path pop2d pop3d qth udbm

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		'SLIBS=-lc -lcurses -lndbm' \
		_all;; \
	AIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE -I../lib' \
		'SLIBS=-lcurses' \
		_all;; \
	IRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed -I../lib' \
		'SLIBS=-lcurses' \
		_all;; \
	Linux*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lcurses -ltermcap -ldbm' \
		_all;; \
	386BSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lcurses -ltermcap' \
		_all;; \
	FreeBSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lcurses -ltermcap -lcrypt' \
		_all;; \
	BSD/386*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lcurses -ltermcap' \
		_all;; \
	SunOS\ 4.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lcurses -ltermcap' \
		_all;; \
	SunOS\ 5.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -v -I../lib' \
		'SLIBS=-lcurses -lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC -I../lib' \
		'SLIBS=-lcurses -ltermcap -ldbm' \
		_all;; \
	A/UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'SLIBS=-lcurses -ltermcap -ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'SLIBS=-lcurses -ltermcap' \
		_all;; \
	AOS*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -Dibm6153 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		'SLIBS=-lcurses -ltermcap' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

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

pop2d:  pop2d.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) pop2d.o $(LIBS) $(SLIBS) -o $@

pop3d:  pop3d.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) pop3d.o $(LIBS) $(SLIBS) -o $@

qth:    qth.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) qth.o $(LIBS) $(SLIBS) -lm -o $@

udbm:   udbm.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) udbm.o $(LIBS) $(SLIBS) -o $@

install: \
	bridge \
	netupdc \
	netupds \
	pop2d \
	pop3d \
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

clean:; @rm -f *.o $(TARGETS) *~

depend:; @mkmf -f $(MAKEFILE)
###
bridge.o: ../lib/buildsaddr.h
cnet.o: ../lib/buildsaddr.h
md5drivr.o: ../lib/md5.h
netupds.o: ../lib/buildsaddr.h ../lib/calc_crc.h ../lib/configure.h \
	../lib/lockfile.h ../lib/md5.h ../lib/strdup.h ../lib/strmatch.h
path.o: ../lib/strdup.h ../lib/configure.h
pop2d.o: ../lib/configure.h ../lib/lockfile.h ../lib/seek.h
pop3d.o: ../lib/configure.h ../lib/lockfile.h
udbm.o: ../lib/calc_crc.h ../lib/callvalid.h ../lib/configure.h
