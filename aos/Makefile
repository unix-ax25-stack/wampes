# @(#) $Id: Makefile,v 1.10 1998/03/09 17:46:44 deyke Exp $

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
		'CC=gcc' \
		'CFLAGS=-O2' \
		_all;; \
	NeXT*) \
		:;; \
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

clean:; @rm -f *.o $(LIBRARY) *~ \#*\#

depend:; @mkmf -f $(MAKEFILE)
###
ctime.o: time.h ansi.h cdefs.h tzfile.h
