# @(#) $Id: Makefile,v 1.14 2000/01/15 18:34:09 deyke Exp $

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
	@ar cru $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

clean:; @rm -f *.o *.a *~ \#*\#
###
ctime.o:\
	ansi.h\
	cdefs.h\
	ctime.c\
	time.h\
	tzfile.h
getcwd.o:\
	getcwd.c
putenv.o:\
	putenv.c
setsid.o:\
	setsid.c
strstr.o:\
	strstr.c
