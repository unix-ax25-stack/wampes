# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/aos/Makefile,v 1.6 1995/03/13 13:32:27 deyke Exp $

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
	FreeBSD*) \
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
		$(MAKE) -i -f $(MAKEFILE) \
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
