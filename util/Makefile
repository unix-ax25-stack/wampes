# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.46 1994/02/19 17:50:37 deyke Exp $

CFLAGS     = -I../lib
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = cnet md5 mkhostdb netupdc netupds path pop2d qth udbm

all:;   chmod 755 genupd
	@case "`uname -sr`" in \
	HP-UX*) \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -lndbm' \
		_all;; \
	AIX*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -qchars=signed -D_ALL_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses' \
		_all;; \
	Linux*) \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm' \
		_all;; \
	386BSD*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all;; \
	BSD/386*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all;; \
	SunOS\ 4.*) \
		make -i \
		'CC=acc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all;; \
	SunOS\ 5.*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm' \
		_all;; \
	A/UX*) \
		make -i \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -Dibm6153 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a -lcurses -ltermcap' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(TARGETS)

cnet:   cnet.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

md5:    md5drivr.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) md5drivr.o $(LIBS) -o $@

mkhostdb: mkhostdb.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

netupdc: netupdc.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

netupds: netupds.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

path:   path.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

pop2d:  pop2d.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

qth:    qth.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -lm -o $@

udbm:   udbm.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: \
	/usr/local/bin/cnet \
	/usr/local/bin/md5 \
	/usr/local/etc/mkhostdb \
	netupdc \
	netupds \
	/usr/local/bin/path \
	pop2d \
	/usr/local/bin/qth \
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

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
cnet.o: ../lib/buildsaddr.h
md5drivr.o: ../lib/md5.h
netupdc.o: ../lib/buildsaddr.h ../lib/configure.h netupd.h ../lib/strdup.h
netupds.o: ../lib/configure.h netupd.h
path.o: ../lib/strdup.h
pop2d.o: ../lib/configure.h
udbm.o: ../lib/bbs.h ../lib/configure.h ../lib/calc_crc.h ../lib/callvalid.h
