# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.38 1996/08/11 18:03:58 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
LIBS       = ../lib/libutil.a
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi
RMTARGET   = @if [ -f $@ ]; then rm -f $@ || mv -f $@ $@~; fi
SLIBS      =

TARGETS    = convers conversd

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		_all;; \
	AIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE -I../lib' \
		_all;; \
	IRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed -I../lib' \
		_all;; \
	Linux*) \
		if [ -r /lib/libc.so.5 ]; then \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O -I../lib' \
			_all; \
		else \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O -I../lib' \
			_all; \
		fi;; \
	386BSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		_all;; \
	FreeBSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		_all;; \
	BSD/386*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		_all;; \
	SunOS\ 4.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -I../lib' \
		_all;; \
	SunOS\ 5.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -v -I../lib' \
		'SLIBS=-lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC -I../lib' \
		_all;; \
	A/UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'SLIBS=-lposix' \
		_all;; \
	RISC\ iX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		_all;; \
	AOS*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(TARGETS)

convers: convers.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) convers.o $(LIBS) $(SLIBS) -o $@

conversd: conversd.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) conversd.o $(LIBS) $(SLIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#

depend:; @mkmf -f $(MAKEFILE)
###
convers.o: ../lib/buildsaddr.h
conversd.o: ../lib/buildsaddr.h ../lib/configure.h ../lib/md5.h \
	../lib/strdup.h
