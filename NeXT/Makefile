# @(#) $Id: Makefile,v 1.5 2000/01/15 18:34:09 deyke Exp $

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
next.o:\
	next.c\
	next.h
putenv.o:\
	putenv.c
