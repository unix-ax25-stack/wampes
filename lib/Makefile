# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/lib/Makefile,v 1.6 1993/07/17 20:34:39 deyke Exp $

LIBRARY       = libutil.a

OBJS          = buildsaddr.o \
		md5.o \
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
		'CFLAGS=-O6' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		if [ -x /usr/lang/acc ] ; then \
			make -i \
			'CC=acc' \
			'CFLAGS=-O' \
			_all; \
		else \
			make -i \
			'CC=cc' \
			'CFLAGS=-O' \
			'RANLIB=:' \
			_all; \
		fi \
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
md5.o: md5.h
strdup.o: strdup.h
