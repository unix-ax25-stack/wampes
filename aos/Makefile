# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/aos/Makefile,v 1.5 1994/07/22 13:18:15 deyke Exp $

LIBRARY       = libaos.a

MAKEFILE      = Makefile

OBJS          = ctime.o \
		getcwd.o \
		putenv.o \
		setsid.o \
		strstr.o

RANLIB        = ranlib

all:;   @case "`uname -sr`" in \
	HP-UX*) \
		:;; \
	AIX*) \
		:;; \
	IRIX*) \
		:;; \
	Linux*) \
		:;; \
	386BSD*) \
		:;; \
	BSD/386*) \
		:;; \
	SunOS\ 4.*) \
		:;; \
	SunOS\ 5.*) \
		:;; \
	ULTRIX*) \
		:;; \
	A/UX*) \
		:;; \
	RISC\ iX*) \
		:;; \
	AOS*) \
		/usr/local/bin/make -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@sleep 1
	@ar cru $@ $(OBJS)
	@$(RANLIB) $@
	@echo "done"

install:;

clean:; @rm -f *.o $(LIBRARY) *~

depend:; @mkmf -f $(MAKEFILE)
###
ctime.o: time.h ansi.h cdefs.h tzfile.h
