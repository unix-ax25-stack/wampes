# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.33 1996/08/11 18:17:09 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
LIBS       = ../lib/libutil.a
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi
RMTARGET   = @if [ -f $@ ]; then rm -f $@ || mv -f $@ $@~; fi
SLIBS      =

TARGETS    = \
	     check_seteugid \
	     connect \
	     connect2 \
	     filter \
	     import \
	     makeiprt \
	     mm2html \
	     ms2mm \
	     udpbridge

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		'SLIBS=-lndbm' \
		_all;; \
	AIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE -I../lib' \
		'SLIBS=' \
		_all;; \
	IRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed -I../lib' \
		'SLIBS=' \
		_all;; \
	Linux*) \
		if [ -r /lib/libc.so.5 ]; then \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O -I../lib' \
			'SLIBS=-lgdbm' \
			_all; \
		else \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O -I../lib' \
			'SLIBS=-ldbm' \
			_all; \
		fi;; \
	386BSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	FreeBSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	BSD/386*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	SunOS\ 4.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
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
		'SLIBS=-ldbm' \
		_all;; \
	A/UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'SLIBS=-ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'SLIBS=' \
		_all;; \
	AOS*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -Dibm6153 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		'SLIBS=' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(TARGETS)

check_seteugid: check_seteugid.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) check_seteugid.o $(LIBS) $(SLIBS) -o $@
	mk chown root $@
	mk chmod 4555 $@

connect: connect.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) connect.o $(LIBS) $(SLIBS) -o $@

connect2: connect2.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) connect2.o $(LIBS) $(SLIBS) -o $@

filter: filter.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) filter.o $(LIBS) $(SLIBS) -o $@

import: import.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) import.o $(LIBS) $(SLIBS) -o $@

makeiprt: makeiprt.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) makeiprt.o $(LIBS) $(SLIBS) -o $@

mm2html: mm2html.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) mm2html.o $(LIBS) $(SLIBS) -o $@

ms2mm:  ms2mm.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) ms2mm.o $(LIBS) $(SLIBS) -o $@

udpbridge: udpbridge.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) udpbridge.o $(LIBS) $(SLIBS) -o $@

install: \
	/usr/local/bin/import

/usr/local/bin/import: import
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~ \#*\#

depend:; @mkmf -f $(MAKEFILE)
###
check_seteugid.o: ../lib/seteugid.h
hs.o: ../lib/buildsaddr.h
