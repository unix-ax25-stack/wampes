# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/Makefile,v 1.30 1996/08/11 18:17:33 deyke Exp $

LIBRARY       = libutil.a

MAKEFILE      = Makefile

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

RANLIB        = ranlib

all:;   if [ -x /bin/bash ]; then \
		/bin/bash < configure > configure.tmp; \
	elif [ -x /bin/ksh ]; then \
		/bin/ksh < configure > configure.tmp; \
	else \
		/bin/sh < configure > configure.tmp; \
	fi
	if cmp -s configure.tmp configure.h; then \
		rm -f configure.tmp; \
	else \
		rm -f configure.h; \
		mv configure.tmp configure.h; \
	fi
	@case "`uname -sr`" in \
	HP-UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE' \
		'RANLIB=:' \
		_all;; \
	AIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE' \
		'RANLIB=:' \
		_all;; \
	IRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed' \
		'RANLIB=:' \
		_all;; \
	Linux*) \
		if [ -r /lib/libc.so.5 ]; then \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O' \
			_all; \
		else \
			$(MAKE) -i -f $(MAKEFILE) \
			'CC=gcc' \
			'CFLAGS=-O' \
			_all; \
		fi;; \
	386BSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		_all;; \
	FreeBSD*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		_all;; \
	BSD/386*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		_all;; \
	SunOS\ 4.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O' \
		_all;; \
	SunOS\ 5.*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -v' \
		'RANLIB=:' \
		_all;; \
	ULTRIX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC' \
		_all;; \
	A/UX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE' \
		'RANLIB=:' \
		_all;; \
	RISC\ iX*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX' \
		_all;; \
	AOS*) \
		$(MAKE) -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@sleep 1
	@ar cru $@ $(OBJS)
	@$(RANLIB) $@

install:;

clean:; @rm -f *.o *.a *~ \#*\#

depend:; @mkmf -f $(MAKEFILE)
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
