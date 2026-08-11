# @(#) $Id: Makefile,v 1.37 2000/01/15 18:34:12 deyke Exp $

LIBRARY       = libutil.a

OBJS          = buildsaddr.o \
		calc_crc.o \
		callvalid.o \
		hostdb.o \
		lockfile.o \
		md5.o \
		ndbm.o \
		seteugid.o \
		strdup.o \
		strerror.o \
		strmatch.o \
		strtoul.o

all:;   @echo "Configuring ..."
	@if   [ -x /bin/bash ]; then /bin/bash configure; \
	 elif [ -x /bin/ksh  ]; then /bin/ksh  configure; \
	 else                        /bin/sh   configure; \
	 fi
	@. ../lib/configure.mak; $(MAKE) -i _all

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@ar cru $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

clean:; @rm -f *.o *.a configure.h configure.mak *~ \#*\#
###
buildsaddr.o:\
	buildsaddr.c\
	buildsaddr.h
calc_crc.o:\
	calc_crc.c\
	calc_crc.h
callvalid.o:\
	callvalid.c\
	callvalid.h
hostdb.o:\
	hostdb.c\
	hostdb.h
lockfile.o:\
	lockfile.c\
	lockfile.h\
	seek.h
md5.o:\
	md5.c\
	md5.h
seteugid.o:\
	seteugid.c\
	seteugid.h
strdup.o:\
	configure.h\
	strdup.c\
	strdup.h
strerror.o:\
	configure.h\
	strerror.c\
	strerror.h
strmatch.o:\
	strmatch.c\
	strmatch.h
strtoul.o:\
	configure.h\
	strtoul.c\
	strtoul.h
