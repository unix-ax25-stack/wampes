# @(#) $Id: Makefile,v 1.34 1999/01/22 21:22:12 deyke Exp $

LIBRARY       = libutil.a

OBJS          = buildsaddr.o \
		calc_crc.o \
		callvalid.o \
		lockfile.o \
		md5.o \
		seteugid.o \
		strdup.o \
		strerror.o \
		strmatch.o \
		strtoul.o

all:;   if   [ -x /bin/bash ]; then /bin/bash configure; \
	elif [ -x /bin/ksh  ]; then /bin/ksh  configure; \
	else                        /bin/sh   configure; \
	fi
	. ../lib/configure.mak; $(MAKE) -i _all

depend:; . ../lib/configure.mak; $(MAKE) -i _depend

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
buildsaddr.o: buildsaddr.h
calc_crc.o: calc_crc.h
callvalid.o: callvalid.h
lockfile.o: lockfile.h seek.h
md5.o: md5.h
seteugid.o: seteugid.h
strdup.o: configure.h strdup.h
strerror.o: configure.h strerror.h
strmatch.o: strmatch.h
strtoul.o: configure.h strtoul.h
