# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/aos/Makefile,v 1.1 1994/02/07 12:40:17 deyke Exp $

LIBRARY       = libaos.a

OBJS          = ctime.o \
		getcwd.o \
		putenv.o \
		setsid.o \
		strstr.o

OS            = "`uname -s`"

RANLIB        = ranlib

all:;   @if [ $(OS) = HP-UX ] ; then \
		true; \
	elif [ $(OS) = AIX ] ; then \
		true; \
	elif [ $(OS) = Linux ] ; then \
		true; \
	elif [ $(OS) = 386BSD ] ; then \
		true; \
	elif [ $(OS) = BSD/386 ] ; then \
		true; \
	elif [ $(OS) = SunOS ] ; then \
		true; \
	elif [ $(OS) = ULTRIX ] ; then \
		true; \
	elif [ $(OS) = A/UX ] ; then \
		true; \
	elif [ $(OS) = "RISC iX" ] ; then \
		true; \
	elif [ $(OS) = AOS ] ; then \
		/usr/local/bin/make -i \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2' \
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
ctime.o: time.h ansi.h cdefs.h tzfile.h
