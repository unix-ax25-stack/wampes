# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.38 1994/02/28 11:55:35 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = bbs findpath killdup sfstat

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		make -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lndbm' \
		_all;; \
	AIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	Linux*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O -I../lib' \
		'LIBS=../lib/libutil.a -ldbm' \
		_all;; \
	386BSD*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	BSD/386*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	SunOS\ 4.*) \
		make -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	SunOS\ 5.*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'LIBS=../lib/libutil.a -lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a -ldbm' \
		_all;; \
	A/UX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		make -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i -f $(MAKEFILE) \
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
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

findpath: findpath.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

killdup: killdup.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

sfstat: sfstat.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

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

depend:; @mkmf -f $(MAKEFILE)
###
bbs.o: ../lib/bbs.h ../lib/configure.h ../lib/buildsaddr.h ../lib/callvalid.h \
	../lib/lockfile.h ../lib/seteugid.h ../lib/strdup.h
findpath.o: ../lib/bbs.h ../lib/configure.h ../lib/calc_crc.h \
	../lib/callvalid.h
killdup.o: ../lib/bbs.h ../lib/configure.h ../lib/md5.h
sfstat.o: ../lib/bbs.h ../lib/configure.h
