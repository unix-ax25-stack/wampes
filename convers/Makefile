# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.32 1994/02/28 11:55:56 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
LDFLAGS    = $(CFLAGS) -s
MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = convers conversd

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		make -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a' \
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
		'LIBS=../lib/libutil.a' \
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
		'LIBS=../lib/libutil.a' \
		_all;; \
	A/UX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lposix' \
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

convers: convers.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

conversd: conversd.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(LDFLAGS) $? $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf -f $(MAKEFILE)
###
convers.o: ../lib/buildsaddr.h
conversd.o: ../lib/buildsaddr.h ../lib/configure.h ../lib/md5.h \
	../lib/strdup.h
