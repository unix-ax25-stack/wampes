# @(#) $Id: Makefile,v 1.4 1999/01/27 18:47:13 deyke Exp $

LIBRARY       = libnext.a

OBJS          = next.o \
		putenv.o

all:;   @case "`uname -sr`" in \
	NeXT*) . ../lib/configure.mak; $(MAKE) -i _all;; \
	esac

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@ar cru $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

clean:; @rm -f *.o *.a *~ \#*\#
###
next.o: next.h
