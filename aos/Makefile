# @(#) $Id: Makefile,v 1.12 1999/01/24 22:25:32 deyke Exp $

LIBRARY       = libaos.a

OBJS          = ctime.o \
		getcwd.o \
		putenv.o \
		setsid.o \
		strstr.o

all:;   @case "`uname -sr`" in \
	AOS*) . ../lib/configure.mak; $(MAKE) -i _all;; \
	esac

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@sleep 1
	@ar cru $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

clean:; @rm -f *.o *.a *~ \#*\#
###
ctime.o: time.h ansi.h cdefs.h tzfile.h
