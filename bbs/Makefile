# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.51 1996/05/28 15:57:43 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
LIBS       = ../lib/libutil.a
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi
RMTARGET   = @if [ -f $@ ]; then rm -f $@ || mv -f $@ $@~; fi
SLIBS      =

TARGETS    = bbs

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
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O -I../lib' \
		_all;; \
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

bbs:    bbs.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) bbs.o $(LIBS) $(SLIBS) -o $@

install: /usr/local/bin/bbs /usr/local/lib/bbs.help

/usr/local/bin/bbs: bbs
	$(MKDIR)
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp $? $@
	chmod 4555 $@
	ln $@ /usr/local/bin/BBS

/usr/local/lib/bbs.help: bbs.help
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; @rm -f *.o $(TARGETS) *~

depend:; @mkmf -f $(MAKEFILE)
###
bbs.o: ../lib/buildsaddr.h ../lib/callvalid.h ../lib/configure.h \
	../lib/lockfile.h ../lib/seteugid.h ../lib/strdup.h ../lib/strmatch.h
