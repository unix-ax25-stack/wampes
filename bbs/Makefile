# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.22 1993/07/18 19:52:54 deyke Exp $

CFLAGS     = -I../lib
LD         = $(CC)
OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = bbs findpath killdup sfstat

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
		(if [ -x /usr/lang/acc ] ; then \
			make -i \
			'CC=acc' \
			'CFLAGS=-O -s -I../lib' \
			'LIBS=../lib/libutil.a' \
			_all; \
		else \
			make -i \
			'CC=cc' \
			'CFLAGS=-O -s -I../lib' \
			'LD=/usr/ucb/cc' \
			'LIBS=../lib/libutil.a' \
			_all; \
		fi) \
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

bbs:    bbs.o
	$(RMTARGET)
	@sleep 1
	$(LD) $(CFLAGS) $? $(LIBS) -o $@

findpath: findpath.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

killdup: killdup.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

sfstat: sfstat.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat /users/bbs/help

/usr/local/bin/bbs: bbs
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp $? $@
	chmod 4555 $@
	ln $@ /usr/local/bin/BBS

/usr/local/bin/sfstat: sfstat
	$(RMTARGET)
	cp $? $@

/users/bbs/help: help
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
bbs.o: bbs.h ../lib/buildsaddr.h ../lib/strdup.h
findpath.o: bbs.h
killdup.o: bbs.h
sfstat.o: bbs.h
