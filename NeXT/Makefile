# @(#) $Id: Makefile,v 1.3 1999/01/24 22:25:14 deyke Exp $

LIBRARY       = libnext.a

OBJS          = next.o \
		putenv.o

all:;   @case "`uname -sr`" in \
	NeXT*) . ../lib/configure.mak; $(MAKE) -i _all;; \
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
next.o: next.h
