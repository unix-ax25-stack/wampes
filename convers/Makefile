# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.21 1993/07/17 20:35:25 deyke Exp $

CFLAGS     = -I../lib
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
		'CFLAGS=-O6 -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		if [ -x /usr/lang/acc ] ; then \
			make -i \
			'CC=acc' \
			'CFLAGS=-O -s -I../lib' \
			'LIBS=../lib/libutil.a' \
			_all; \
		else \
			make -i \
			'CC=cc' \
			'CFLAGS=-O -s -I../lib' \
			'LIBS=../lib/libutil.a -lsocket -lnsl' \
			_all; \
		fi \
	elif [ $(OS) = ULTRIX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = A/UX ] ; then \
		make -i \
		'CC=gcc -fpcc-struct-return' \
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

convers: convers.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

conversd: conversd.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(RMTARGET)
	cp $? $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
convers.o: ../lib/buildsaddr.h
conversd.o: ../lib/buildsaddr.h ../lib/strdup.h ../lib/md5.h
