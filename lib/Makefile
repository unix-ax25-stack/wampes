# @(#) $Id: Makefile,v 1.37 2000/01/15 18:34:12 deyke Exp $

LIBRARY       = libutil.a

OBJS          = buildsaddr.o \
		calc_crc.o \
		callvalid.o \
		hostdb.o \
		lockfile.o \
		md5.o \
		ndbm.o \
		rundir.o \
		seteugid.o \
		strdup.o \
		strerror.o \
		strmatch.o \
		strtoul.o

min:         all

all:;   @echo "Configuring ..."
	@if   [ -x /bin/bash ]; then /bin/bash configure; \
	 elif [ -x /bin/ksh  ]; then /bin/ksh  configure; \
	 else                        /bin/sh   configure; \
	 fi
	@. ../lib/configure.mak; $(MAKE) -i _all

_all:   $(LIBRARY)

# "cr", not "cru".  The "u" means "replace only members that are newer", and
# current binutils build archives deterministically by default - every member
# gets timestamp 0, so nothing is ever newer and ar says so on every build:
#
#       ar: `u' modifier ignored since `D' is the default (see `U')
#
# It bought nothing here in any case: make already decides when the archive is
# out of date, and then every object goes in.  Dropping it is also the more
# portable form - "u" is the optional extra, "cr" is everywhere.
$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@ar cr $@ $(OBJS)
	@ranlib $@ >/dev/null 2>&1

install:;

# clean removes what a build produces: the objects, the library and the
# probe result in configure.h, which is regenerated from a compile test on
# every build and so follows the platform and the flags.  It deliberately
# keeps configure.mak, which is not build output but the recorded answer to
# a question - it is the only place a TCPDIR that was not a default exists,
# and "make clean; make" has to keep installing where the last build
# installed.  Dropping it there would mean the one command an operator runs
# between trying a change and keeping it forgets the change.
#
# distclean is the one that forgets.  It means "take this tree back to what a
# fresh checkout looks like, and let the next build ask again", so a TCPDIR
# that was not a default is gone and the platform default comes back.  Same
# name as the autotools target, because it means the same thing: clean, plus
# everything a configure run left behind.
distclean: clean
	@rm -f configure.mak

clean:; @rm -f *.o *.a configure.h *~ \#*\#
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
rundir.o:\
	configure.h\
	rundir.c\
	rundir.h
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
