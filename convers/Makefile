# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.15 1993/06/06 08:26:34 deyke Exp $

OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = convers conversd

all:;   @if [ $(OS) = HP-UX ] ; then \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = Linux ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DLINUX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		make -i \
		'CC=acc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = ULTRIX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = A/UX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lposix' \
		_all; \
	elif [ $(OS) = "RISC iX" ] ; then \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	else \
		echo System $(OS) is not supported; \
	fi

_all:   $(TARGETS)

.c:
	$(RMTARGET)
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(RMTARGET)
	cp convers $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp conversd $@

clean:; rm -f *.o $(TARGETS) *~
