# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.36 1994/02/19 17:50:19 deyke Exp $

CFLAGS     = -I../lib
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = bbs findpath killdup sfstat

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lndbm' \
		_all;; \
	AIX*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -qchars=signed -D_ALL_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	Linux*) \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -ldbm' \
		_all;; \
	386BSD*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	BSD/386*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	SunOS\ 4.*) \
		make -i \
		'CC=acc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	SunOS\ 5.*) \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a -ldbm' \
		_all;; \
	A/UX*) \
		make -i \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(TARGETS)

bbs:    bbs.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

findpath: findpath.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

killdup: killdup.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

sfstat: sfstat.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat /usr/local/lib/bbs.help

/usr/local/bin/bbs: bbs
	$(MKDIR)
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp $? $@
	chmod 4555 $@
	ln $@ /usr/local/bin/BBS

/usr/local/bin/sfstat: sfstat
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/lib/bbs.help: help
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
bbs.o: ../lib/bbs.h ../lib/configure.h ../lib/buildsaddr.h ../lib/callvalid.h \
	../lib/seteugid.h ../lib/strdup.h
findpath.o: ../lib/bbs.h ../lib/configure.h ../lib/calc_crc.h \
	../lib/callvalid.h
killdup.o: ../lib/bbs.h ../lib/configure.h ../lib/md5.h
sfstat.o: ../lib/bbs.h ../lib/configure.h
