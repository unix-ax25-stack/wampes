# @(#) $Id: Makefile,v 1.11 1999/01/22 21:22:21 deyke Exp $

LIBRARY       = libaos.a

OBJS          = ctime.o \
		getcwd.o \
		putenv.o \
		setsid.o \
		strstr.o

all:;   @case "`uname -sr`" in \
	AOS*) . ../lib/configure.mak; $(MAKE) -i _all;; \
	esac

depend:; @case "`uname -sr`" in \
	AOS*) . ../lib/configure.mak; $(MAKE) -i _depend;; \
	esac

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@sleep 1
	@ar cru $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

clean:; @rm -f *.o *.a *~ \#*\#

_depend:; @mkmf
###
ctime.o: time.h ansi.h cdefs.h tzfile.h
