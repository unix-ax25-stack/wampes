# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.31 1994/02/19 17:50:27 deyke Exp $

CFLAGS     = -I../lib
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = convers conversd

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a' \
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
		'LIBS=../lib/libutil.a' \
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
		'LIBS=../lib/libutil.a' \
		_all;; \
	A/UX*) \
		make -i \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lposix' \
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

convers: convers.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

conversd: conversd.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
convers.o: ../lib/buildsaddr.h
conversd.o: ../lib/buildsaddr.h ../lib/configure.h ../lib/md5.h \
	../lib/strdup.h
