# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.21 1994/07/22 13:18:02 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
LIBS       = ../lib/libutil.a
MAKEFILE   = Makefile
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi
SLIBS      =

TARGETS    = \
	     bridge \
	     check_seteugid \
	     connect \
	     connect2 \
	     filter \
	     makeiprt \
	     mp \
	     nos-cb \
	     s2m

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		make -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		'SLIBS=-lndbm' \
		_all;; \
	AIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE -I../lib' \
		'SLIBS=' \
		_all;; \
	IRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed -I../lib' \
		'SLIBS=' \
		_all;; \
	Linux*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-ldbm' \
		_all;; \
	386BSD*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	BSD/386*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	SunOS\ 4.*) \
		make -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=' \
		_all;; \
	SunOS\ 5.*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -I../lib' \
		'SLIBS=-lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC -I../lib' \
		'SLIBS=-ldbm' \
		_all;; \
	A/UX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'SLIBS=-ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		make -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'SLIBS=' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -Dibm6153 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		'SLIBS=' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(TARGETS)

bridge: bridge.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) bridge.o $(LIBS) $(SLIBS) -o $@

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

makeiprt: makeiprt.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) makeiprt.o $(LIBS) $(SLIBS) -o $@

mp:     mp.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) mp.o $(LIBS) $(SLIBS) -o $@

nos-cb: nos-cb.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) nos-cb.o $(LIBS) $(SLIBS) -o $@

s2m:    s2m.o $(LIBS)
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) s2m.o $(LIBS) $(SLIBS) -o $@

install:;

clean:; @rm -f *.o $(TARGETS) *~

depend:; @mkmf -f $(MAKEFILE)
###
bridge.o: ../lib/buildsaddr.h
check_seteugid.o: ../lib/seteugid.h
