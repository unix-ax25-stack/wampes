# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/Makefile,v 1.21 1994/07/22 13:18:20 deyke Exp $

LIBRARY       = libutil.a

MAKEFILE      = Makefile

OBJS          = buildsaddr.o \
		calc_crc.o \
		callvalid.o \
		lockfile.o \
		md5.o \
		seteugid.o \
		strdup.o \
		strtoul.o

RANLIB        = ranlib

all:;   if [ -x /bin/ksh ] ; then \
		/bin/ksh < configure > configure.tmp; \
	else \
		/bin/sh < configure > configure.tmp; \
	fi
	if cmp -s configure.tmp configure.h ; then \
		rm -f configure.tmp; \
	else \
		rm -f configure.h; \
		mv configure.tmp configure.h; \
	fi
	@case "`uname -sr`" in \
	HP-UX*) \
		make -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -D_HPUX_SOURCE' \
		'RANLIB=:' \
		_all;; \
	AIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -D_ALL_SOURCE' \
		'RANLIB=:' \
		_all;; \
	IRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed' \
		'RANLIB=:' \
		_all;; \
	Linux*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O' \
		_all;; \
	386BSD*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		_all;; \
	BSD/386*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		_all;; \
	SunOS\ 4.*) \
		make -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O' \
		_all;; \
	SunOS\ 5.*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O' \
		'RANLIB=:' \
		_all;; \
	ULTRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC' \
		_all;; \
	A/UX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE' \
		'RANLIB=:' \
		_all;; \
	RISC\ iX*) \
		make -i -f $(MAKEFILE) \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i -f $(MAKEFILE) \
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

clean:; @rm -f *.o *.a *~

depend:; @mkmf -f $(MAKEFILE)
###
buildsaddr.o: buildsaddr.h
calc_crc.o: calc_crc.h
callvalid.o: callvalid.h
md5.o: md5.h
seteugid.o: seteugid.h
strdup.o: strdup.h
