# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/Makefile,v 1.3 1993/06/17 07:28:14 deyke Exp $

LIBRARY       = libutil.a

OBJS          = buildsaddr.o \
		strdup.o \
		strtoul.o

OS            = "`uname -s`"

RANLIB        = ranlib

all:;   @if [ $(OS) = HP-UX ] ; then \
		make -i \
		'CC=c89' \
		'CFLAGS=-O' \
		'RANLIB=:' \
		_all; \
	elif [ $(OS) = Linux ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -DLINUX' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		make -i \
		'CC=acc' \
		'CFLAGS=-O' \
		_all; \
	elif [ $(OS) = ULTRIX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -DULTRIX_RISC' \
		_all; \
	elif [ $(OS) = A/UX ] ; then \
		make -i \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -D_POSIX_SOURCE' \
		'RANLIB=:' \
		_all; \
	elif [ $(OS) = "RISC iX" ] ; then \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX' \
		_all; \
	else \
		echo System $(OS) is not supported; \
	fi

_all:   $(LIBRARY)

$(LIBRARY): $(OBJS)
	@echo "Loading $@ ..."
	@sleep 1
	@ar cru $@ $(OBJS)
	@$(RANLIB) $@
	@echo "done"

install:;

clean:; rm -f *.o $(LIBRARY) *~

depend:; @mkmf
###
buildsaddr.o: buildsaddr.h
strdup.o: strdup.h
